#!/usr/bin/env python3
"""Find functions in the stock image that nothing references: no bsr/bra/bt/bf into them, no 32-bit
pointer to anywhere inside them, no PC-relative constant load into them from outside.
Research tool used to find reusable code space; candidates still need checking by hand.

usage: python3 src/tools/deadcode.py official/sp404sx_up.bin
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sh
if len(sys.argv) != 2:
    sys.exit(__doc__)
FW = sh.load(sys.argv[1])
BASE = sh.BASE
N = len(FW)
def h(i): return (FW[i] << 8) | FW[i+1]
# literal pool words
pools = set()
for i in range(0, N-1, 2):
    op = h(i); a = BASE+i
    if op >> 12 == 0xD: t = (a & ~3)+4+(op & 0xFF)*4; pools.update((t, t+2))
    elif op >> 12 == 0x9: pools.add(a+4+(op & 0xFF)*2)
# function starts: 'sts.l pr,@-r15' or 'mov.l rN,@-r15' run, preceded by rts-delay / nop / pool
starts = []
i = 0x900
while i < 0xB2000:
    a = BASE+i; op = h(i)
    if a not in pools and (op == 0x4F22 or (op & 0xF0FF) == 0x2F06 and (op >> 4 & 0xF) >= 8):
        prev = h(i-2); prev2 = h(i-4)
        if prev in (0x0009, 0x006B, 0x000B) or prev2 in (0x000B, 0x006B) or (a-2) in pools:
            starts.append(a)
            # skip rest of this prologue
            while (h(i) & 0xF0FF) in (0x2F06,) or h(i) in (0x4F22, 0x4F12, 0x4F02, 0x4F13): i += 2
            continue
    i += 2
starts = sorted(set(starts))
# referenced addresses
refd = set()
for i in range(0, N-1, 2):
    a = BASE+i; op = h(i)
    if a in pools: continue
    if op >> 12 in (0xA, 0xB):
        d = op & 0xFFF; d -= 0x1000 if d & 0x800 else 0; refd.add(a+4+2*d)
    elif op >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
        d = op & 0xFF; d -= 0x100 if d & 0x80 else 0; refd.add(a+4+2*d)
ptrs = set(struct.unpack('>I', FW[i:i+4])[0] for i in range(0, N-3, 2))
loads = []
for i in range(0, N-1, 2):
    a = BASE+i; op = h(i)
    if a in pools: continue
    if op >> 12 == 0xD or op >> 8 == 0xC7: loads.append((a, (a & ~3)+4+(op & 0xFF)*4))
    elif op >> 12 == 0x9: loads.append((a, a+4+(op & 0xFF)*2))
dead = []
for k, s in enumerate(starts):
    e = starts[k+1] if k+1 < len(starts) else s
    if e - s < 64 or e - s > 0x4000: continue
    if s in refd or any(s <= p < e for p in ptrs if 0x0C080000 <= p < 0x0C140000): continue
    if any(s <= t < e and not s <= a < e for a, t in loads): continue
    dead.append((s, e))
# filter: no branch from outside into [s,e)
def br_into(s, e):
    for i in range(0x800, N-1, 2):
        a = BASE+i
        if s <= a < e or a in pools: continue
        op = h(i)
        if op >> 12 in (0xA, 0xB):
            d = op & 0xFFF; d -= 0x1000 if d & 0x800 else 0; t = a+4+2*d
        elif op >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
            d = op & 0xFF; d -= 0x100 if d & 0x80 else 0; t = a+4+2*d
        else: continue
        if s <= t < e: return True
    return False
dead = [(s, e) for s, e in dead if not br_into(s, e)]
tot = sum(e-s for s, e in dead)
print(len(starts), 'function starts;', len(dead), 'unreferenced candidates,', tot, 'bytes')
for s, e in sorted(dead, key=lambda x: x[0]-x[1])[:25]:
    print(hex(s), hex(e), e-s)

print('\n--- strict re-check ---')
def refs_any(s, e):
    if any(s <= p < e for p in ptrs): return 'pointer'
    if any(s <= t < e and not s <= a < e for a, t in loads): return 'const-load'
    if br_into(s, e): return 'branch-in'
    return None
good = []
for s, e in dead:
    s2 = s
    while (h(s2-2-BASE) & 0xF0FF) == 0x2F06 or h(s2-2-BASE) in (0x4F22, 0x4F12, 0x4F02, 0x4F13): s2 -= 2
    s2 -= 4   # margin
    why = refs_any(s2, e)
    # find last rts before e
    last = None
    for a in range(e-2, s, -2):
        if h(a-BASE) in (0x000B, 0x006B): last = a; break
    print(hex(s2+4), hex(e), e-s2-4, 'REFERENCED:'+why if why else 'clean', 'ends rts@'+hex(last) if last else 'no rts')
    if not why and last: good.append((s2+4, e))
print('clean total', sum(e-s for s, e in good))
