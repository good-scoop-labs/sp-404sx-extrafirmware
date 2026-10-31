"""SP-404SX/A EXTRAfirmware: release build.

Compiles the firmware code in src/firmware (SH cross toolchain, see docs/DEVELOPMENT.md), places it into the
official SP-404SX system 1.07 image and writes a new sp404sx_up.bin. Every patch site is checked against the
stock bytes first, and the result is checked before it is written:
  * the instructions we generate are all valid SH-2A (allow-list);
  * nothing in Roland's code branches into, points into or loads constants from the areas we reuse;
  * every changed byte is inside a planned patch area;
  * the update container re-decodes to the new image and its checksum and tag are valid.

Patches on top of stock 1.07 (addresses in the unpacked image, loaded at 0x0C080000):
  * free 1 KB at 0x0C080400 (above vector 255): entry stubs, UI code, RAM (per-pad speed, per-voice state)
  * reclaimed hidden PAD_INFO debug dump 0x0C089EF4..0x0C08A35F: resampler + fill wrapper; its two callers'
    literals (0x0C0CFE48 / 0x0C0C26FC) -> a `return 1` stub; two literals of the PAD_INFO save routine that
    lived there are moved into the dead filename helper 0x0C089E9C
  * unreferenced OS routine 0x0C0EB5A0 (persistence), unreferenced effects method 0x0C0A9558 (ratio table, path)
  * factory test mode disabled at boot (0x0C0D1852); methods of CPageTestDevice / CPageTestMidi / CPageTestLED
    reused for code, their vtable slots pointed at the same page's `return 0` method
  * CPageSmplEdit (TIME/BPM page): event vtable slot 0x0C12F414 -> ev_stub; 12-byte trampoline at 0x0C0CB5D4 ->
    disp_stub; light (0x0C0CBF0C) and refresh (0x0C0CC0E6) mode ranges extended to the CTRL 3 view
  * fill() slot of all 10 CSmplCharge vtables -> fill_fwd / fill_rev (wrapping Roland's 0x0C0914B8 / 0x0C0921FC)
  * version screen text 1.07 -> 2.00 (0x0C0F1142, 0x0C0F116C)
  * PAD_INFO load pointer 0x0C093D78 and 100 ms auto-save pointer 0x0C0EF4E8 -> load_stub / tick_stub

usage: python3 src/tools/build.py official/sp404sx_up.bin [-o out/sp404sx_up.bin]
"""
import argparse, hashlib, os, struct, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sp404fw
from sha import movl_pc, jmp, nop, long

OFFICIAL_MD5 = '96a4e2622738acd48cbbbfc667ebcfe5'
BASE = 0x0C080000
CAVE, CAVE_END = 0x0C080400, 0x0C080800
DUMP, DUMP_END = 0x0C089EF4, 0x0C08A360
SRC = os.path.join(HERE, '..', 'firmware')
VT_SLOT, VT_OLD = 0x0C12F414, 0x0C0CC078
DISP = 0x0C0CB5D4
DISP_OLD = bytes.fromhex('4f22 2fa6 2f96 6a43 2f86 e407')
DUMP_LITS = (0x0C0CFE48, 0x0C0C26FC)
FILL_FWD, FILL_REV = 0x0C0914B8, 0x0C0921FC
# The PAD_INFO save routine (0x0C089DE4, 11 callers: copy, exchange, delete, ...) keeps two constants in the
# literal pool inside the reclaimed area. Move them into the dead filename helper (0x0C089E9C, only called by
# the removed debug dump) and retarget its two loads. (site, old insn, new insn, old literal addr, new literal addr)
HELPER, HELPER_END = 0x0C089E9C, 0x0C089EF4      # dead filename helper: 8 B relocated literals + 80 B code
HELPER_CODE = 0x0C089EA4
OSFN, OSFN_END = 0x0C0EB5A0, 0x0C0EB758          # unreferenced OS service routine (440 B)
WSPFN, WSPFN_END = 0x0C0A9558, 0x0C0A9604        # unreferenced effects method (172 B)
LOAD_PTR, LOAD_FN = 0x0C093D78, 0x0C08A68C       # pointer used to call Roland's PAD_INFO load
TICK_PTR, TICK_FN = 0x0C0EF4E8, 0x0C08A700       # pointer used to call Roland's 100 ms PAD_INFO auto-save
LIT_MOVES = ((0x0C089E80, 0xD3AE, 0xD306, 0x0C08A13C, 0x0C089E9C),
             (0x0C089E82, 0xD4AF, 0xD407, 0x0C08A140, 0x0C089EA0))
