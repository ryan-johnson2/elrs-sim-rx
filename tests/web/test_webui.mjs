// Drive web/index.html in mock mode, capture console errors and screenshots.
import { chromium } from 'playwright';
import fs from 'node:fs';

const URL = process.env.WEBUI_URL || 'file:///work/web/index.html?mock';   // ?mock: the in-page simulated device
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

// The controller page is the one a bare address opens
check('controller page is the default', await page.locator('#channels').isVisible() && (await page.locator('#tabs a.on').textContent()) === 'Controller');
check('firmware panel hidden to start with', !(await page.locator('#sec-firmware').isVisible()));
check('no simulated-device link on the page', (await page.locator('#link-mock').count()) === 0 && !(await page.innerText('body')).includes('simulated device'));
check('no footer', (await page.locator('footer').count()) === 0);

// Tooltips: shown on hover, hidden again, and never wider than the window
await page.locator('#sec-rxsettings .tip').scrollIntoViewIfNeeded();
await page.waitForTimeout(200);   // scrolling closes a tooltip, so let it settle first
await page.hover('#sec-rxsettings .tip');
check('tooltip shows on hover', await page.locator('#tipbox').isVisible() && (await page.textContent('#tipbox')).includes('ExpressLRS 4.0'));
await page.hover('#btn-save');
check('buttons carry tooltips too', (await page.textContent('#tipbox')).includes('active profile'));
await page.hover('header h1');
check('tooltip hides when the pointer leaves', !(await page.locator('#tipbox').isVisible()));

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

// Model match: sends the command and reports the result
await page.selectOption('#sel-modelmatch', '7');
await page.click('#btn-modelmatch');
await page.waitForFunction(() => document.querySelector('#modelmatch-result').textContent !== '');
check('model match reports sent', (await page.textContent('#modelmatch-result')) === 'sent: ID 7');

// Receiver settings: load, render by type, write, model-match readback
await page.click('#btn-rxs-load');
await page.waitForSelector('#rxs-fields tr[data-field="9"]');
check('rx settings device line', (await page.textContent('#rxs-device')).includes('Mock RX · 4.0.1'));
check('rx settings rows (hidden one skipped)', (await page.locator('#rxs-fields tr').count()) === 8);
check('rx selection shows value with units', (await page.locator('#rxs-fields tr[data-field="2"] select option:checked').textContent()) === '25mW');
check('rx folder is a heading', (await page.locator('#rxs-fields tr[data-field="3"] th').textContent()) === 'Team Race');
check('rx folder child indented', (await page.locator('#rxs-fields tr[data-field="4"] td.sub').count()) === 1);
check('rx number field', (await page.inputValue('#rxs-fields tr[data-field="5"] input')) === '2');
check('rx info text maps arrow glyph', (await page.locator('#rxs-fields tr[data-field="9"] td').nth(1).textContent()) === '4.0.1 ↑');
check('model match shows receiver value', (await page.textContent('#modelmatch-result')) === 'receiver reports: ID 7');
await page.selectOption('#rxs-fields tr[data-field="2"] select', '3');
await page.waitForFunction(() => document.querySelector('#sec-rxsettings').dataset.busy !== '1');
check('rx selection write persisted', (await page.locator('#rxs-fields tr[data-field="2"] select option:checked').textContent()) === '100mW');
await page.fill('#rxs-fields tr[data-field="5"] input', '9');
await page.dispatchEvent('#rxs-fields tr[data-field="5"] input', 'change');
await page.waitForFunction(() => document.querySelector('#sec-rxsettings').dataset.busy !== '1');
check('rx number write clamped to max', (await page.inputValue('#rxs-fields tr[data-field="5"] input')) === '6');
page.once('dialog', (d) => d.dismiss());
await page.selectOption('#rxs-fields tr[data-field="1"] select', '2');
await page.waitForTimeout(200);
check('rx protocol change away from CRSF needs confirming', (await page.inputValue('#rxs-fields tr[data-field="1"] select')) === '0');
await page.click('#rxs-fields tr[data-field="7"] button');
await page.waitForFunction(() => document.querySelector('#rxs-fields tr[data-field="7"] span').textContent === 'done');
check('rx command ran to completion', true);
await page.selectOption('#sel-modelmatch', 'off');
await page.click('#btn-modelmatch');
await page.waitForFunction(() => document.querySelector('#modelmatch-result').textContent === 'receiver reports: Off');
check('model match set reads back from receiver', true);
await page.screenshot({ path: `${OUT}/07-rx-settings.png`, fullPage: true });

