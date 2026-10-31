#!/usr/bin/env python3
"""SP-404SX/A EXTRAfirmware patcher.

Turns the official Roland SP-404SX System Update 1.07 file (sp404sx_up.bin) into the EXTRAfirmware
update file. Python 3.8+ only, no packages needed. The web patcher (docs/index.html) does exactly the same
thing in the browser and gives a byte-identical file.

usage: python3 patch.py path/to/official/sp404sx_up.bin [-o out/sp404sx_up.bin]

The input must be the official 1.07 file (checked by SHA-256); the output is checked against the
release's expected SHA-256 before it is written.
"""
import argparse, base64, hashlib, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'src', 'tools'))
import sp404fw

DATA = os.path.join(HERE, 'docs', 'extrafirmware-patch.js')


def load_data():
    s = open(DATA).read()
    return json.loads(s[s.index('{'):s.rindex('}') + 1])


def patch(official, data):
    """Return the EXTRAfirmware update file for the official file, or raise ValueError."""
    sha = hashlib.sha256(official).hexdigest()
    if sha == data['output']['sha256']:
        raise ValueError(f'this file is already {data["name"]} v{data["release"]}; use it as it is')
    if sha != data['input']['sha256']:
        raise ValueError('this is not the official SP-404SX System Update 1.07 sp404sx_up.bin '
                         '(download it from Roland and use the file inside the zip unchanged)')
    img, _ = sp404fw.unpack(official)
    if len(img) != data['image']['size']:
        raise ValueError('unexpected program size')
    img = bytearray(img)
    for off, b64 in data['runs']:
        b = base64.b64decode(b64)
        img[off:off + len(b)] = b
    out, _ = sp404fw.splice(official, bytes(img))
    if hashlib.sha256(out).hexdigest() != data['output']['sha256']:
        raise ValueError('internal error: the result does not match the release (nothing written)')
    return out


def main():
    ap = argparse.ArgumentParser(description='SP-404SX/A EXTRAfirmware patcher')
    ap.add_argument('official', help='official SP-404SX System Update 1.07 sp404sx_up.bin')
    ap.add_argument('-o', '--output', default=os.path.join('out', 'sp404sx_up.bin'))
    a = ap.parse_args()
    data = load_data()
    try:
        out = patch(open(a.official, 'rb').read(), data)
    except (OSError, ValueError) as e:
        sys.exit(f'error: {e}')
    if os.path.basename(a.output) != 'sp404sx_up.bin':
        print('note: the SP-404SX/A only installs a file named sp404sx_up.bin')
    os.makedirs(os.path.dirname(a.output) or '.', exist_ok=True)
    open(a.output, 'wb').write(out)
    print(f'{data["name"]} v{data["release"]} written to {a.output}')
    print(f'SHA-256 {data["output"]["sha256"]}')
    print('Copy it to the root of an SD card formatted by the SP-404SX/A and power on (see docs/INSTALL.md).')


if __name__ == '__main__':
    main()