FILL_SLOTS_FWD = (0x0C0F5F44, 0x0C0F5FD0, 0x0C0F60B0, 0x0C0F6190, 0x0C0F6270)
FILL_SLOTS_REV = (0x0C0F6040, 0x0C0F6120, 0x0C0F6200, 0x0C0F62E0, 0x0C0F64C0)
# Factory test mode. Boot runs: kind = detect test file / key combo (0x0C0BFDE0); if kind: enter test pages.
# Gate: 0x0C0D1852 `mov r0,r3` -> `mov #0,r3` (the normal-boot value), so the test pages are never entered.
TEST_GATE, TEST_GATE_OLD, TEST_GATE_NEW = 0x0C0D1852, bytes.fromhex('6483 6303 2338 890a'), 0xE300
# CPageTestDevice method 5 (0x0C0CF568..0x0C0CF7B3) is only reachable through its vtable slot. Its first part
# keeps the page's shared literal pool (0x0C0CF5D0..0x0C0CF61F), so only 0x0C0CF620.. is reused. The slot
# is pointed at the page's own `return 0` method (0x0C0CF7B8) in case the page were ever shown.
DEV_METHOD, DEV, DEV_END = 0x0C0CF568, 0x0C0CF620, 0x0C0CF7B4
DEV_SLOT, DEV_RET0 = 0x0C130318, 0x0C0CF7B8
# TIME/BPM LED: `mov #6,r4` (mode-1 <= 6, i.e. modes 5..7 light LED 22) -> `mov #7,r4` (modes 5..8)
LED_RANGE, LED_RANGE_OLD, LED_RANGE_NEW = 0x0C0CBF0C, bytes.fromhex('e406 3346 8b09'), 0xE407
# Page refresh (vtable method 5): `mov #2,r4; add #-5,r3; cmp/hi` (modes 5..7) -> `mov #3,r4` (modes 5..8)
REFRESH_RANGE, REFRESH_OLD, REFRESH_NEW = 0x0C0CC0E6, bytes.fromhex('e402 73fb 3346 890e'), 0xE403
# Version screen (hold MARK at power-on): Roland's text routine 0x0C0F112C turns the version number 107 into
# digits by adding '0'. The number itself (0x0C0F1128, also read by the removed test pages) stays 107; only
# the text changes: last digit 7 + 0x29 = '0', first digit 1 + 0x31 = '2', so the screen shows 2.00.
VERSION_TEXT = ((0x0C0F1142, 0x7030, 0x7029),   # add #48,r0 -> add #41,r0 (ones digit: '7' -> '0')
                (0x0C0F116C, 0x7030, 0x7031))   # add #48,r0 -> add #49,r0 (hundreds digit: '1' -> '2')
# More test page methods with no literal pool used from outside, no branch in, one vtable pointer:
# (section, start, end, vtable slot, the same page's `return 0` method)
EXTRA = (('midi', 0x0C0CDDDC, 0x0C0CDEBC, 0x0C12FD24, 0x0C0CE074),     # CPageTestMidi method 2
         ('led',  0x0C0CD838, 0x0C0CD8FC, 0x0C12F9A4, 0x0C0CD900))     # CPageTestLED method 5
CF = '-m4-nofpu -mb -Os -ffreestanding -fno-pic -fno-builtin -nostdlib -fno-asynchronous-unwind-tables -fno-jump-tables'.split()

DENY = {'pref', 'movca.l', 'ocbi', 'ocbp', 'ocbwb', 'ldtlb', 'icbi', 'prefi', 'synco', 'movli.l', 'movco.l',
        'fschg', 'frchg', 'fsca', 'fsrra', 'ftrv', 'fipr', 'clrs', 'sets', 'movua.l'}
