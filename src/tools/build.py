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
  * version screen text 1.07 -> 2.11 (0x0C0F1142, 0x0C0F115A, 0x0C0F116C)
  * PAD_INFO load pointer 0x0C093D78 and 100 ms auto-save pointer 0x0C0EF4E8 -> load_stub / tick_stub
  * chromatic mode: the note handler's play request literal 0x0C08948C -> req_stub, pad start pointer 0x0C093F44
    and pad release pointer 0x0C093F38 -> play_stub / rel_stub,
    CPagePlay event / enter / refresh slots 0x0C12E060 / 0x0C12E050 / 0x0C12E078 -> pev_stub / pen_stub /
    pref_stub, the event loop's note handler literal 0x0C08F778 -> note_stub, the MIDI receiver's and the note handlers' note gate
    literals 0x0C0F2784 / 0x0C089480 -> gate_stub / gate2_stub, HOLD's GATE lookup literals 0x0C0F1DD4 (arm) / 0x0C0F1FEC (release-all) -> hold_stub, release-all's playing-pads literal 0x0C0F1FE0 -> chr_playmask;
    pad copy / exchange worker literals 0x0C088BF8 / 0x0C088CB4 -> xcopy_stub / xexch_stub (varispeed follows the sample); TIME/BPM LED blinks in mode 9; code and state in 11 more factory test page methods

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
# the text changes: last digit 7 + 0x2A = '1', middle digit 0 + 0x31 = '1', first digit 1 + 0x31 = '2': 2.11.
VERSION_TEXT = ((0x0C0F1142, 0x7030, 0x702A),   # add #48,r0 -> add #42,r0 (ones digit: '7' -> '1')
                (0x0C0F115A, 0x7030, 0x7031),   # add #48,r0 -> add #49,r0 (tens digit: '0' -> '1')
                (0x0C0F116C, 0x7030, 0x7031))   # add #48,r0 -> add #49,r0 (hundreds digit: '1' -> '2')
# More test page methods with no literal pool used from outside, no branch in, one vtable pointer:
# (section, start, end, vtable slot, the same page's `return 0` method)
EXTRA = (('midi', 0x0C0CDDDC, 0x0C0CDEBC, 0x0C12FD24, 0x0C0CE074),     # CPageTestMidi method 2
         ('led',  0x0C0CD838, 0x0C0CD8FC, 0x0C12F9A4, 0x0C0CD900),     # CPageTestLED method 5
         # chromatic mode: more methods of the factory test pages (unreachable since test mode is off)
         ('chra', 0x0C0CEEB0, 0x0C0CEF44, 0x0C13014C, 0x0C0CEF48),     # CPageTestVer method 5
         ('chrb', 0x0C0CD4D0, 0x0C0CD55C, 0x0C12F89C, 0x0C0CD740),     # CPageTestExtract method 2
         ('chrc', 0x0C0CD238, 0x0C0CD2C4, 0x0C12F6E4, 0x0C0CD2C8),     # CPageTestBattery method 5
         ('chrd', 0x0C0CE134, 0x0C0CE1A8, 0x0C12FE1C, 0x0C0CE3A4),     # CPageTestMute method 2
         ('chre', 0x0C0CD7BC, 0x0C0CD828, 0x0C12F98C, 0x0C0CD900),     # CPageTestLED method 2
         ('chrf', 0x0C0CD1BC, 0x0C0CD228, 0x0C12F6CC, 0x0C0CD2C8),     # CPageTestBattery method 2
         ('chrg', 0x0C0CE444, 0x0C0CE4A8, 0x0C12FEF8, 0x0C0CE4E0),     # CPageTestPhones method 2
         ('chrh', 0x0C0CDC88, 0x0C0CDCEC, 0x0C12FC30, 0x0C0CDD24),     # CPageTestMicInt method 2
         ('chri', 0x0C0CDB20, 0x0C0CDB84, 0x0C12FB44, 0x0C0CDBBC),     # CPageTestMicExt method 2
         ('chrj', 0x0C0CD9D0, 0x0C0CDA34, 0x0C12FA68, 0x0C0CDA6C),     # CPageTestLine method 2
         ('chrk', 0x0C0CD398, 0x0C0CD3FC, 0x0C12F7B0, 0x0C0CD414),     # CPageTestCompleted method 2
         ('chrl', 0x0C0CEC38, 0x0C0CECC8, 0x0C130124, 0x0C0CEF48),     # CPageTestVer method 0
         ('chrm', 0x0C0CF49C, 0x0C0CF51C, 0x0C1302F0, 0x0C0CF7B8),     # CPageTestDevice method 0
         ('chrn', 0x0C0CE1A8, 0x0C0CE214, 0x0C12FE24, 0x0C0CE3A4),     # CPageTestMute method 3
         ('chrp', 0x0C0CE720, 0x0C0CE768, 0x0C130044, 0x0C0CE834),     # CPageTestSw method 0
         ('chrq', 0x0C0CD358, 0x0C0CD394, 0x0C12F7A0, 0x0C0CD414),     # CPageTestCompleted method 0
         # larger test page methods whose literal pools the page's earlier methods share; see POOL_SPANS
         ('chrx', 0x0C0CD56C, 0x0C0CD73C, 0x0C12F8B4, 0x0C0CD740),     # CPageTestExtract method 5
         ('chry', 0x0C0CDEE4, 0x0C0CE070, 0x0C12FD3C, 0x0C0CE074),     # CPageTestMidi method 5
         ('chrz', 0x0C0CE280, 0x0C0CE3A0, 0x0C12FE34, 0x0C0CE3A4),     # CPageTestMute method 5
         # first methods that start right after a literal pool word instead of a nop / rts
         ('chrs', 0x0C0CD474, 0x0C0CD4BC, 0x0C12F88C, 0x0C0CD740),     # CPageTestExtract method 0
         ('chru', 0x0C0CDC20, 0x0C0CDC70, 0x0C12FC20, 0x0C0CDD24),     # CPageTestMicInt method 0
         ('chrv', 0x0C0CD974, 0x0C0CD9B0, 0x0C12FA58, 0x0C0CDA6C),     # CPageTestLine method 0
         # helper functions after the CPageTestSw constructor (0x0C0CE838..0x0C0CE85F, live, kept), called only by
         # CPageTestVer methods; no vtable slot (see HELPER_BLOCK)
         ('chrw', 0x0C0CE860, 0x0C0CEC34, None, None))
