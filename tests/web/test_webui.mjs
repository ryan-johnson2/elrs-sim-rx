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

// Dark mode
await page.emulateMedia({ colorScheme: 'dark' });
await page.waitForTimeout(300);
await page.screenshot({ path: `${OUT}/03-connected-dark.png`, fullPage: true });

// Narrow viewport
await page.setViewportSize({ width: 400, height: 900 });
await page.waitForTimeout(300);
const hasHScroll = await page.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth + 1);
check('no horizontal scroll at 400px', !hasHScroll);
await page.screenshot({ path: `${OUT}/04-mobile-dark.png`, fullPage: false });

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