ALLOW = {'mov', 'mov.l', 'mov.w', 'mov.b', 'sts.l', 'lds.l', 'sts', 'lds', 'jsr', 'jmp', 'rts', 'nop', 'bf', 'bt',
         'bf.s', 'bt.s', 'bf/s', 'bt/s', 'bra', 'bsr', 'add', 'sub', 'cmp/hi', 'cmp/hs', 'cmp/eq', 'cmp/pz', 'cmp/pl',
         'cmp/gt', 'cmp/ge', 'tst', 'extu.w', 'extu.b', 'exts.w', 'exts.b', 'mul.l', 'muls.w', 'mulu.w', 'shad', 'shld',
         'shll', 'shlr', 'shll2', 'shlr2', 'shll8', 'shlr8', 'shll16', 'shlr16', 'shar', 'shal', 'and', 'or', 'xor',
         'neg', 'not', 'dt', 'movt', 'mova', 'addc', 'subc', 'negc', 'swap.w', 'swap.b', 'rotl', 'rotr', 'sett', 'clrt'}

def run(cmd, **kw):
    return subprocess.run(cmd, cwd=SRC, check=True, capture_output=True, text=True, **kw).stdout

def compile_all():
    run(['python3', 'gen_ratio.py'])
    for c in ('vari.c', 'ratio.c'):
        run(['sh4-linux-gnu-gcc', *CF, '-c', c, '-o', c[:-2] + '.o'])
    run(['sh4-linux-gnu-gcc', '-mb', '-c', 'stubs.S', '-o', 'stubs.o'])
    run(['sh4-linux-gnu-ld', '-EB', '--no-warn-rwx-segments', '-z', 'noexecstack', '-T', 'link.ld',
         '-Map', 'vari.map', '-o', 'vari.elf', 'stubs.o', 'vari.o', 'ratio.o'])
    run(['sh4-linux-gnu-objcopy', '-O', 'binary', '-j', '.entry', 'vari.elf', 'cave.bin'])
    run(['sh4-linux-gnu-objcopy', '-O', 'binary', '-j', '.text', 'vari.elf', 'dump.bin'])
    for sec in ('persist', 'wsp', 'helper', 'dev', 'midi', 'led'):
        run(['sh4-linux-gnu-objcopy', '-O', 'binary', '-j', '.' + sec, 'vari.elf', sec + '.bin'])
    syms = {}
    for line in run(['sh4-linux-gnu-nm', 'vari.elf']).splitlines():
        a, t, n = line.split()
        syms[n] = int(a, 16)
    assert not run(['sh4-linux-gnu-nm', '-u', 'vari.elf']).strip(), 'undefined symbols'
    dis = run(['sh4-linux-gnu-objdump', '-d', '-j', '.entry', '-j', '.text', '-j', '.persist', '-j', '.helper', '-j', '.dev', '-j', '.midi', '-j', '.led', 'vari.elf'])
    rd = lambda n: open(os.path.join(SRC, n), 'rb').read()
    return rd('cave.bin'), rd('dump.bin'), rd('persist.bin'), rd('wsp.bin'), rd('helper.bin'), rd('dev.bin'), {n: rd(n + '.bin') for n in ('midi', 'led')}, syms, dis

def check_instructions(dis, syms, cave_len):
    """Decoded instructions in code ranges must be on the SH-2A allow-list.
    Data: literal pools (targets of pc-relative loads), the stub literal block, and .rodata.cave."""
    pools = set()
    for line in dis.splitlines():
        p = line.split('\t')
        if len(p) >= 4 and p[2].split() and p[2].split()[0] in ('mov.l', 'mov.w'):
            try: t = int(p[3].split(',')[0].split()[0], 16)
            except ValueError: continue
            pools.update(range(t, t + (4 if p[2].split()[0] == 'mov.l' else 2), 2))
    data = [(syms['dump_ok'] + 4, syms['stub_lits_end']),                   # stub literals
            ((syms['tick_stub'] + 0x22 + 3) & ~3, syms['persist_lits_end']),  # persistence stub literals
            (syms['vari_need_load'], CAVE + cave_len),                     # .cavedata
            (WSPFN, WSPFN_END)]                                            # ratio table + path
    bad, seen = [], set()
    for line in dis.splitlines():
        p = line.split('\t')
        if len(p) < 3 or not p[0].strip().endswith(':') or not p[2].strip():
            continue
        a = int(p[0].strip()[:-1], 16)
        m = p[2].split()[0]
        if a in pools or any(lo <= a < hi for lo, hi in data) or m.startswith('.'):
            continue
        seen.add(m)
        if m in DENY or m not in ALLOW:
            bad.append((hex(a), m))
    return bad, seen

