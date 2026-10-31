#!/usr/bin/env node
/* Optional end-to-end test of the web patcher page in a real browser (needs `npm install playwright` and
   `npx playwright install chromium`). Serves docs/ locally, drops a wrong file, the official file and an
   already-patched file onto the page in light and dark mode, downloads the result and compares its SHA-256
   with the release; checks there is no horizontal scrolling at phone width.

   usage: node src/tools/check_web_page.js official/sp404sx_up.bin [screenshot-folder] */
'use strict';
const http = require('http'), fs = require('fs'), path = require('path'), os = require('os'), crypto = require('crypto');
const { chromium } = require('playwright');
const docs = path.join(__dirname, '..', '..', 'docs');
const data = require(path.join(docs, 'extrafirmware-patch.js'));
const [official, shots] = process.argv.slice(2);
if (!official) { console.error('usage: node src/tools/check_web_page.js official/sp404sx_up.bin [screenshot-folder]'); process.exit(2); }
const types = { '.html': 'text/html', '.js': 'text/javascript' };
const server = http.createServer((req, res) => {
  const f = path.join(docs, path.normalize(decodeURIComponent(req.url.split('?')[0])).replace(/^([/\\])+/, '') || 'index.html');
  fs.readFile(f, (e, b) => { if (e) { res.writeHead(404); res.end(); } else { res.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream' }); res.end(b); } });
});
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
(async () => {
  await new Promise((r) => server.listen(0, r));
  const url = 'http://localhost:' + server.address().port + '/index.html';
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'xfw-'));
  const junk = path.join(tmp, 'junk.bin'); fs.writeFileSync(junk, 'not a firmware file');
  const browser = await chromium.launch();
  let fail = 0;
  const check = (ok, what) => { console.log((ok ? 'ok    ' : 'FAIL  ') + what); if (!ok) fail++; };
  for (const scheme of ['light', 'dark']) {
    const page = await browser.newPage({ colorScheme: scheme, acceptDownloads: true });
    const errors = []; page.on('pageerror', (e) => errors.push(e.message));
    await page.goto(url);
    const drop = async (f) => {
      await page.setInputFiles('#file', f);
      await page.waitForFunction(() => /ok|err/.test(document.getElementById('status').className));
      return [await page.getAttribute('#status', 'class'), await page.isDisabled('#dl')];
    };
    let [cls, dis] = await drop(junk); check(cls === 'err' && dis, scheme + ': wrong file refused');
    [cls, dis] = await drop(official); check(cls === 'ok' && !dis, scheme + ': official file accepted');
    const [dl] = await Promise.all([page.waitForEvent('download'), page.click('#dl')]);
    const out = path.join(tmp, scheme + '.bin'); await dl.saveAs(out);
    check(dl.suggestedFilename() === 'sp404sx_up.bin', scheme + ': download named sp404sx_up.bin');
    check(sha(fs.readFileSync(out)) === data.output.sha256, scheme + ': download matches release v' + data.release);
    [cls, dis] = await drop(out); check(cls === 'err' && dis, scheme + ': patched file recognised');
    await page.setViewportSize({ width: 375, height: 800 });
    check(!(await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)), scheme + ': no horizontal scroll at phone width');
    if (shots) await page.screenshot({ path: path.join(shots, scheme + '_phone.png'), fullPage: true });
    check(errors.length === 0, scheme + ': no page errors ' + errors.join('; '));
    await page.close();
  }
  await browser.close(); server.close();
  process.exit(fail ? 1 : 0);
})();