# chrw: the only branches into it, all from CPageTestVer methods (span below: no constructor, reached only through
# its vtable); 4 of them are inside chrl, which is replaced by our code.
HELPER_BLOCK = ('chrw', (0x0C0CEC9E, 0x0C0CECA8, 0x0C0CECB4, 0x0C0CECB8, 0x0C0CED4C, 0x0C0CEE4C), (0x0C0CEC38, 0x0C0CEF48))
# For these, constant loads into the area are allowed only from inside the span of that page's earlier methods.
# The build checks that each span is dead like the area itself: no branch into it from outside, no pointer into it
# except from a test page vtable, and no load of a test page vtable in it (a constructor, which runs at boot).
POOL_SPANS = {'chrx': (0x0C0CD474, 0x0C0CD56C), 'chry': (0x0C0CDD88, 0x0C0CDEE4), 'chrz': (0x0C0CE0A0, 0x0C0CE280)}
TEST_VTABLES = (0x0C12F6B0, 0x0C12F794, 0x0C1302E4, 0x0C12F880, 0x0C12F970, 0x0C12FA4C, 0x0C12FB28, 0x0C12FC14,
                0x0C12FD08, 0x0C12FE00, 0x0C12FEDC, 0x0C130038, 0x0C130118, 0x0C1301F0)