// Save clears dirty
await page.click('#btn-save');
await page.waitForTimeout(1300);
check('dirty indicator hidden after save', !(await page.locator('#dirty').isVisible()));

// Defaults restores mapping
await page.click('#btn-defaults');
await page.waitForTimeout(600);
check('defaults restored axis 0 to ch1', (await page.inputValue('#ax-ch-0')) === '1');

// Profiles: save the mapping to a second slot, switch between them, delete one
check('profile controls shown', await page.locator('#profile-box').isVisible());
check('one profile to start with', (await page.locator('#sel-profile option').count()) === 1);
check('sixteen slots offered', (await page.locator('#sel-slot option').count()) === 16);
await page.selectOption('#ax-ch-1', '6');
await page.waitForTimeout(300);
await page.selectOption('#sel-slot', '2');
await page.fill('#in-profile-name', 'Lift"off');
await page.click('#btn-profile-save');
await page.waitForTimeout(600);
check('saving to slot 2 makes it active', (await page.inputValue('#sel-profile')) === '2');
check('profile name is cleaned and shown', (await page.locator('#sel-profile option:checked').textContent()) === '2: Liftoff');
check('saving clears the unsaved marker', !(await page.locator('#dirty').isVisible()));
await page.selectOption('#sel-profile', '1');
await page.waitForTimeout(600);
check('profile 1 brings its own mapping back', (await page.inputValue('#ax-ch-1')) === '2');
await page.selectOption('#sel-profile', '2');
await page.waitForTimeout(600);
check('profile 2 kept its mapping', (await page.inputValue('#ax-ch-1')) === '6');
await page.selectOption('#sel-slot', '2');
page.once('dialog', (d) => d.accept());
await page.click('#btn-profile-delete');
await page.waitForTimeout(400);
check('the active profile cannot be deleted', (await page.textContent('#profile-result')).includes('cannot delete the active profile'));
await page.selectOption('#sel-slot', '1');
await page.fill('#in-profile-name', 'Velocidrone');
await page.click('#btn-profile-rename');
await page.waitForTimeout(400);
check('rename shows in the slot list', (await page.locator('#sel-slot option').first().textContent()) === '1: Velocidrone');
page.once('dialog', (d) => d.accept());
await page.click('#btn-profile-delete');
await page.waitForTimeout(400);
check('deleting profile 1 leaves one profile', (await page.locator('#sel-profile option').count()) === 1);
check('slot 1 reads empty again', (await page.locator('#sel-slot option').first().textContent()) === '1: (empty)');
await page.screenshot({ path: `${OUT}/10-profiles.png`, fullPage: true });

// Profile export and import. Here: profile 2 "Liftoff" is the only one, with axis 1 on channel 6.
await page.selectOption('#ax-ch-0', '4');
await page.waitForTimeout(300);
await page.click('#btn-profile-export');
await page.waitForTimeout(300);
check('export refuses while there are unsaved changes', (await page.textContent('#profile-result')).includes('Save the unsaved changes first'));
await page.selectOption('#sel-slot', '3');
await page.fill('#in-profile-name', 'Sim B');
await page.click('#btn-profile-save');
await page.waitForTimeout(600);
const [download] = await Promise.all([page.waitForEvent('download'), page.click('#btn-profile-export')]);
const exportPath = await download.path();
const exported = JSON.parse(fs.readFileSync(exportPath, 'utf8'));
await page.waitForTimeout(800);
check('export file is named and dated', /^simrx-profiles-\d{4}-\d\d-\d\d\.json$/.test(download.suggestedFilename()), download.suggestedFilename());
check('export holds both profiles by slot', exported.format === 'elrs-sim-rx-profiles' && exported.profiles.map((p) => p.slot).join() === '2,3');
check('export carries names and mappings', exported.profiles[0].name === 'Liftoff' && exported.profiles[0].axes[0].ch === 1 && exported.profiles[0].axes[1].ch === 6
  && exported.profiles[1].name === 'Sim B' && exported.profiles[1].axes[0].ch === 4 && exported.profiles[1].buttons.length === 36);
