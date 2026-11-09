// Drive web/index.html in mock mode, capture console errors and screenshots.
import { chromium } from 'playwright';

const URL = process.env.WEBUI_URL || 'file:///work/web/index.html?mock';
const OUT = process.env.SHOTS_DIR || '/shots';
const errors = [];
const results = [];
const check = (name, cond, extra = '') => { results.push({ name, ok: !!cond, extra }); };

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 1200, height: 1400 }, colorScheme: 'light' });
const page = await ctx.newPage();
page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') errors.push(`[${m.type()}] ${m.text()}`); });
page.on('pageerror', (e) => errors.push(`[pageerror] ${e.message}`));

await page.goto(URL);
await page.waitForTimeout(1500);   // let mock connect, map/status load, stream start

// Connected UI state
check('disconnect button visible', await page.locator('#btn-disconnect').isVisible());
check('status version shows mock', (await page.textContent('#st-version')).includes('mock'));
check('rx power button reads ON', (await page.textContent('#btn-rxpower')).includes('ON'));
check('16 channel rows', (await page.locator('#channels .ch').count()) === 16);
check('8 axis rows', (await page.locator('#axes tr').count()) === 8);
const btnRowsCollapsed = await page.locator('#buttons tr').count();
check('button table collapsed to 12 rows', btnRowsCollapsed === 12, `got ${btnRowsCollapsed}`);
await page.waitForTimeout(600);
const ch1 = await page.textContent('#chval-0');
check('channel 1 value is numeric and moving', /^\d+$/.test(ch1.trim()) && ch1.trim() !== '–', ch1);
const rate = await page.textContent('#ch-rate');
check('update rate shown', /\d+ updates\/s/.test(rate), rate);
const axVal = await page.textContent('#ax-val-0');
check('axis X value derived from ch1', /^-?\d+$/.test(axVal.trim()), axVal);

await page.screenshot({ path: `${OUT}/01-connected-light.png`, fullPage: true });

// Change axis mapping: X <- ch2, invert
await page.selectOption('#ax-ch-0', '2');
await page.waitForTimeout(200);
await page.check('#ax-inv-0');
await page.waitForTimeout(400);
check('axis 0 select persisted as ch2 after map refresh', (await page.inputValue('#ax-ch-0')) === '2');
check('axis 0 invert persisted', await page.isChecked('#ax-inv-0'));
await page.waitForTimeout(1200);   // status poll
check('dirty indicator visible after change', await page.locator('#dirty').isVisible());

// Buttons: show all, change button 20 to ch 9 high
await page.click('#btn-buttons-toggle');
await page.waitForTimeout(200);
check('button table expanded to 36 rows', (await page.locator('#buttons tr').count()) === 36);
await page.selectOption('#bt-ch-20', '9');
await page.waitForTimeout(200);
await page.selectOption('#bt-lv-20', 'high');
await page.waitForTimeout(400);
check('button 20 channel persisted', (await page.inputValue('#bt-ch-20')) === '9');
check('button 20 level persisted', (await page.inputValue('#bt-lv-20')) === 'high');

// Throttle + failsafe
await page.selectOption('#sel-throttle', '0');
await page.waitForTimeout(300);
check('throttle axis persisted', (await page.inputValue('#sel-throttle')) === '0');
await page.fill('#in-failsafe', '150');
await page.dispatchEvent('#in-failsafe', 'change');
await page.waitForTimeout(300);
check('failsafe persisted', (await page.inputValue('#in-failsafe')) === '150');

// RX power toggle -> OFF, channels stop, pill updates
await page.click('#btn-rxpower');
await page.waitForTimeout(1500);
check('rx power button reads OFF', (await page.textContent('#btn-rxpower')).includes('OFF'));
check('link pill says RX off', (await page.textContent('#pill-link')).includes('RX off'));
await page.screenshot({ path: `${OUT}/02-rx-off.png`, fullPage: false });
await page.click('#btn-rxpower');
await page.waitForTimeout(800);

// Save clears dirty
await page.click('#btn-save');
await page.waitForTimeout(1300);
check('dirty indicator hidden after save', !(await page.locator('#dirty').isVisible()));