_POOLS = {}
def literal_pools(img):
    """Halfword addresses holding literal-pool data: targets of every mov.l/mov.w @(disp,PC) in the image."""
    key = id(img)
    if key not in _POOLS:
        pools = set()
        for i in range(0, len(img) - 1, 2):
            op = (img[i] << 8) | img[i + 1]
            a = BASE + i
            if op >> 12 == 0xD:
                t = (a & ~3) + 4 + (op & 0xFF) * 4; pools.update((t, t + 2))
            elif op >> 12 == 0x9:
                pools.add(a + 4 + (op & 0xFF) * 2)
        _POOLS[key] = pools
    return _POOLS[key]

def branches_into(img, lo, hi):
    """Branches (bra/bsr/bt/bf) from code outside [lo, hi) to inside it, ignoring literal-pool words."""
    hits, pools = [], literal_pools(img)
    for i in range(0x800, len(img) - 1, 2):
        a = BASE + i
        if lo <= a < hi or a in pools: continue
        op = struct.unpack('>H', img[i:i + 2])[0]
        if op >> 12 in (0xA, 0xB):
            d = op & 0xFFF; d -= 0x1000 if d & 0x800 else 0; t = a + 4 + 2 * d
        elif op >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
            d = op & 0xFF; d -= 0x100 if d & 0x80 else 0; t = a + 4 + 2 * d
        else:
            continue
        if lo < t < hi: hits.append((hex(a), hex(t)))
    return hits

def const_loads_into(img, lo, hi, skip):
    """Constant loads (mov.l/mov.w @(disp,PC), mova) from code outside `skip` ranges whose target is in [lo, hi)."""
    pools, hits = literal_pools(img), []
    for i in range(0x800, len(img) - 1, 2):
        a = BASE + i
        if a in pools or any(s0 <= a < s1 for s0, s1 in skip): continue
        op = (img[i] << 8) | img[i + 1]
        if op >> 12 == 0xD or op >> 8 == 0xC7: t = (a & ~3) + 4 + (op & 0xFF) * 4
        elif op >> 12 == 0x9: t = a + 4 + (op & 0xFF) * 2
        else: continue
        if lo <= t < hi: hits.append((a, t))
    return hits

def u32(buf, addr):
    return struct.unpack('>I', buf[addr - BASE:addr - BASE + 4])[0]

def put32(buf, addr, old, new):
    o = addr - BASE
    assert struct.unpack('>I', buf[o:o + 4])[0] == old, f'{addr:#x}: expected {old:#x}'
    buf[o:o + 4] = struct.pack('>I', new)

