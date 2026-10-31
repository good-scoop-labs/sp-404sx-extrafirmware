#!/usr/bin/env python3
"""SH-2A disassembly helpers for the SP-404SX system image (research tool, needs `pip install capstone`).

usage:
  python3 src/tools/sh.py official/sp404sx_up.bin dis   0C0CB5D4 40   # disassemble 40 instructions
  python3 src/tools/sh.py official/sp404sx_up.bin xref  0C082B34      # pc-relative loads of a constant
  python3 src/tools/sh.py official/sp404sx_up.bin calls 0C0CB5D4      # bsr/bra and literal references

As a module: load(path) then dis(addr, n), xref_const(value), calls_to(addr), funcstart(addr), u16/u32.
The update container is unpacked first (src/tools/sp404fw.py); addresses are run-time addresses
(the image is loaded at 0x0C080000).
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sp404fw

BASE = 0x0C080000
FW = b''
_md = None


def load(path):
    """Load an sp404sx_up.bin (or an already unpacked image) as FW."""
    global FW
    data = open(path, 'rb').read()
    FW = sp404fw.unpack(data)[0] if data[4:8] == b'\x00\x0b\x8f\xf4' else data
    return FW


def _cs():
    global _md
    if _md is None:
        import capstone as C
        _md = C.Cs(C.CS_ARCH_SH, C.CS_MODE_BIG_ENDIAN | C.CS_MODE_SH2A | C.CS_MODE_SHFPU)
    return _md


def u32(a): o = a - BASE; return struct.unpack('>I', FW[o:o + 4])[0]
def u16(a): o = a - BASE; return struct.unpack('>H', FW[o:o + 2])[0]


def lit(a):
    """(address, value) loaded by a mov.l / mov.w @(disp,PC) at address a, else None."""
    op = u16(a)
    if op >> 12 == 0xD:
        ea = (a & ~3) + 4 + (op & 0xFF) * 4
        return ea, u32(ea)
    if op >> 12 == 0x9:
        ea = a + 4 + (op & 0xFF) * 2
        v = u16(ea)
        return ea, (v - 0x10000 if v & 0x8000 else v)
    return None


def dis(a, n):
    out, end = [], a + 2 * n
    while a < end:
        o = a - BASE
        ins = next(_cs().disasm(FW[o:o + 4], a), None)
        if ins is None:
            out.append(f'{a:08x}: {u16(a):04x}      .word 0x{u16(a):04x}')
            a += 2
            continue
        s = f'{ins.mnemonic} {ins.op_str}'
        L = lit(a) if ins.size == 2 else None
        if L:
            s += f'    ; =0x{L[1] & 0xffffffff:08x}'
        out.append(f'{a:08x}: {FW[o:o + ins.size].hex():8s}  {s}')
        a += ins.size
    return '\n'.join(out)


def xref_const(v):
    """(pool addresses holding v, addresses of mov.l @(disp,PC) instructions that load them)."""
    pools = [BASE + i for i in range(0, len(FW) - 3, 4) if struct.unpack('>I', FW[i:i + 4])[0] == v]
    ps, res = set(pools), []
    for i in range(0, len(FW) - 1, 2):
        op = struct.unpack('>H', FW[i:i + 2])[0]
        if op >> 12 == 0xD:
            a = BASE + i
            if (a & ~3) + 4 + (op & 0xFF) * 4 in ps:
                res.append(a)
    return pools, res


def calls_to(t):
    res = []
    for i in range(0, len(FW) - 1, 2):
        a = BASE + i
        op = struct.unpack('>H', FW[i:i + 2])[0]
        if op >> 12 in (0xA, 0xB):
            d = op & 0xFFF
            d = d - 0x1000 if d & 0x800 else d
            if a + 4 + d * 2 == t:
                res.append(('bsr' if op >> 12 == 0xB else 'bra', hex(a)))
    res += [('lit', hex(x)) for x in xref_const(t)[1]]
    return res


def funcstart(a):
    """Heuristic: walk back to the nearest function prologue."""
    while a > BASE:
        a -= 2
        if u16(a) == 0x4F22 or (u16(a) & 0xF0FF) == 0x2F06 and u16(a - 2) in (0x0009, 0x000B):
            while (u16(a - 2) & 0xFF0F) == 0x2F06:
                a -= 2
            if u16(a - 2) in (0x0009, 0x000B) or u16(a - 4) == 0x000B:
                return a
    return a


if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    load(sys.argv[1])
    cmd, args = sys.argv[2], [int(x, 16) for x in sys.argv[3:]]
    if cmd == 'dis':
        print(dis(args[0], args[1] if len(args) > 1 else 32))
    elif cmd == 'xref':
        p, r = xref_const(args[0])
        print('pools', [hex(x) for x in p]); print('loads', [hex(x) for x in r])
    elif cmd == 'calls':
        print(calls_to(args[0]))
    else:
        sys.exit(__doc__)