// Defaults restores mapping
await page.click('#btn-defaults');
await page.waitForTimeout(600);
check('defaults restored axis 0 to ch1', (await page.inputValue('#ax-ch-0')) === '1');

// Developer toggle: console hidden by default, shown after toggle
check('console hidden by default', !(await page.locator('#console').isVisible()));
check('firmware panel visible by default', await page.locator('#sec-firmware').isVisible());
await page.click('#btn-dev');
await page.waitForTimeout(200);
check('console visible in dev mode', await page.locator('#console').isVisible());
check('counters visible in dev mode', await page.locator('#st-frames').isVisible());

// Raw console command
await page.fill('#in-cmd', 'version');
await page.press('#in-cmd', 'Enter');
await page.waitForTimeout(300);
const consoleText = await page.textContent('#console');
check('console shows sent command', consoleText.includes('> version'));
check('console shows reply', consoleText.includes('"version":"mock"'));
await page.fill('#in-cmd', 'bogus');
await page.press('#in-cmd', 'Enter');
await page.waitForTimeout(300);
check('console shows error for unknown command', (await page.textContent('#console')).includes('error: unknown command'));

// Firmware update flow against the in-page mock BOOTSEL device
const uf2 = process.env.UF2_PATH || '/work/firmware/build/elrs_sim_rx.uf2';
await page.setInputFiles('#fw-file', uf2);
await page.waitForTimeout(400);
check('uf2 accepted', (await page.textContent('#fw-info')).includes('blocks'), await page.textContent('#fw-info'));
check('download enabled after pick', !(await page.locator('#btn-fw-download').isDisabled()));
await page.click('#btn-fw-step1');
await page.waitForTimeout(600);
check('step 1 disconnected the device', await page.locator('#btn-connect').isVisible());
check('step 2 enabled', !(await page.locator('#btn-fw-step2').isDisabled()));
await page.click('#btn-fw-step2');
await page.waitForFunction(() => document.getElementById('fw-status').textContent.startsWith('Flashed'), null, { timeout: 15000 }).catch(() => {});
const fwStatus = await page.textContent('#fw-status');
check('mock flash completed', fwStatus.startsWith('Flashed'), fwStatus);
check('progress bar full', (await page.getAttribute('#fw-bar', 'style') || '').includes('100%'));
const fwLogText = await page.textContent('#fw-log');
check('flash log shows erase and reboot', fwLogText.includes('erase 0x10000000') && fwLogText.includes('rebooting'));
await page.screenshot({ path: `${OUT}/06-firmware-flashed-dev.png`, fullPage: true });

// Reconnect mock for the remaining checks
await page.click('#link-mock');
await page.waitForTimeout(1200);
check('reconnected after flash', await page.locator('#btn-disconnect').isVisible());

// Dark mode
await page.emulateMedia({ colorScheme: 'dark' });
await page.waitForTimeout(300);
await page.screenshot({ path: `${OUT}/03-connected-dark.png`, fullPage: true });

// Half-screen window (page beside a simulator), still desktop only: Web Serial
// does not exist on phone browsers.
await page.setViewportSize({ width: 800, height: 900 });
await page.waitForTimeout(300);
const hasHScroll = await page.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth + 1);
check('no horizontal scroll at 800px', !hasHScroll);
await page.screenshot({ path: `${OUT}/04-half-screen-dark.png`, fullPage: false });

// Disconnect
await page.setViewportSize({ width: 1200, height: 900 });
await page.click('#btn-disconnect');
await page.waitForTimeout(500);
check('connect button visible after disconnect', await page.locator('#btn-connect').isVisible());
check('controls disabled after disconnect', await page.locator('#btn-save').isDisabled());
await page.screenshot({ path: `${OUT}/05-disconnected.png`, fullPage: false });

await browser.close();

let fails = 0;
for (const r of results) { console.log(`${r.ok ? 'PASS' : 'FAIL'}  ${r.name}${r.extra ? '  (' + r.extra + ')' : ''}`); if (!r.ok) fails++; }
console.log(`\n${results.length - fails}/${results.length} checks passed`);
if (errors.length) { console.log('\nBrowser console errors/warnings:'); for (const e of errors) console.log('  ' + e); }
else console.log('\nNo browser console errors or warnings.');
process.exit(fails || errors.length ? 1 : 0);