def build(official):
    if hashlib.md5(official).hexdigest() != OFFICIAL_MD5:
        sys.exit('input is not the official SP-404SX system 1.07 sp404sx_up.bin (MD5 mismatch)')
    cave, dump, persist, wsp, helper, dev, extra, syms, dis = compile_all()
    bad, seen = check_instructions(dis, syms, len(cave))
    assert not bad, f'non-SH-2A or unexpected instructions: {bad}'
    assert syms['_start'] == CAVE and CAVE + len(cave) <= min(syms['vari_vs'], syms['vari_snap']) and syms['vari_tab'] + 244 <= CAVE_END
    assert DUMP + len(dump) <= DUMP_END and OSFN + len(persist) <= OSFN_END
    assert WSPFN + len(wsp) <= WSPFN_END and HELPER_CODE + len(helper) <= HELPER_END
    assert DEV + len(dev) <= DEV_END and syms['base_bpm'] == DEV
    for n, lo, hi, slot, ret0 in EXTRA:
        assert lo + len(extra[n]) <= hi and extra[n]
    assert syms['vari_path'] >= WSPFN and syms['pad_fp'] == HELPER_CODE and syms['resync'] >= OSFN
    load_stub, tick_stub = syms['load_stub'], syms['tick_stub']
    disp_stub, ev_stub, fill_fwd, fill_rev, dump_ok = (syms[n] for n in ('disp_stub', 'ev_stub', 'fill_fwd', 'fill_rev', 'dump_ok'))

    img, tag = sp404fw.unpack(official)
    new = bytearray(img)
    co, do_ = CAVE - BASE, DUMP - BASE
    assert new[co:CAVE_END - BASE] == bytes(CAVE_END - CAVE), 'cave not empty'
    assert new[do_:do_ + 4] == bytes.fromhex('2fe64f22'), 'debug dump prologue not found'
    assert not branches_into(img, DUMP, DUMP_END), 'something branches into the reclaimed debug dump'
    assert not branches_into(img, HELPER, DUMP_END), 'something branches into the dead filename helper'
    for lo, hi in ((OSFN, OSFN_END), (WSPFN, WSPFN_END)):
        assert not branches_into(img, lo, hi), f'something branches into {lo:#x}'
        assert not const_loads_into(img, lo, hi, [(lo, hi)]), f'constant loads into {lo:#x}'
        ptr = [hex(BASE + i) for i in range(0, len(img) - 3, 4) if lo <= struct.unpack('>I', img[i:i + 4])[0] < hi]
        assert not ptr, f'pointers into {lo:#x}: {ptr}'
        assert img[lo - BASE:lo - BASE + 2] == b'\x4f\x22', f'{lo:#x} is not the expected function start'
    # factory test page method: no branch, constant load or pointer reaches it except its vtable slot
    assert not branches_into(img, DEV_METHOD, DEV_END), 'something branches into the test page method'
    assert not const_loads_into(img, DEV, DEV_END, [(DEV_METHOD, DEV_END)]), 'constant loads into the reused part'
    ptr = [BASE + i for i in range(0, len(img) - 3, 4) if DEV_METHOD <= struct.unpack('>I', img[i:i + 4])[0] < DEV_END]
    assert ptr == [DEV_SLOT], f'pointers into the test page method: {[hex(p) for p in ptr]}'
    assert img[DEV_METHOD - BASE - 4:DEV_METHOD - BASE + 2] == bytes.fromhex('006b 0009 4f22'), 'method boundary'
    assert img[DEV_END - BASE:DEV_END - BASE + 8] == bytes.fromhex('e004 006b e000 006b'), 'next methods'
    for n, lo, hi, slot, ret0 in EXTRA:
        assert not branches_into(img, lo, hi), f'something branches into {n}'
        assert not const_loads_into(img, lo, hi, [(lo, hi)]), f'constant loads into {n}'
        ptr = [BASE + i for i in range(0, len(img) - 3, 4) if lo <= struct.unpack('>I', img[i:i + 4])[0] < hi]
        assert ptr == [slot] and u32(img, slot) == lo, f'pointers into {n}: {[hex(p) for p in ptr]}'
        assert img[lo - BASE - 2:lo - BASE + 2] in (bytes.fromhex('0009 4f22'), bytes.fromhex('006b 4f22')), f'{n} start'
        assert img[hi - BASE - 2:hi - BASE] == bytes.fromhex('006b'), f'{n} end'
        assert img[ret0 - BASE:ret0 - BASE + 4] == bytes.fromhex('e000 006b'), f'{n} return-0 method'
    assert img[TEST_GATE - BASE - 2:TEST_GATE - BASE + 6] == TEST_GATE_OLD, 'test-mode gate not found'
    assert [i for i in range(0, len(img) - 3, 4) if struct.unpack('>I', img[i:i + 4])[0] == 0x0C0BFC40] == \
        [0x0C0D1978 - BASE], 'test-mode first page is reachable from elsewhere'
    for p, f in ((LOAD_PTR, LOAD_FN), (TICK_PTR, TICK_FN)):
        assert [i for i in range(0, len(img) - 3, 4) if struct.unpack('>I', img[i:i + 4])[0] == f] == [p - BASE], \
            f'{f:#x} is referenced from more than {p:#x}'
    # every constant load from live code into the reclaimed area must be one we relocate
    loads = const_loads_into(img, DUMP, DUMP_END, [(HELPER, DUMP_END)])
    expect = sorted((m[0], m[3]) for m in LIT_MOVES)
    assert sorted(loads) == expect, f'unexpected constant loads into the reclaimed area: {[(hex(a), hex(t)) for a, t in loads]}'
    for site, old_op, new_op, old_lit, new_lit in LIT_MOVES:
        so = site - BASE
        assert struct.unpack('>H', new[so:so + 2])[0] == old_op
        assert (site & ~3) + 4 + (new_op & 0xFF) * 4 == new_lit and (new_op & 0xFF00) == (old_op & 0xFF00)
        new[new_lit - BASE:new_lit - BASE + 4] = new[old_lit - BASE:old_lit - BASE + 4]
        new[so:so + 2] = struct.pack('>H', new_op)
    new[co:co + len(cave)] = cave
    new[do_:do_ + len(dump)] = dump
    new[OSFN - BASE:OSFN - BASE + len(persist)] = persist
    new[WSPFN - BASE:WSPFN - BASE + len(wsp)] = wsp
    new[HELPER_CODE - BASE:HELPER_CODE - BASE + len(helper)] = helper
    new[DEV - BASE:DEV - BASE + len(dev)] = dev
    put32(new, DEV_SLOT, DEV_METHOD, DEV_RET0)
    for n, lo, hi, slot, ret0 in EXTRA:
        new[lo - BASE:lo - BASE + len(extra[n])] = extra[n]
        put32(new, slot, lo, ret0)
    new[TEST_GATE - BASE:TEST_GATE - BASE + 2] = struct.pack('>H', TEST_GATE_NEW)
    assert img[LED_RANGE - BASE:LED_RANGE - BASE + 6] == LED_RANGE_OLD, 'TIME/BPM LED range check not found'
    new[LED_RANGE - BASE:LED_RANGE - BASE + 2] = struct.pack('>H', LED_RANGE_NEW)
    for a, old_op, new_op in VERSION_TEXT:
        assert struct.unpack('>H', img[a - BASE:a - BASE + 2])[0] == old_op, f'version text {a:#x}'
        new[a - BASE:a - BASE + 2] = struct.pack('>H', new_op)
    assert img[0x0C0F1128 - BASE:0x0C0F1128 - BASE + 2] == bytes.fromhex('e06b'), 'version number 107'
    assert img[REFRESH_RANGE - BASE - 2:REFRESH_RANGE - BASE + 8] == bytes.fromhex('5381') + REFRESH_OLD, 'refresh check'
    new[REFRESH_RANGE - BASE:REFRESH_RANGE - BASE + 2] = struct.pack('>H', REFRESH_NEW)
    put32(new, LOAD_PTR, LOAD_FN, load_stub)
    put32(new, TICK_PTR, TICK_FN, tick_stub)
    for lit in DUMP_LITS:
        put32(new, lit, DUMP, dump_ok)
    put32(new, VT_SLOT, VT_OLD, ev_stub)
    for s in FILL_SLOTS_FWD: put32(new, s, FILL_FWD, fill_fwd)
    for s in FILL_SLOTS_REV: put32(new, s, FILL_REV, fill_rev)
    # no pointer to the dump or fill routines left anywhere except our own stub literals
    for target in (DUMP, FILL_FWD, FILL_REV):
        left = [hex(BASE + i) for i in range(0, len(new) - 3, 4)
                if struct.unpack('>I', new[i:i + 4])[0] == target and not any(
                    lo - BASE <= i < hi - BASE for lo, hi in ((CAVE, CAVE_END), (DUMP, DUMP_END), (OSFN, OSFN_END),
                                                               (HELPER_CODE, HELPER_END)))]
        assert not left, f'{target:#x} still referenced at {left}'
    o = DISP - BASE
    assert new[o:o + 12] == DISP_OLD and not branches_into(img, DISP, DISP + 12)
    assert not const_loads_into(img, DISP, DISP + 12, []), 'constant loads into the display trampoline'
    assert not const_loads_into(img, CAVE, CAVE_END, []), 'constant loads into the free area'
    new[o:o + 12] = movl_pc(DISP, DISP + 8, 0) + jmp(0) + nop() + nop() + long(disp_stub)

    # after patching: no live code outside our areas loads a constant from inside them
    ours = [(CAVE, CAVE_END), (DUMP, DUMP_END), (HELPER, HELPER_END), (DISP, DISP + 12),
            (OSFN, OSFN_END), (WSPFN, WSPFN_END), (DEV_METHOD, DEV_END)] + [(lo, hi) for _, lo, hi, _, _ in EXTRA]
    for lo, hi in ((DUMP, DUMP_END), (CAVE, CAVE_END), (DISP, DISP + 12), (OSFN, OSFN_END), (WSPFN, WSPFN_END),
                   (HELPER_CODE, HELPER_END), (DEV, DEV_END), *[(lo, hi) for _, lo, hi, _, _ in EXTRA]):
        stray = const_loads_into(bytes(new), lo, hi, ours)
        assert not stray, f'constant loads into {lo:#x}: {[(hex(a), hex(t)) for a, t in stray]}'
    left = [hex(BASE + i) for i in range(0, len(new) - 3, 4) if DEV_METHOD <= struct.unpack('>I', new[i:i + 4])[0] < DEV_END
            and not any(lo - BASE <= i < hi - BASE for lo, hi in ours)]
    assert not left, f'pointers into the reused test page method: {left}'
    for n, lo, hi, slot, ret0 in EXTRA:
        left = [hex(BASE + i) for i in range(0, len(new) - 3, 4) if lo <= struct.unpack('>I', new[i:i + 4])[0] < hi
                and not any(a - BASE <= i < b - BASE for a, b in ours)]
        assert not left, f'pointers into {n}: {left}'
    for site, _, new_op, old_lit, new_lit in LIT_MOVES:
        assert new[new_lit - BASE:new_lit - BASE + 4] == img[old_lit - BASE:old_lit - BASE + 4]

    out, rewritten = sp404fw.splice(official, bytes(new))
    img2, tag2 = sp404fw.unpack(out)
    assert img2 == bytes(new) and tag2 == tag
    L = struct.unpack('>I', out[:4])[0]
    assert sum(out[4:L - 2]) & 0xFFFF == struct.unpack('>H', out[L - 2:L])[0]
    assert out[L:L + 16] == b'Roland MI059APLI' and b'localInfo:' not in out[L - 128:L]
    diff = [i for i in range(len(img)) if img[i] != img2[i]]
    allowed = (set(range(co, co + len(cave))) | set(range(do_, do_ + len(dump))) | set(range(o, o + 12)) |
               {a - BASE + k for a in (*DUMP_LITS, VT_SLOT, *FILL_SLOTS_FWD, *FILL_SLOTS_REV) for k in range(4)} |
               {m[4] - BASE + k for m in LIT_MOVES for k in range(4)} | {m[0] - BASE + k for m in LIT_MOVES for k in range(2)} |
               set(range(OSFN - BASE, OSFN - BASE + len(persist))) | set(range(WSPFN - BASE, WSPFN - BASE + len(wsp))) |
               set(range(HELPER_CODE - BASE, HELPER_CODE - BASE + len(helper))) |
               {a - BASE + k for a in (LOAD_PTR, TICK_PTR, DEV_SLOT) for k in range(4)} |
               set(range(DEV - BASE, DEV - BASE + len(dev))) |
               {x - BASE for n, lo, hi, slot, ret0 in EXTRA for x in (*range(lo, lo + len(extra[n])), *range(slot, slot + 4))} | {TEST_GATE - BASE, TEST_GATE - BASE + 1, LED_RANGE - BASE + 1, REFRESH_RANGE - BASE + 1} |
               {a - BASE + 1 for a, _, _ in VERSION_TEXT})
    assert set(diff) <= allowed, [hex(BASE + d) for d in diff if d not in allowed]
    info = dict(extra={n: len(b) for n, b in extra.items()}, dev=len(dev), cave=len(cave), dump=len(dump), diff=len(diff), rewritten=rewritten, seen=sorted(seen),
                stubs=dict(disp=disp_stub, ev=ev_stub, fill_fwd=fill_fwd, fill_rev=fill_rev, dump_ok=dump_ok),
                ram=(syms['vari_vs'], syms['vari_tab'] + 244), persist=len(persist), wsp=len(wsp), helper=len(helper),
                load_stub=load_stub, tick_stub=tick_stub)
    return out, info

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('official'); ap.add_argument('-o', default='out/sp404sx_up.bin')
    a = ap.parse_args()
    out, info = build(open(a.official, 'rb').read())
    os.makedirs(os.path.dirname(a.o) or '.', exist_ok=True)
    open(a.o, 'wb').write(out)
    print(f'wrote {a.o} ({len(out)} bytes, MD5 {hashlib.md5(out).hexdigest()})')
    print(f"free area: {info['cave']} B code/data + RAM {info['ram'][0]:#x}..{info['ram'][1]:#x}; "
          f"reclaimed area: {info['dump']} of {DUMP_END - DUMP} B")
    print(f"program bytes changed: {info['diff']}; Roland tokens rewritten: {info['rewritten']}")
    print('stubs:', {k: hex(v) for k, v in info['stubs'].items()}, 'load', hex(info['load_stub']), 'tick', hex(info['tick_stub']))
    print(f"OS routine area: {info['persist']} of {OSFN_END - OSFN} B; effects area: {info['wsp']} of {WSPFN_END - WSPFN} B; "
          f"helper: {info['helper']} of {HELPER_END - HELPER_CODE} B; test-page areas: {info['dev']} of {DEV_END - DEV} B, {info['extra']}")
    print('instructions used:', ' '.join(info['seen']))