check('export leaves the active profile where it was', (await page.inputValue('#sel-profile')) === '3' && (await page.inputValue('#ax-ch-0')) === '4');
check('export reports what it did', (await page.textContent('#profile-result')).includes('Exported 2 profiles'));
// wreck both, then bring them back from the file
await page.selectOption('#sel-slot', '2');
page.once('dialog', (d) => d.accept());
await page.click('#btn-profile-delete');
await page.waitForTimeout(400);
await page.selectOption('#ax-ch-0', '8');
await page.waitForTimeout(300);
await page.click('#btn-save');
await page.waitForTimeout(1300);
check('one profile left before the import', (await page.locator('#sel-profile option').count()) === 1);
let importPrompt = '';
page.once('dialog', (d) => { importPrompt = d.message(); d.accept(); });
await page.setInputFiles('#file-profiles', exportPath);
await page.waitForTimeout(3000);
check('import asks, naming what it replaces', importPrompt.includes('Import 2 profiles') && importPrompt.includes('3: Sim B'), importPrompt);
check('import reports what it did', (await page.textContent('#profile-result')).includes('Imported 2 profiles'), await page.textContent('#profile-result'));
check('import restores both profiles', (await page.locator('#sel-profile option').count()) === 2);
check('import returns to the profile that was active, restored', (await page.inputValue('#sel-profile')) === '3' && (await page.inputValue('#ax-ch-0')) === '4');
check('import leaves nothing unsaved', !(await page.locator('#dirty').isVisible()));
await page.selectOption('#sel-profile', '2');
await page.waitForTimeout(600);
check('imported profile 2 has its name and mapping', (await page.locator('#sel-profile option:checked').textContent()) === '2: Liftoff'
  && (await page.inputValue('#ax-ch-0')) === '1' && (await page.inputValue('#ax-ch-1')) === '6');