TEST_VT_AREA = (0x0C12F6B0, 0x0C130330)
# Chromatic mode hooks: pointers through which Roland calls pad start / pad release, and the play page's
# event vtable slot (pointer site, Roland function it held)
REQ_PTR, REQ_FN, REQ_LOAD = 0x0C08948C, 0x0C093D28, 0x0C0892DE   # note handler -> play request (its own literal)
PLAY_PTR, PLAY_FN = 0x0C093F44, 0x0C084974       # play request 0x0C093D28 -> start pad (kind, pad, vel)
REL_PTR, REL_FN = 0x0C093F38, 0x0C084B48         # release request 0x0C093DE4 -> pad released (pad)
PEV_SLOT, PEV_FN = 0x0C12E060, 0x0C0C5384        # CPagePlay vtable: event handler
NOTE_PTR, NOTE_FN = 0x0C08F778, 0x0C089330      # event loop 0x0C08F708 -> note handler (event)
GATE_PTR, GATE_FN, GATE_LOAD = 0x0C0F2784, 0x0C08FD70, 0x0C0F2676   # MIDI receive -> "notes allowed?" (its own literal)
GATE2_PTR, GATE2_LOADS = 0x0C089480, (0x0C08930A, 0x0C08935E)      # note handlers -> the same check, MIDI events only
HOLD_PTR, HOLD_LOAD = 0x0C0F1DD4, 0x0C0F1D5A    # HOLD check 0x0C0F1D50 -> R_GET(38 = GATE, pressed pad) (its own literal)
# HOLD release-all 0x0C0F1E94 (called by page handlers): keeps a held pad only if R_GET(38 = GATE, pad) == 1; its own
# literal, loaded once into r2 and kept in macl (lds r2,macl), called at four sites as sts macl,r1; mov r8,r5; jsr @r1; mov #38,r4
HOLD2_PTR, HOLD2_LOAD, HOLD2_MACL = 0x0C0F1FEC, 0x0C0F1EF4, 0x0C0F1EF8
HOLD2_CALLS = (0x0C0F1F26, 0x0C0F1F8A, 0x0C0F200C, 0x0C0F2074)
# ... and lets go of a latched pad whose bit in 0x0C085074(bank) (pads with a voice playing) is clear: its own literal,
# loaded once (mov.l @(8,r15),r4; jsr @r3), straight to chr_playmask (release-all sets mach / macl only after the call)
PMASK_PTR, PMASK_FN, PMASK_LOAD = 0x0C0F1FE0, 0x0C085074, 0x0C0F1EDE
# Varispeed follows copy / exchange: the file task's job wrappers (commands 12 / 11, 0x0C088BB0 / 0x0C088C6C) call
# Roland's workers through their own literals, each loaded once; the workers take the pads from params 60 / 61
XCOPY_PTR, XCOPY_FN, XCOPY_LOAD = 0x0C088BF8, 0x0C087A64, 0x0C088BD2   # copy worker (0 = done)
XEXCH_PTR, XEXCH_FN, XEXCH_LOAD = 0x0C088CB4, 0x0C087DEC, 0x0C088C8E   # exchange worker (0 = done)
# TIME/BPM LED: modes other than 1..8 (the chromatic menu, mode 9) blink the TIME/BPM button instead of
# turning it off: `mov #0,r5` -> `mov #2,r5` (LED value 2 = blink)
LED_BLINK, LED_BLINK_OLD, LED_BLINK_NEW = 0x0C0CBF12, bytes.fromhex('e500 480b e415'), 0xE502
PEN_SLOT, PEN_FN = 0x0C12E050, 0x0C0C5250        # CPagePlay vtable: enter
PREF_SLOT, PREF_FN = 0x0C12E078, 0x0C0C53F4      # CPagePlay vtable: refresh (UI loop, every other tick)
CHR_DATA = ('chrk', 'chrg')                      # areas holding only chromatic tables, texts and state (no code)
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
    for sec in ('persist', 'wsp', 'helper', 'dev', *(n for n, *_ in EXTRA)):
        run(['sh4-linux-gnu-objcopy', '-O', 'binary', '-j', '.' + sec, 'vari.elf', sec + '.bin'])
    syms = {}
    for line in run(['sh4-linux-gnu-nm', 'vari.elf']).splitlines():
        a, t, n = line.split()
        syms[n] = int(a, 16)
    assert not run(['sh4-linux-gnu-nm', '-u', 'vari.elf']).strip(), 'undefined symbols'
    code = ('.entry', '.text', '.persist', '.helper', '.dev', *('.' + n for n, *_ in EXTRA if n not in CHR_DATA))
    dis = run(['sh4-linux-gnu-objdump', '-d', *(x for s in code for x in ('-j', s)), 'vari.elf'])
    rd = lambda n: open(os.path.join(SRC, n), 'rb').read()
    return rd('cave.bin'), rd('dump.bin'), rd('persist.bin'), rd('wsp.bin'), rd('helper.bin'), rd('dev.bin'), {n: rd(n + '.bin') for n, *_ in EXTRA}, syms, dis

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
            (syms['chr_lits'], syms['chr_lits_end']),                        # chromatic stub literals
            (syms['chr_lits2'], syms['chr_lits2_end']),
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

