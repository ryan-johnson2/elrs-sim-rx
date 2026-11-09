// PICOBOOT over WebUSB: flash a UF2 onto an RP2040 sitting in BOOTSEL mode.
//
// Protocol reference: pico-sdk boot/picoboot.h and picotool picoboot_connection.c.
//   - 32-byte command on the bulk OUT endpoint
//   - optional data phase (OUT for writes, IN for reads)
//   - acknowledgement: a (zero-length) transfer in the opposite direction
//   - on a stall, GET_COMMAND_STATUS (vendor, interface, 0x42) explains why
//
// Works as a browser global (script tag) and as a CommonJS module (tests).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PicoBootLib = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const UF2_MAGIC_START0 = 0x0A324655;
  const UF2_MAGIC_START1 = 0x9E5D5157;
  const UF2_MAGIC_END    = 0x0AB16F30;
  const UF2_FLAG_NOT_MAIN_FLASH    = 0x00000001;
  const UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000;
  const RP2040_FAMILY_ID   = 0xe48bff56;
  const ABSOLUTE_FAMILY_ID = 0xe48bff57;

  const FLASH_BASE   = 0x10000000;
  const FLASH_MAX    = 0x11000000;      // 16 MB window
  const SECTOR_SIZE  = 4096;
  const PAGE_SIZE    = 256;
  const WRITE_CHUNK  = 4096;

  const PICOBOOT_MAGIC = 0x431fd10b;
  const IF_RESET       = 0x41;
  const IF_CMD_STATUS  = 0x42;
  const CMD = { EXCLUSIVE_ACCESS: 0x1, REBOOT: 0x2, FLASH_ERASE: 0x3, READ: 0x84, WRITE: 0x5, EXIT_XIP: 0x6, ENTER_CMD_XIP: 0x7 };
  const STATUS_NAMES = ['OK', 'UNKNOWN_CMD', 'INVALID_CMD_LENGTH', 'INVALID_TRANSFER_LENGTH', 'INVALID_ADDRESS', 'BAD_ALIGNMENT',
    'INTERLEAVED_WRITE', 'REBOOTING', 'UNKNOWN_ERROR', 'INVALID_STATE', 'NOT_PERMITTED', 'INVALID_ARG', 'BUFFER_TOO_SMALL',
    'PRECONDITION_NOT_MET', 'MODIFIED_DATA', 'INVALID_DATA', 'NOT_FOUND', 'UNSUPPORTED_MODIFICATION'];

  const BOOTSEL_FILTER = { vendorId: 0x2e8a, productId: 0x0003 };

  // -------------------------------------------------------------------------
  // UF2
  // -------------------------------------------------------------------------
  // Returns { blocks: [{addr, data: Uint8Array}], numBlocks, familyId }.
  function parseUf2(buffer) {
    const bytes = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
    if (bytes.length === 0 || bytes.length % 512 !== 0) throw new Error(`UF2 size ${bytes.length} is not a multiple of 512`);
    const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const blocks = [];
    let familyId = null;
    let numBlocks = 0;
    for (let off = 0; off < bytes.length; off += 512) {
      if (dv.getUint32(off, true) !== UF2_MAGIC_START0 || dv.getUint32(off + 4, true) !== UF2_MAGIC_START1 ||
          dv.getUint32(off + 508, true) !== UF2_MAGIC_END) {
        throw new Error(`bad UF2 magic at block ${off / 512}`);
      }
      const flags = dv.getUint32(off + 8, true);
      const addr = dv.getUint32(off + 12, true);
      const size = dv.getUint32(off + 16, true);
      numBlocks = dv.getUint32(off + 24, true);
      if (flags & UF2_FLAG_FAMILY_ID_PRESENT) {
        const fam = dv.getUint32(off + 28, true);
        if (familyId === null) familyId = fam;
        if (fam !== RP2040_FAMILY_ID && fam !== ABSOLUTE_FAMILY_ID) {
          throw new Error(`UF2 family 0x${fam.toString(16)} is not RP2040`);
        }
      }
      if (flags & UF2_FLAG_NOT_MAIN_FLASH) continue;
      if (size === 0 || size > 476) throw new Error(`bad payload size ${size} at block ${off / 512}`);
      if (addr < FLASH_BASE || addr + size > FLASH_MAX) throw new Error(`block address 0x${addr.toString(16)} is outside flash`);
      if (addr % PAGE_SIZE !== 0) throw new Error(`block address 0x${addr.toString(16)} is not page aligned`);
      blocks.push({ addr, data: bytes.slice(off + 32, off + 32 + size) });
    }
    if (blocks.length === 0) throw new Error('UF2 contains no flash data');
    blocks.sort((a, b) => a.addr - b.addr);
    return { blocks, numBlocks, familyId };
  }

  // Turn blocks into erase/write work: contiguous runs of 4 KB sectors, each
  // run erased in one command and written in WRITE_CHUNK pieces. Gaps inside a
  // run are filled with 0xFF (the erased value).
  function planFlash(parsed) {
    const sectors = new Map();   // sector base addr -> Uint8Array(4096)
    let bytes = 0;
    for (const b of parsed.blocks) {
      let a = b.addr, o = 0;
      while (o < b.data.length) {
        const base = a - (a % SECTOR_SIZE);
        let s = sectors.get(base);
        if (!s) { s = new Uint8Array(SECTOR_SIZE).fill(0xFF); sectors.set(base, s); }
        const n = Math.min(b.data.length - o, base + SECTOR_SIZE - a);
        s.set(b.data.subarray(o, o + n), a - base);
        a += n; o += n;
      }
      bytes += b.data.length;
    }
    const bases = [...sectors.keys()].sort((x, y) => x - y);
    const runs = [];
    for (const base of bases) {
      const last = runs[runs.length - 1];
      if (last && last.addr + last.size === base) {
        last.size += SECTOR_SIZE; last.sectors.push(sectors.get(base));
      } else {
        runs.push({ addr: base, size: SECTOR_SIZE, sectors: [sectors.get(base)] });
      }
    }
    for (const r of runs) {
      r.image = new Uint8Array(r.size);
      r.sectors.forEach((s, i) => r.image.set(s, i * SECTOR_SIZE));
      delete r.sectors;
    }
    const total = runs.reduce((n, r) => n + r.size, 0);
    return { runs, dataBytes: bytes, flashBytes: total, lowest: runs[0]?.addr, highest: runs.length ? runs[runs.length - 1].addr + runs[runs.length - 1].size : 0 };
  }

  // -------------------------------------------------------------------------
  // PICOBOOT connection
  // -------------------------------------------------------------------------
  class PicoBootError extends Error {
    constructor(msg, status) { super(msg); this.status = status; }
  }

  class PicoBoot {
    // usbDevice: a WebUSB USBDevice (or a test double with the same shape)
    constructor(usbDevice, log = () => {}) {
      this.dev = usbDevice;
      this.log = log;
      this.token = 1;
      this.iface = null; this.outEp = null; this.inEp = null;
    }

    async open() {
      const d = this.dev;
      await d.open();
      if (!d.configuration) await d.selectConfiguration(1);
      // PICOBOOT is the vendor-class interface with one bulk OUT and one bulk IN.
      let found = null;
      for (const itf of d.configuration.interfaces) {
        const alt = itf.alternates[0];
        if (alt.interfaceClass !== 0xff) continue;
        const outs = alt.endpoints.filter(e => e.direction === 'out' && e.type === 'bulk');
        const ins  = alt.endpoints.filter(e => e.direction === 'in'  && e.type === 'bulk');
        if (outs.length === 1 && ins.length === 1) { found = { itf, out: outs[0].endpointNumber, in: ins[0].endpointNumber }; break; }
      }
      if (!found) throw new PicoBootError('no PICOBOOT interface on this device (is it in BOOTSEL mode?)');
      await d.claimInterface(found.itf.interfaceNumber);
      this.iface = found.itf.interfaceNumber; this.outEp = found.out; this.inEp = found.in;
      this.log(`PICOBOOT interface ${this.iface}, OUT ep ${this.outEp}, IN ep ${this.inEp}`);
      await this.reset();
    }

    async close() {
      try { if (this.iface !== null) await this.dev.releaseInterface(this.iface); } catch {}
      try { await this.dev.close(); } catch {}
    }

    async reset() {
      await this.dev.controlTransferOut({ requestType: 'vendor', recipient: 'interface', request: IF_RESET, value: 0, index: this.iface });
    }

    async commandStatus() {
      const r = await this.dev.controlTransferIn({ requestType: 'vendor', recipient: 'interface', request: IF_CMD_STATUS, value: 0, index: this.iface }, 16);
      if (r.status !== 'ok' || r.data.byteLength < 16) return null;
      const dv = r.data;
      return { token: dv.getUint32(0, true), status: dv.getUint32(4, true), cmdId: dv.getUint8(8), inProgress: dv.getUint8(9) };
    }

    buildCmd(cmdId, args, transferLength) {
      const buf = new ArrayBuffer(32);
      const dv = new DataView(buf);
      dv.setUint32(0, PICOBOOT_MAGIC, true);
      dv.setUint32(4, this.token++, true);
      dv.setUint8(8, cmdId);
      dv.setUint8(9, args.length);
      dv.setUint16(10, 0, true);
      dv.setUint32(12, transferLength, true);
      new Uint8Array(buf, 16, 16).set(args);
      return buf;
    }

    async fail(what) {
      let detail = '';
      try {
        const st = await this.commandStatus();
        if (st) detail = ` (status ${STATUS_NAMES[st.status] ?? st.status} for cmd 0x${st.cmdId.toString(16)})`;
      } catch {}
      try { await this.dev.clearHalt('in', this.inEp); await this.dev.clearHalt('out', this.outEp); } catch {}
      throw new PicoBootError(`${what} failed${detail}`);
    }

    // Run one command with optional OUT data, then wait for the IN ack.
    async cmd(cmdId, args, data) {
      const len = data ? data.byteLength : 0;
      let r = await this.dev.transferOut(this.outEp, this.buildCmd(cmdId, args, len));
      if (r.status !== 'ok') await this.fail(`command 0x${cmdId.toString(16)}`);
      if (len) {
        r = await this.dev.transferOut(this.outEp, data);
        if (r.status !== 'ok' || r.bytesWritten !== len) await this.fail(`data for 0x${cmdId.toString(16)}`);
      }
      r = await this.dev.transferIn(this.inEp, 64);
      if (r.status !== 'ok') await this.fail(`ack for 0x${cmdId.toString(16)}`);
    }

    u32(...vals) {
      const a = new Uint8Array(vals.length * 4);
      const dv = new DataView(a.buffer);
      vals.forEach((v, i) => dv.setUint32(i * 4, v >>> 0, true));
      return a;
    }

    exclusiveAccess(mode = 1)   { return this.cmd(CMD.EXCLUSIVE_ACCESS, new Uint8Array([mode]), null); }
    exitXip()                   { return this.cmd(CMD.EXIT_XIP, new Uint8Array(0), null); }
    enterCmdXip()               { return this.cmd(CMD.ENTER_CMD_XIP, new Uint8Array(0), null); }
    flashErase(addr, size)      { return this.cmd(CMD.FLASH_ERASE, this.u32(addr, size), null); }
    write(addr, data)           { return this.cmd(CMD.WRITE, this.u32(addr, data.byteLength), data); }
    async reboot(delayMs = 500) {
      // The device reboots after the delay; the ack usually arrives first, but
      // a missing ack here is not an error.
      try { await this.cmd(CMD.REBOOT, this.u32(0, 0, delayMs), null); } catch (e) { this.log('reboot ack not received (normal)'); }
    }

    // Full flash procedure. onProgress({phase, done, total}) with bytes.
    async flashUf2(uf2Buffer, onProgress = () => {}) {
      const parsed = parseUf2(uf2Buffer);
      const plan = planFlash(parsed);
      this.log(`UF2: ${parsed.blocks.length} blocks, ${plan.dataBytes} bytes, flash 0x${plan.lowest.toString(16)}..0x${plan.highest.toString(16)} in ${plan.runs.length} run(s)`);

      await this.exclusiveAccess(1);
      await this.exitXip();

      let done = 0;
      const total = plan.flashBytes;
      for (const run of plan.runs) {
        onProgress({ phase: 'erase', done, total, addr: run.addr });
        this.log(`erase 0x${run.addr.toString(16)} +${run.size}`);
        await this.flashErase(run.addr, run.size);
        for (let o = 0; o < run.size; o += WRITE_CHUNK) {
          const chunk = run.image.subarray(o, Math.min(o + WRITE_CHUNK, run.size));
          await this.write(run.addr + o, chunk);
          done += chunk.length;
          onProgress({ phase: 'write', done, total, addr: run.addr + o });
        }
      }
      onProgress({ phase: 'reboot', done: total, total });
      this.log('rebooting into new firmware');
      await this.reboot(500);
      return plan;
    }
  }

  async function requestBootselDevice(usb) {
    return usb.requestDevice({ filters: [BOOTSEL_FILTER] });
  }

  return { parseUf2, planFlash, PicoBoot, PicoBootError, requestBootselDevice, BOOTSEL_FILTER, CMD, PICOBOOT_MAGIC, SECTOR_SIZE, WRITE_CHUNK, FLASH_BASE };
});
