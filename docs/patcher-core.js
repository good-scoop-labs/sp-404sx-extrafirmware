/* SP-404SX/A EXTRAfirmware patcher core (browser and Node).
   Same steps as patch.py / src/tools/sp404fw.py:
     1. check the input is the official SP-404SX System Update 1.07 (SHA-256);
     2. unpack its LZSS stream into the program image;
     3. write the patch runs (only the bytes this project changes);
     4. re-pack with the minimal-change packer: Roland's own tokens are kept, only tokens that cover or copy
        from a changed byte become literals, then the length, checksum and tag are rebuilt;
     5. check the result against the release SHA-256.
   Container: u32be L | u32be unpacked size | LZSS | zero pad (L multiple of 16) | u16be sum(file[4..L-2)) | tag.
   LZSS: N=4096, F=18, threshold 2, ring starts zeroed at N-F, flag bits LSB first, 1 = literal. */
(function (root) {
  'use strict';
  var N = 4096, F = 18, TH = 2;

  function be32(b, o) { return ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0; }

  /* Decode into the image and a token list (typed arrays: kind 0 = literal, 1 = copy). */
  function decode(f) {
    var L = be32(f, 0), size = be32(f, 4), end = L - 2;
    if (L < 16 || L > f.length || size > 0x400000) throw new Error('not an SP-404SX update file');
    var img = new Uint8Array(size), ring = new Uint8Array(N), r = N - F, out = 0, pos = 8;
    var kind = [], ri = [], len = [], at = [];
    while (out < size) {
      if (pos >= end) throw new Error('truncated update file');
      var flags = f[pos++];
      for (var b = 0; b < 8 && out < size; b++) {
        if ((flags >> b) & 1) {
          var c = f[pos++];
          kind.push(0); ri.push(c); len.push(1); at.push(out);
          img[out++] = c; ring[r] = c; r = (r + 1) & (N - 1);
        } else {
          var i = f[pos] | ((f[pos + 1] & 0xf0) << 4), j = (f[pos + 1] & 15) + TH + 1;
          pos += 2;
          kind.push(1); ri.push(i); len.push(j); at.push(out);
          for (var k = 0; k < j; k++) {
            c = ring[(i + k) & (N - 1)];
            if (out < size) img[out] = c;
            out++; ring[r] = c; r = (r + 1) & (N - 1);
          }
        }
      }
    }
    return { img: img, tag: f.subarray(L), kind: kind, ri: ri, len: len, at: at };
  }

  function splice(d, newImg) {
    var n = newImg.length, changed = new Uint8Array(n), i, k;
    for (i = 0; i < n; i++) changed[i] = d.img[i] !== newImg[i] ? 1 : 0;
    var body = [], grp = [], t;
    function flush() {
      var flags = 0, payload = [];
      for (var b = 0; b < grp.length; b++) {
        var g = grp[b];
        if (g[0] === 0) { flags |= 1 << b; payload.push(g[1]); }
        else payload.push(g[1] & 0xff, ((g[1] >> 4) & 0xf0) | (g[2] - TH - 1));
      }
      body.push(flags);
      for (b = 0; b < payload.length; b++) body.push(payload[b]);
      grp = [];
    }
    function emit(tok) { grp.push(tok); if (grp.length === 8) flush(); }
    for (t = 0; t < d.kind.length; t++) {
      var out = d.at[t];
      if (d.kind[t] === 0) { emit([0, newImg[out]]); continue; }
      var ii = d.ri[t], j = d.len[t], hit = false;
      var p = out - ((((N - F + out) - ii) - 1) & (N - 1)) - 1;   /* first source byte of the copy */
      for (k = 0; k < j && !hit; k++) {
        if (out + k < n && changed[out + k]) hit = true;
        if (p + k >= 0 && p + k < n && changed[p + k]) hit = true;
      }
      if (hit) { for (k = 0; k < j; k++) emit([0, newImg[out + k]]); }
      else emit([1, ii, j]);
    }
    if (grp.length) flush();
    var L = (8 + body.length + 2 + 15) & ~15;
    var f = new Uint8Array(L + d.tag.length);
    f[0] = L >>> 24; f[1] = (L >> 16) & 255; f[2] = (L >> 8) & 255; f[3] = L & 255;
    f[4] = n >>> 24; f[5] = (n >> 16) & 255; f[6] = (n >> 8) & 255; f[7] = n & 255;
    f.set(body, 8);
    var sum = 0;
    for (i = 4; i < L - 2; i++) sum = (sum + f[i]) & 0xffff;
    f[L - 2] = sum >> 8; f[L - 1] = sum & 255;
    f.set(d.tag, L);
    return f;
  }

  function b64(s) {
    if (typeof atob === 'function') {
      var bin = atob(s), u = new Uint8Array(bin.length);
      for (var i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
      return u;
    }
    return new Uint8Array(Buffer.from(s, 'base64'));
  }

  /* SHA-256 in plain JS (used when WebCrypto is unavailable, e.g. the page opened from disk). */
  function sha256js(m) {
    var K = [0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2];
    var H = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
    var l = m.length, nb = ((l + 9 + 63) >> 6) << 6, p = new Uint8Array(nb), w = new Int32Array(64), i, j;
    p.set(m); p[l] = 0x80;
    var bits = l * 8;
    p[nb - 4] = (bits >>> 24) & 255; p[nb - 3] = (bits >>> 16) & 255; p[nb - 2] = (bits >>> 8) & 255; p[nb - 1] = bits & 255;
    p[nb - 5] = Math.floor(l / 0x20000000) & 255;
    function rr(x, n) { return (x >>> n) | (x << (32 - n)); }
    for (i = 0; i < nb; i += 64) {
      for (j = 0; j < 16; j++) w[j] = (p[i + 4 * j] << 24) | (p[i + 4 * j + 1] << 16) | (p[i + 4 * j + 2] << 8) | p[i + 4 * j + 3];
      for (j = 16; j < 64; j++) {
        var s0 = rr(w[j - 15], 7) ^ rr(w[j - 15], 18) ^ (w[j - 15] >>> 3);
        var s1 = rr(w[j - 2], 17) ^ rr(w[j - 2], 19) ^ (w[j - 2] >>> 10);
        w[j] = (w[j - 16] + s0 + w[j - 7] + s1) | 0;
      }
      var a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
      for (j = 0; j < 64; j++) {
        var t1 = (h + (rr(e, 6) ^ rr(e, 11) ^ rr(e, 25)) + ((e & f) ^ (~e & g)) + K[j] + w[j]) | 0;
        var t2 = ((rr(a, 2) ^ rr(a, 13) ^ rr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c))) | 0;
        h = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
      }
      H[0] = (H[0] + a) | 0; H[1] = (H[1] + b) | 0; H[2] = (H[2] + c) | 0; H[3] = (H[3] + d) | 0;
      H[4] = (H[4] + e) | 0; H[5] = (H[5] + f) | 0; H[6] = (H[6] + g) | 0; H[7] = (H[7] + h) | 0;
    }
    return H.map(function (x) { return ('00000000' + (x >>> 0).toString(16)).slice(-8); }).join('');
  }

  function sha256(bytes) {
    var subtle = typeof crypto !== 'undefined' && crypto.subtle;
    if (subtle) {
      return subtle.digest('SHA-256', bytes).then(function (d) {
        return Array.prototype.map.call(new Uint8Array(d), function (x) { return ('0' + x.toString(16)).slice(-2); }).join('');
      }, function () { return sha256js(bytes); });
    }
    return Promise.resolve(sha256js(bytes));
  }

  /* Resolves to {ok: true, bytes} or {ok: false, code: 'patched' | 'wrong' | 'error', message}. */
  function apply(file, data) {
    return sha256(file).then(function (h) {
      if (h === data.output.sha256)
        return { ok: false, code: 'patched', message: 'This file is already ' + data.name + ' v' + data.release + '. You can use it as it is.' };
      if (h !== data.input.sha256)
        return { ok: false, code: 'wrong', message: 'This is not the official SP-404SX System Update 1.07 file. Use sp404sx_up.bin from Roland’s download, unchanged.' };
      var d = decode(file);
      if (d.img.length !== data.image.size) return { ok: false, code: 'error', message: 'Unexpected program size.' };
      var img = new Uint8Array(d.img);
      data.runs.forEach(function (r) { img.set(b64(r[1]), r[0]); });
      var out = splice(d, img);
      return sha256(out).then(function (h2) {
        if (h2 !== data.output.sha256)
          return { ok: false, code: 'error', message: 'Internal check failed: the result does not match the release. Nothing was created.' };
        return { ok: true, bytes: out, sha256: h2 };
      });
    }).catch(function (e) { return { ok: false, code: 'error', message: 'Could not read this file: ' + e.message }; });
  }

  var api = { apply: apply, decode: decode, splice: splice, sha256: sha256, sha256js: sha256js };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.ExtraPatcher = api;
})(this);
