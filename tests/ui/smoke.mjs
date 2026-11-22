// UI smoke test: open demo -> analyze -> rename -> search -> save -> undo.
// Run: node tests/ui/smoke.mjs   (needs Playwright + Chromium; PYTHON env selects interpreter)
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import path from 'node:path';
import fs from 'node:fs';
import os from 'node:os';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..', '..');
const shots = process.env.SHOTS || '';
const py = process.env.PYTHON || 'python3';
const home = fs.mkdtempSync(path.join(os.tmpdir(), 'reversa-ui-'));
const srv = spawn(py, ['-m', 'reversa', '--demo', '--no-browser', '--port', '0'], {
  cwd: root, env: { ...process.env, PYTHONPATH: path.join(root, 'src'), HOME: home },
});
let out = '';
const url = await new Promise((resolve, reject) => {
  const t = setTimeout(() => reject(new Error('server did not start: ' + out)), 20000);
  srv.stdout.on('data', (d) => { out += d; const m = out.match(/http:\/\/127\.0\.0\.1:\d+\/\?token=\S+/); if (m) { clearTimeout(t); resolve(m[0]); } });
  srv.stderr.on('data', (d) => { out += d; });
});
const fail = (msg) => { throw new Error(msg); };
const browser = await chromium.launch();
let ok = false;
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 860 } });
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.goto(url);
  // onboarding is shown on first run and can be skipped
  await page.waitForSelector('#onboarding:not([hidden])');
  if (shots) await page.screenshot({ path: path.join(shots, '01-onboarding.png') });
  await page.click('#onb-skip');
  await page.waitForSelector('.frow[data-addr]', { timeout: 20000 });
  const names = await page.$$eval('.frow .nm', (els) => els.map((e) => e.textContent));
  if (!names.some((n) => n.includes('main'))) fail('main not in function list: ' + names);
  if (!names.some((n) => n.includes('check_password'))) fail('check_password missing');
  // listing shows instructions of main
  await page.waitForSelector('#listing .ln');
  if (shots) await page.screenshot({ path: path.join(shots, '02-listing.png') });
  // pseudocode of main
  await page.click('button[data-tab="pseudocode"]');
  await page.waitForSelector('#pseudo');
  const code = await page.textContent('#pseudo');
  if (!code.includes('check_user(') || !code.includes('puts(')) fail('pseudocode incomplete: ' + code.slice(0, 400));
  if (shots) await page.screenshot({ path: path.join(shots, '03-pseudocode.png') });
  // details show xrefs; select check_user via click in pseudocode and rename it with the keyboard
  await page.click('#pseudo .sk-func:text("check_user")');
  await page.waitForSelector('.details-body .name:text("check_user")');
  await page.click('button[data-tab="pseudocode"]');
  await page.waitForSelector('#pseudo');
  await page.keyboard.press('n');
  await page.waitForSelector('#dlg[open]');
  await page.fill('#dlg-input', 'overenie_pouzivatela');
  await page.click('#dlg-ok');
  await page.waitForSelector('.frow .nm:text("overenie_pouzivatela")', { timeout: 5000 });
  await page.click('.frow .nm:text("★ main")');
  await page.click('button[data-tab="pseudocode"]');
  await page.waitForFunction(() => document.querySelector('#pseudo') && document.querySelector('#pseudo').textContent.includes('overenie_pouzivatela('));
  // call graph and CFG render as SVG
  await page.click('button[data-tab="callgraph"]');
  await page.waitForSelector('svg.graph .gnode');
  if (shots) await page.screenshot({ path: path.join(shots, '04-callgraph.png') });
  await page.click('button[data-tab="cfg"]');
  await page.waitForSelector('svg.graph .gnode');
  if (shots) await page.screenshot({ path: path.join(shots, '05-cfg.png') });
  // strings + search
  await page.fill('#search', 'Access');
  await page.keyboard.press('Enter');
  await page.waitForSelector('#tbl-box td:has-text("Access granted")');
  // save project
  await page.keyboard.press('Control+s');
  await page.waitForSelector('#toast:not([hidden])');
  const toast = await page.textContent('#toast');
  if (!toast.includes('.rvproj')) fail('save toast: ' + toast);
  // undo the rename
  await page.click('#btn-undo');
  await page.waitForSelector('.frow .nm:text("check_user")', { timeout: 5000 });
  // theme toggle works
  await page.click('#btn-theme');
  if (shots) { await page.click('button[data-tab="pseudocode"]'); await page.waitForSelector('#pseudo'); await page.screenshot({ path: path.join(shots, '06-dark.png') }); }
  if (errors.length) fail('page errors: ' + errors.join('\n'));
  ok = true;
  console.log('UI smoke test: OK');
} finally {
  await browser.close();
  srv.kill();
  fs.rmSync(home, { recursive: true, force: true });
}
process.exit(ok ? 0 : 1);