await page.setInputFiles('#file-profiles', { name: 'other.json', mimeType: 'application/json', buffer: Buffer.from('{"profiles":[]}') });
await page.waitForTimeout(300);
check('a file that is not a profile export is refused', (await page.textContent('#profile-result')).includes('not an ELRS Sim RX profile file'));
const broken = JSON.parse(JSON.stringify(exported)); broken.profiles[0].axes[0].ch = 99;
await page.setInputFiles('#file-profiles', { name: 'broken.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(broken)) });
await page.waitForTimeout(300);
check('a file with an out-of-range channel is refused before anything is sent', (await page.textContent('#profile-result')).includes('profile 2: bad axes')
  && (await page.inputValue('#ax-ch-0')) === '1');
await page.screenshot({ path: `${OUT}/11-profile-import.png`, fullPage: true });
// put things back as the later checks expect: profile 2 alone
await page.selectOption('#sel-slot', '3');
page.once('dialog', (d) => d.accept());
await page.click('#btn-profile-delete');
await page.waitForTimeout(400);

// Tabs: the console, the docs and dongle firmware each have their own page
check('console hidden on the controller page', !(await page.locator('#console').isVisible()));
check('firmware panel hidden on the controller page', !(await page.locator('#sec-firmware').isVisible()));
await page.click('#tab-docs');
await page.waitForTimeout(200);
check('docs visible on their tab', await page.locator('#docs-start').isVisible() && await page.locator('#docs-console').isVisible());
check('channels hidden on the docs tab', !(await page.locator('#channels').isVisible()));
await page.screenshot({ path: `${OUT}/08-docs.png`, fullPage: true });
await page.click('.toc a[href="#docs-windows"]');
await page.waitForTimeout(200);
check('docs contents link scrolls to its section', await page.evaluate(() => { const t = document.getElementById('docs-windows').getBoundingClientRect().top; return t >= 0 && t < 100; }));
check('docs tab stays lit on a section link', (await page.locator('#tabs a.on').textContent()) === 'Docs');
await page.click('#tab-main');
await page.click('section[data-page="main"] a[href="#receiver-update"]');
await page.waitForTimeout(200);
check('controller links to the RX flashing tab', await page.locator('#btn-rxfw-disconnect').isVisible() && (await page.locator('#tabs a.on').textContent()) === 'RX flashing');
check('RX flashing links to the ExpressLRS flasher', (await page.locator('#sec-rxfw a.btn').getAttribute('href')) === 'https://expresslrs.github.io/web-flasher/');
check('RX flashing page is short', (await page.innerText('#sec-rxfw')).length < 400, String((await page.innerText('#sec-rxfw')).length));
await page.screenshot({ path: `${OUT}/12-rx-flashing.png`, fullPage: false });
await page.click('#sec-rxfw a[href="#docs-receiver-update"]');
await page.waitForTimeout(200);
check('RX flashing links to the full guide in docs', await page.locator('#docs-receiver-update').isVisible());
await page.evaluate(() => { location.hash = '#main'; });
await page.waitForTimeout(200);
check('old link #main still opens the controller', await page.locator('#channels').isVisible());
await page.click('#tab-console');
await page.waitForTimeout(200);
check('console visible on its tab', await page.locator('#console').isVisible());
check('counters visible on the console tab', await page.locator('#st-frames').isVisible());
await page.waitForTimeout(1200);   // at least one status poll
check('status polling stays out of the console', !(await page.textContent('#console')).includes('> status'));
check('counters still update from the quiet poll', /^\d+ \/ \d+ \/ \d+$/.test((await page.textContent('#st-frames')).trim()), await page.textContent('#st-frames'));

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

await page.screenshot({ path: `${OUT}/09-console.png`, fullPage: true });

// Firmware update flow against the in-page mock BOOTSEL device
await page.click('#tab-fw');
await page.waitForTimeout(200);
check('firmware panel visible on its tab', await page.locator('#sec-firmware').isVisible());
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
const fwNames = await page.locator('#fw-select option').allTextContents();
check('firmware names are short', fwNames.every((n) => n.length <= 32), fwNames.join(' | '));
const fwStatus = await page.textContent('#fw-status');
check('mock flash completed', fwStatus.startsWith('Flashed'), fwStatus);
check('progress bar full', (await page.getAttribute('#fw-bar', 'style') || '').includes('100%'));
const fwLogText = await page.textContent('#fw-log');
check('flash log shows erase and reboot', fwLogText.includes('erase 0x10000000') && fwLogText.includes('rebooting'));
await page.screenshot({ path: `${OUT}/06-firmware-flashed-dev.png`, fullPage: true });

// Reconnect mock for the remaining checks
await page.click('#btn-connect');
await page.click('#tab-main');
await page.waitForTimeout(1200);
check('reconnected after flash', await page.locator('#btn-disconnect').isVisible());

// One-click update: enters update mode and flashes with no second click
await page.click('#tab-fw');
await page.evaluate(() => { document.getElementById('fw-status').textContent = ''; });
await page.click('#btn-fw-auto');
await page.waitForFunction(() => document.getElementById('fw-status').textContent.startsWith('Flashed'), null, { timeout: 15000 }).catch(() => {});
check('one-click update completed', (await page.textContent('#fw-status')).startsWith('Flashed'), await page.textContent('#fw-status'));
check('one-click update disconnected the device', await page.locator('#btn-connect').isVisible());
check('update button usable again', !(await page.locator('#btn-fw-auto').isDisabled()));
await page.click('#btn-connect');
await page.click('#tab-main');
await page.waitForTimeout(1200);
check('reconnected after one-click update', await page.locator('#btn-disconnect').isVisible());

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
