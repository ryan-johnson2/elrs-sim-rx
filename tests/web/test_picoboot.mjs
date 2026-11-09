// Node test for web/picoboot.js: parse the real build UF2, "flash" it to a
// simulated BOOTSEL device, and check the resulting flash image.
import { createRequire } from 'node:module';
import fs from 'node:fs';
const require = createRequire(import.meta.url);
const lib = require(process.env.PICOBOOT_JS || '/work/web/picoboot.js');
const { parseUf2, planFlash, PicoBoot, CMD, PICOBOOT_MAGIC, SECTOR_SIZE, FLASH_BASE } = lib;

let fails = 0, checks = 0;
const check = (name, cond, extra = '') => { checks++; if (!cond) { fails++; console.log(`FAIL  ${name}${extra ? ' (' + extra + ')' : ''}`); } else console.log(`PASS  ${name}`); };

// --------------------------------------------------------------------------
// Simulated RP2040 in BOOTSEL: decodes PICOBOOT commands, keeps a flash image.
// --------------------------------------------------------------------------
class FakeBootsel {
  constructor() {
    this.opened = false; this.claimed = null; this.configuration = null;
    this.flash = new Uint8Array(2 * 1024 * 1024).fill(0x00);   // "dirty" flash
    this.erased = [];      // {addr,size}
    this.writes = [];      // {addr,len}
    this.seq = [];         // command ids in order
    this.xipExited = false; this.exclusive = 0; this.rebooted = null;
    this.pendingData = null; this.resets = 0;
  }
  async open() { this.opened = true; }
  async close() { this.opened = false; }
  async selectConfiguration(n) {
    this.configuration = { configurationValue: n, interfaces: [
      { interfaceNumber: 0, alternates: [{ interfaceClass: 0x08, endpoints: [{ direction: 'out', endpointNumber: 1, type: 'bulk' }, { direction: 'in', endpointNumber: 2, type: 'bulk' }] }] },
      { interfaceNumber: 1, alternates: [{ interfaceClass: 0xff, endpoints: [{ direction: 'out', endpointNumber: 3, type: 'bulk' }, { direction: 'in', endpointNumber: 4, type: 'bulk' }] }] },
    ] };
  }
  async claimInterface(n) { if (n !== 1) throw new Error('cannot claim MSC'); this.claimed = n; }
  async releaseInterface() { this.claimed = null; }
  async controlTransferOut(setup) { if (setup.request === 0x41) this.resets++; return { status: 'ok', bytesWritten: 0 }; }
  async controlTransferIn(setup, len) {
    const b = new ArrayBuffer(16); const dv = new DataView(b);
    dv.setUint32(4, this.lastStatus ?? 0, true);
    return { status: 'ok', data: dv };
  }
  async clearHalt() {}
  async transferOut(ep, data) {
    if (ep !== 3) throw new Error('wrong OUT endpoint ' + ep);
    const bytes = data instanceof ArrayBuffer ? new Uint8Array(data) : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    if (this.pendingData) {
      const { addr, size } = this.pendingData;
      if (bytes.length !== size) { this.lastStatus = 3; this.pendingData = null; return { status: 'stall' }; }
      this.flash.set(bytes, addr - FLASH_BASE);
      this.writes.push({ addr, len: size });
      this.pendingData = null;
      this.ackPending = true;
      return { status: 'ok', bytesWritten: bytes.length };
    }
    if (bytes.length !== 32) { this.lastStatus = 2; return { status: 'stall' }; }
    const dv = new DataView(bytes.buffer, bytes.byteOffset, 32);
    if (dv.getUint32(0, true) !== PICOBOOT_MAGIC) { this.lastStatus = 1; return { status: 'stall' }; }
    const id = dv.getUint8(8), argLen = dv.getUint8(9), xfer = dv.getUint32(12, true);
    const a0 = dv.getUint32(16, true), a1 = dv.getUint32(20, true), a2 = dv.getUint32(24, true);
    this.seq.push(id);
    switch (id) {
      case CMD.EXCLUSIVE_ACCESS: if (argLen !== 1) return this.stall(2); this.exclusive = bytes[16]; break;
      case CMD.EXIT_XIP: this.xipExited = true; break;
      case CMD.FLASH_ERASE:
        if (!this.xipExited) return this.stall(9);
        if (a0 % SECTOR_SIZE || a1 % SECTOR_SIZE) return this.stall(5);
        if (a0 < FLASH_BASE || a0 + a1 > FLASH_BASE + this.flash.length) return this.stall(4);
        this.flash.fill(0xFF, a0 - FLASH_BASE, a0 - FLASH_BASE + a1);
        this.erased.push({ addr: a0, size: a1 });
        break;
      case CMD.WRITE:
        if (!this.xipExited) return this.stall(9);
        if (a0 % 256 || a1 % 256) return this.stall(5);
        if (xfer !== a1) return this.stall(3);
        this.pendingData = { addr: a0, size: a1 };
        return { status: 'ok', bytesWritten: 32 };
      case CMD.REBOOT: this.rebooted = { pc: a0, sp: a1, delay: a2 }; break;
      default: return this.stall(1);
    }
    this.ackPending = true;
    return { status: 'ok', bytesWritten: 32 };
  }
  stall(code) { this.lastStatus = code; return { status: 'stall' }; }
  async transferIn(ep, len) {
    if (ep !== 4) throw new Error('wrong IN endpoint ' + ep);
    if (!this.ackPending) { this.lastStatus = 9; return { status: 'stall' }; }
    this.ackPending = false;
    return { status: 'ok', data: new DataView(new ArrayBuffer(0)) };
  }
}