def const_loads_into_value(img, value):
    """(load address, pool address) of every mov.l @(disp,PC) in the image that loads `value`."""
    res = []
    for i in range(0x800, len(img) - 1, 2):
        op = (img[i] << 8) | img[i + 1]
        if op >> 12 == 0xD:
            a = BASE + i; t = (a & ~3) + 4 + (op & 0xFF) * 4
            if t + 4 <= BASE + len(img) and struct.unpack('>I', img[t - BASE:t - BASE + 4])[0] == value: res.append((a, t))
    return res

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
        assert lo + len(extra[n]) <= hi and extra[n], f'{n} empty or too big'
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
    ctor_loads = [a for vt in TEST_VTABLES for a, _ in const_loads_into_value(img, vt)]
    for n, lo, hi, slot, ret0 in EXTRA:
        if slot is None:                                   # the helper block
            hn, srcs, (s0, s1) = HELPER_BLOCK
            br = branches_into(img, lo, hi)
            assert n == hn and sorted(int(a, 16) for a, _ in br) == sorted(srcs), f'branches into {n}: {br}'
            assert all(s0 <= a < s1 for a in srcs) and not [a for a in ctor_loads if s0 <= a < s1], f'{n} callers'
            assert not branches_into(img, s0, s1), f'something branches into the callers of {n}'
            sp = [BASE + i for i in range(0, len(img) - 3, 4) if s0 <= struct.unpack('>I', img[i:i + 4])[0] < s1]
            assert all(TEST_VT_AREA[0] <= p < TEST_VT_AREA[1] for p in sp), f'pointers into the callers of {n}: {sp}'
            assert not const_loads_into(img, lo, hi, [(lo, hi)]), f'constant loads into {n}'
            assert not [i for i in range(0, len(img) - 3, 4) if lo <= struct.unpack('>I', img[i:i + 4])[0] < hi], f'pointers into {n}'
            assert img[lo - BASE - 4:lo - BASE] == bytes.fromhex('0c0b f920') and img[lo - BASE:lo - BASE + 2] == bytes.fromhex('2f96'), f'{n} start'
            continue
        assert not branches_into(img, lo, hi), f'something branches into {n}'
        loads = const_loads_into(img, lo, hi, [(lo, hi)])
        if n in POOL_SPANS:
            s0, s1 = POOL_SPANS[n]
            assert s1 == lo and all(s0 <= a < s1 for a, _ in loads), f'constant loads into {n} from outside its span'
            assert not branches_into(img, s0, hi), f'something branches into the span of {n}'   # span + area
            assert not [a for a in ctor_loads if s0 <= a < s1], f'a constructor in the span of {n}'
            sp = [BASE + i for i in range(0, len(img) - 3, 4) if s0 <= struct.unpack('>I', img[i:i + 4])[0] < s1]
            assert all(TEST_VT_AREA[0] <= p < TEST_VT_AREA[1] for p in sp), f'pointers into the span of {n}: {sp}'
            assert img[hi - BASE:hi - BASE + 4] in (bytes(4), bytes.fromhex('e000 006b')) or hi + 4 == ret0, f'{n} end'
        else:
            assert not loads, f'constant loads into {n}'
            assert img[hi - BASE - 2:hi - BASE] == bytes.fromhex('006b') or \
                img[hi - BASE - 4:hi - BASE] == bytes.fromhex('006b 0009'), f'{n} end'
        ptr = [BASE + i for i in range(0, len(img) - 3, 4) if lo <= struct.unpack('>I', img[i:i + 4])[0] < hi]
        assert ptr == [slot] and u32(img, slot) == lo, f'pointers into {n}: {[hex(p) for p in ptr]}'
        assert img[lo - BASE:lo - BASE + 2] == bytes.fromhex('4f22') and (img[lo - BASE - 2:lo - BASE] in
            (bytes.fromhex('0009'), bytes.fromhex('006b')) or lo - 2 in literal_pools(img)), f'{n} start'
        assert img[ret0 - BASE:ret0 - BASE + 4] == bytes.fromhex('e000 006b'), f'{n} return-0 method'
    assert img[TEST_GATE - BASE - 2:TEST_GATE - BASE + 6] == TEST_GATE_OLD, 'test-mode gate not found'
    assert [i for i in range(0, len(img) - 3, 4) if struct.unpack('>I', img[i:i + 4])[0] == 0x0C0BFC40] == \
        [0x0C0D1978 - BASE], 'test-mode first page is reachable from elsewhere'
    # the MIDI note gate is called through two literals; only the MIDI receiver's one (loaded once) is swapped
    assert u32(img, GATE_PTR) == GATE_FN and const_loads_into(img, GATE_PTR, GATE_PTR + 4, []) == [(GATE_LOAD, GATE_PTR)], \
        'MIDI gate literal'
    assert img[GATE_LOAD - BASE + 2:GATE_LOAD - BASE + 4] == bytes.fromhex('434b'), 'MIDI gate call (jsr/n @r3)'
    # the note handlers' literal: loaded at exactly two sites, each called right after a "kind == 3" test
    assert u32(img, GATE2_PTR) == GATE_FN and sorted(a for a, _ in const_loads_into(img, GATE2_PTR, GATE2_PTR + 4, [])) == \
        sorted(GATE2_LOADS), 'note handler MIDI gate literal'
    for a in GATE2_LOADS:
        assert img[a - BASE + 2:a - BASE + 4] == bytes.fromhex('434b'), f'MIDI gate call at {a:#x}'
    assert u32(img, HOLD_PTR) == 0x0C082B34 and const_loads_into(img, HOLD_PTR, HOLD_PTR + 4, []) == [(HOLD_LOAD, HOLD_PTR)] \
        and img[HOLD_LOAD - BASE + 4:HOLD_LOAD - BASE + 8] == bytes.fromhex('430b e426'), 'HOLD GATE lookup literal'
    assert u32(img, HOLD2_PTR) == 0x0C082B34 and const_loads_into(img, HOLD2_PTR, HOLD2_PTR + 4, []) == [(HOLD2_LOAD, HOLD2_PTR)] \
        and img[HOLD2_LOAD - BASE] == 0xD2 and img[HOLD2_MACL - BASE:HOLD2_MACL - BASE + 2] == bytes.fromhex('421a') \
        and all(img[a - BASE:a - BASE + 8] == bytes.fromhex('011a 6583 410b e426') for a in HOLD2_CALLS), 'HOLD release-all GATE literal'
    assert u32(img, PMASK_PTR) == PMASK_FN and const_loads_into(img, PMASK_PTR, PMASK_PTR + 4, []) == [(PMASK_LOAD, PMASK_PTR)] \
        and img[PMASK_LOAD - BASE] == 0xD3 and img[PMASK_LOAD - BASE + 2:PMASK_LOAD - BASE + 6] == bytes.fromhex('54f2 430b') \
        and img[PMASK_LOAD - BASE + 16:PMASK_LOAD - BASE + 18] == bytes.fromhex('420a'), 'HOLD release-all playing-pads literal'
    for p, f, ld in ((XCOPY_PTR, XCOPY_FN, XCOPY_LOAD), (XEXCH_PTR, XEXCH_FN, XEXCH_LOAD)):
        assert u32(img, p) == f and const_loads_into(img, p, p + 4, []) == [(ld, p)] and \
            img[ld - BASE + 2:ld - BASE + 4] == bytes.fromhex('434b'), f'copy / exchange worker literal {p:#x}'
        assert [i for i in range(0, len(img) - 3, 4) if struct.unpack('>I', img[i:i + 4])[0] == f] == [p - BASE], \
            f'{f:#x} is referenced from more than {p:#x}'
    # the play request is called through three literals; only the note handler's one (loaded once) is swapped
    assert u32(img, REQ_PTR) == REQ_FN and const_loads_into(img, REQ_PTR, REQ_PTR + 4, []) == [(REQ_LOAD, REQ_PTR)], \
        'play request literal'
    for p, f in ((LOAD_PTR, LOAD_FN), (TICK_PTR, TICK_FN), (PLAY_PTR, PLAY_FN), (REL_PTR, REL_FN), (PEV_SLOT, PEV_FN),
                 (NOTE_PTR, NOTE_FN), (PEN_SLOT, PEN_FN), (PREF_SLOT, PREF_FN)):
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
        if slot is not None: put32(new, slot, lo, ret0)
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
    put32(new, REQ_PTR, REQ_FN, syms['req_stub'])
    put32(new, PLAY_PTR, PLAY_FN, syms['play_stub'])
    put32(new, REL_PTR, REL_FN, syms['rel_stub'])
    put32(new, PEV_SLOT, PEV_FN, syms['pev_stub'])
    put32(new, NOTE_PTR, NOTE_FN, syms['note_stub'])
    put32(new, GATE_PTR, GATE_FN, syms['gate_stub'])
    put32(new, GATE2_PTR, GATE_FN, syms['gate2_stub'])
    put32(new, HOLD_PTR, 0x0C082B34, syms['hold_stub'])
    put32(new, HOLD2_PTR, 0x0C082B34, syms['hold_stub'])
    put32(new, PMASK_PTR, PMASK_FN, syms['chr_playmask'])
    put32(new, XCOPY_PTR, XCOPY_FN, syms['xcopy_stub'])
    put32(new, XEXCH_PTR, XEXCH_FN, syms['xexch_stub'])
    assert img[LED_BLINK - BASE:LED_BLINK - BASE + 6] == LED_BLINK_OLD, 'TIME/BPM LED off path not found'
    new[LED_BLINK - BASE:LED_BLINK - BASE + 2] = struct.pack('>H', LED_BLINK_NEW)
    put32(new, PEN_SLOT, PEN_FN, syms['pen_stub'])
    put32(new, PREF_SLOT, PREF_FN, syms['pref_stub'])
    for lit in DUMP_LITS:
        put32(new, lit, DUMP, dump_ok)
    put32(new, VT_SLOT, VT_OLD, ev_stub)
    for s in FILL_SLOTS_FWD: put32(new, s, FILL_FWD, fill_fwd)
    for s in FILL_SLOTS_REV: put32(new, s, FILL_REV, fill_rev)
    # no pointer to the dump or fill routines left anywhere except in our own code and stub literals
    for target in (DUMP, FILL_FWD, FILL_REV):
        left = [hex(BASE + i) for i in range(0, len(new) - 3, 4)
                if struct.unpack('>I', new[i:i + 4])[0] == target and not any(
                    lo - BASE <= i < hi - BASE for lo, hi in ((CAVE, CAVE_END), (DUMP, DUMP_END), (OSFN, OSFN_END),
                                                               (HELPER_CODE, HELPER_END), (DEV, DEV_END),
                                                               *((lo, lo + len(extra[n])) for n, lo, *_ in EXTRA)))]
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
        # a literal-pool word of the stock image stays data even when its only loader was in an area we reused
        stray = [(a, t) for a, t in const_loads_into(bytes(new), lo, hi, ours) if a not in literal_pools(img)
                 and not any(s0 <= a < s1 for s0, s1 in POOL_SPANS.values())]
        assert not stray, f'constant loads into {lo:#x}: {[(hex(a), hex(t)) for a, t in stray]}'
    left = [hex(BASE + i) for i in range(0, len(new) - 3, 4) if DEV_METHOD <= struct.unpack('>I', new[i:i + 4])[0] < DEV_END
            and not any(lo - BASE <= i < hi - BASE for lo, hi in ours)]
    assert not left, f'pointers into the reused test page method: {left}'
    hooks = {REQ_PTR: syms['req_stub'], PLAY_PTR: syms['play_stub'], REL_PTR: syms['rel_stub'], PEV_SLOT: syms['pev_stub'], NOTE_PTR: syms['note_stub'], GATE_PTR: syms['gate_stub'], GATE2_PTR: syms['gate2_stub'], HOLD_PTR: syms['hold_stub'], HOLD2_PTR: syms['hold_stub'], PMASK_PTR: syms['chr_playmask'], XCOPY_PTR: syms['xcopy_stub'], XEXCH_PTR: syms['xexch_stub'], PEN_SLOT: syms['pen_stub'], PREF_SLOT: syms['pref_stub']}   # planned entries
    for n, lo, hi, slot, ret0 in EXTRA:
        left = [hex(BASE + i) for i in range(0, len(new) - 3, 4) if lo <= struct.unpack('>I', new[i:i + 4])[0] < hi
                and not any(a - BASE <= i < b - BASE for a, b in ours)
                and hooks.get(BASE + i) != struct.unpack('>I', new[i:i + 4])[0]]
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
               {a - BASE + k for a in (LOAD_PTR, TICK_PTR, DEV_SLOT, REQ_PTR, PLAY_PTR, REL_PTR, PEV_SLOT, NOTE_PTR, GATE_PTR, GATE2_PTR, HOLD_PTR, HOLD2_PTR, PMASK_PTR, XCOPY_PTR, XEXCH_PTR, PEN_SLOT, PREF_SLOT) for k in range(4)} |
               set(range(DEV - BASE, DEV - BASE + len(dev))) |
               {x - BASE for n, lo, hi, slot, ret0 in EXTRA for x in (*range(lo, lo + len(extra[n])), *(range(slot, slot + 4) if slot else ()))} | {TEST_GATE - BASE, TEST_GATE - BASE + 1, LED_RANGE - BASE + 1, REFRESH_RANGE - BASE + 1, LED_BLINK - BASE + 1} |
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
