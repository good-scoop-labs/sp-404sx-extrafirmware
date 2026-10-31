#!/usr/bin/env node
/* Check the web patcher without a browser: runs docs/patcher-core.js with docs/extrafirmware-patch.js on the
   official file (the same code the page runs) and compares the result with the release hash and, if given,
   with a file made by patch.py or src/tools/build.py. Also checks the plain-JS SHA-256 fallback.

   usage: node src/tools/check_web_patch.js official/sp404sx_up.bin [out/sp404sx_up.bin] */
'use strict';
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const docs = path.join(__dirname, '..', '..', 'docs');
const core = require(path.join(docs, 'patcher-core.js'));
const data = require(path.join(docs, 'extrafirmware-patch.js'));
const [official, reference] = process.argv.slice(2);
if (!official) { console.error('usage: node src/tools/check_web_patch.js official/sp404sx_up.bin [out/sp404sx_up.bin]'); process.exit(2); }
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
(async () => {
  let fail = 0;
  const check = (ok, what) => { console.log((ok ? 'ok    ' : 'FAIL  ') + what); if (!ok) fail++; };
  const input = new Uint8Array(fs.readFileSync(official));
  check(core.sha256js(input) === sha(input), 'plain-JS SHA-256 matches Node on the official file');
  check(core.sha256js(new Uint8Array(0)) === sha(Buffer.alloc(0)), 'plain-JS SHA-256 of an empty input');
  const r = await core.apply(input, data);
  check(r.ok, 'official file accepted: ' + (r.ok ? 'yes' : r.message));
  if (r.ok) {
    check(sha(r.bytes) === data.output.sha256, 'result matches release ' + data.release + ' SHA-256');
    check(r.bytes.length === data.output.size, 'result size ' + r.bytes.length);
    if (reference) check(Buffer.compare(Buffer.from(r.bytes), fs.readFileSync(reference)) === 0, 'byte-identical to ' + reference);
    const again = await core.apply(r.bytes, data);
    check(!again.ok && again.code === 'patched', 'a patched file is recognised as already patched');
  }
  const other = new Uint8Array(input); other[1000] ^= 1;
  const bad = await core.apply(other, data);
  check(!bad.ok && bad.code === 'wrong', 'a modified input is refused');
  process.exit(fail ? 1 : 0);
})();