// --------------------------------------------------------------------------
// Tests
// --------------------------------------------------------------------------
const uf2Path = process.env.UF2_PATH || '/work/firmware/build/elrs_sim_rx.uf2';
if (!fs.existsSync(uf2Path)) { console.log(`no UF2 at ${uf2Path}; run tools/build.sh first`); process.exit(2); }
const uf2 = new Uint8Array(fs.readFileSync(uf2Path));

// Parse
const parsed = parseUf2(uf2);
check('UF2 parsed', parsed.blocks.length > 0, `${parsed.blocks.length} blocks`);
check('UF2 block count matches header', parsed.blocks.length === parsed.numBlocks, `${parsed.blocks.length} vs ${parsed.numBlocks}`);
check('UF2 family is RP2040', parsed.familyId === 0xe48bff56, parsed.familyId?.toString(16));
check('first block at flash base', parsed.blocks[0].addr === FLASH_BASE, '0x' + parsed.blocks[0].addr.toString(16));
check('blocks are 256 bytes', parsed.blocks.every(b => b.data.length === 256));

// Plan
const plan = planFlash(parsed);
check('single contiguous run', plan.runs.length === 1, `${plan.runs.length}`);
check('run is sector aligned', plan.runs.every(r => r.addr % SECTOR_SIZE === 0 && r.size % SECTOR_SIZE === 0));
check('flash bytes >= data bytes', plan.flashBytes >= plan.dataBytes, `${plan.flashBytes} >= ${plan.dataBytes}`);

// Expected image from the UF2 itself
const expected = new Uint8Array(plan.flashBytes).fill(0xFF);
for (const b of parsed.blocks) expected.set(b.data, b.addr - plan.lowest);

// Flash to the fake device
const fake = new FakeBootsel();
const log = [];
const pb = new PicoBoot(fake, (m) => log.push(m));
const progress = [];
await pb.open();
check('claimed PICOBOOT interface 1', fake.claimed === 1);
check('interface reset sent', fake.resets === 1);
await pb.flashUf2(uf2, (p) => progress.push(p));
await pb.close();

check('command order: exclusive, exit xip, erase first', fake.seq[0] === CMD.EXCLUSIVE_ACCESS && fake.seq[1] === CMD.EXIT_XIP && fake.seq[2] === CMD.FLASH_ERASE, fake.seq.slice(0, 3).join(','));
check('last command is reboot', fake.seq[fake.seq.length - 1] === CMD.REBOOT);
check('reboot into flash with delay', fake.rebooted && fake.rebooted.pc === 0 && fake.rebooted.sp === 0 && fake.rebooted.delay === 500, JSON.stringify(fake.rebooted));
check('one erase per run', fake.erased.length === plan.runs.length, `${fake.erased.length}`);
check('erase covers the run', fake.erased[0].addr === plan.lowest && fake.erased[0].size === plan.flashBytes);
check('writes cover erased bytes exactly', fake.writes.reduce((n, w) => n + w.len, 0) === plan.flashBytes);
check('writes are 4K chunks', fake.writes.every(w => w.len === 4096), `${new Set(fake.writes.map(w => w.len)).size} distinct sizes`);

const got = fake.flash.subarray(plan.lowest - FLASH_BASE, plan.highest - FLASH_BASE);
let mismatch = -1;
for (let i = 0; i < expected.length; i++) if (got[i] !== expected[i]) { mismatch = i; break; }
check('flash image matches UF2 payload byte for byte', mismatch < 0, mismatch >= 0 ? `first mismatch at +0x${mismatch.toString(16)}` : `${expected.length} bytes`);
check('flash outside the image untouched', fake.flash[plan.highest - FLASH_BASE] === 0x00 && fake.flash[fake.flash.length - 1] === 0x00);

check('progress reported erase, write, reboot', progress.some(p => p.phase === 'erase') && progress.some(p => p.phase === 'write') && progress.at(-1).phase === 'reboot');
check('progress ends at total', progress.at(-1).done === progress.at(-1).total);

// Error paths
try { parseUf2(new Uint8Array(100)); check('rejects non-512-multiple', false); } catch (e) { check('rejects non-512-multiple', /512/.test(e.message)); }
const bad = uf2.slice(0, 512); bad[0] = 0;
try { parseUf2(bad); check('rejects bad magic', false); } catch (e) { check('rejects bad magic', /magic/.test(e.message)); }
const wrongFam = uf2.slice(0, 512); new DataView(wrongFam.buffer).setUint32(28, 0xe48bff59, true);
try { parseUf2(wrongFam); check('rejects RP2350 family', false); } catch (e) { check('rejects RP2350 family', /RP2040/.test(e.message)); }

// A device with no vendor interface
const noItf = new FakeBootsel();
noItf.selectConfiguration = async function () { this.configuration = { interfaces: [{ interfaceNumber: 0, alternates: [{ interfaceClass: 0x08, endpoints: [] }] }] }; };
try { await new PicoBoot(noItf).open(); check('open fails without PICOBOOT interface', false); } catch (e) { check('open fails without PICOBOOT interface', /PICOBOOT/.test(e.message)); }

// Stall surfaces a status name
const staller = new FakeBootsel();
const pb2 = new PicoBoot(staller);
await pb2.open();
await pb2.exitXip();
try { await pb2.flashErase(FLASH_BASE + 100, 4096); check('misaligned erase reports BAD_ALIGNMENT', false); }
catch (e) { check('misaligned erase reports BAD_ALIGNMENT', /BAD_ALIGNMENT/.test(e.message), e.message); }

console.log(`\n${checks - fails}/${checks} checks passed`);
process.exit(fails ? 1 : 0);
