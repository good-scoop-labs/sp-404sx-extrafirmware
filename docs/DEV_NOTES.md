# SP-404SX/A EXTRAfirmware: development notes

> Engineering log, kept as written during development. "Build N" / "test N" are internal test builds that
> were flashed on hardware one by one; release 1.0.0 is build 13. Addresses are run-time addresses in the
> official 1.07 image (loaded at 0x0C080000). Tool names in older entries refer to the development
> scripts that became `src/tools/build.py`; `sp404fw.py` and `sh.py` kept their names. The "Next" section
> near the end is a historical to-do list from the start of the project.

Source: official SP-404SX System Update v1.07 (`sp404_sys_v107.zip` → `sp404sx_up.bin`,
MD5 `96a4e2622738acd48cbbbfc667ebcfe5`, 468,176 bytes). SP-404A runs the same firmware.

## Update container (solved, round-trip verified)
`u32be L | u32be unpacked_size (0xB8FF4) | LZSS stream | zero pad | u16be checksum | 48-byte tag`
- LZSS: Okumura (N=4096, F=18, threshold 2, ring zero-filled, start N-F, flag bits LSB-first, 1 = literal,
  ref = `lo | (hi&0xF0)<<4`, length `(hi&0x0F)+3`).
- Checksum at L-2: 16-bit sum of bytes `file[4 : L-2]`. The updater checks it by reading back flash
  **after** programming (0x0C0F0E5C).
- Tag at L: `Roland MI059APLI` + `VER.1.07-BLD.03712 0829 09 10 19`. `MI059BOOT` = bootloader update
  (`/sp404sx_bup.bin`).
- Tool: `sp404fw.py` (unpack/pack, verified to decode back to the identical image).

## Hardware / memory map (from code)
- CPU: Renesas **SH-2A** with FPU (big-endian; movi20, jsr/n, rts/n, 32-bit mov.l @(disp12)). Not V850.
- Program runs from SDRAM at **0x0C080000** (image offset 0). Reset vector at image[0]: PC 0x0C080800, SP 0x0D000000.
- Flash at 0x20000000: bootloader 0x20000000–0x2007FFFF, application stored (compressed, as the
  file) at **0x20080000**. App update does not rewrite the bootloader.
- WSP voice chip (`CWSPDrv`, vtable 0x0C113950, methods 0x0C0A44BC–0x0C0A4BCC), register base held in
  the object (0x30000000 area); indexed access: index → +0x0C, data → +0x0A (16-bit) / +0x08 (32-bit).
  Per-channel 3-word groups at index 0x1000 + ch*4.
- Effects DSP: `CXVDSPDrv`. MFX classes `CMfx*` (incl. PITS/PITC pitch shifter, DJFXLP, BPMLP).
- Time Modify is CPU software (`TIME_MODIFY::Stretch/Compress/CrossFade` state machine).
- Samples are loaded ("charged") into wave memory by `CSmplCharge{WAV,AIF}{Mn,St}[Rev]`.

## Brick risk
- The card updater runs **inside the application** (card task 0x0C0F127C, started from 0x0C0F155C;
  waits ≤3 s for the card, then checks `/sp404sx_up.bin`). A modded app that crashes before that
  check can't be fixed from the card. The bootloader's own recovery ability is unknown.
- Before flashing anything: board photos, identify the flash chip, plan a programmer/debug-port recovery path.

## PAD_INFO.BIN (RAM copy at 0x0C179344, 120 × 32 bytes)
Fields per the firmware's own debug dump (READ_PAD_INFO.TXT): Data Start/End, Loop Start/End, Level,
LoFi, Loop Sw, Gate, Reverse, **Form** (offset 21: 1 = WAV, 0 = AIFF, not a spare byte), Ch,
Sync Type, Original BPM, Modify BPM. No spare byte → varispeed needs its own storage (sidecar file).

## Proposed design (draft)
- CTRL 3 in TIME/BPM = varispeed; display always shows the resulting BPM (base × stretch × varispeed).
- Varispeed applied as playback rate on top of Time Modify (stretch first, then speed).
- Varispeed "Ptn" position = tape sync (speed to pattern tempo, pitch follows). With Time Modify on Ptn
  and manual varispeed: tempo stays locked and stretch compensates, so varispeed acts as a pitch shift.
- Storage: sidecar `/ROLAND/SP-404SX/SMPL/VARI_INF.BIN` (120 × int16 cents), ignored by stock firmware;
  keep in sync on pad copy/exchange/delete/import.

## TIME/BPM page (CPageSmplEdit), mapped
- Param system: get(id, pad) 0x0C082B34, set(id, value, pad, flag) 0x0C082B98; pad = get(52, -1) (current pad).
  Pad param ids → PAD_INFO via pointer table 0x0C17B14C[pad]: 31/32 start, 33 end, 35 data end, 36 lofi(+17),
  37 stereo(+22==2), 38 gate(+19), 39 loop(+18), 40 reverse(+20), 41 level(+16), 42 orig BPM×10 (+24),
  43 sync type (+23: 0 OFF, 1 PTN, 2 user), 44 modify BPM×10 (+28).
- Knob handler 0x0C0CBB7C (page, ctrl, value 0–127):
  ctrl 0 = CTRL 1 Time Modify: value ≤2 → OFF, ≥125 → PTN, else BPM = lerp(0.5×, 1.3× orig) by value/127,
  rounded to whole BPM, clamped 40.0–200.0, written to param 44 (sync=2). Sets display mode 7.
  ctrl 1 = CTRL 2 (0x0C0CBD14, display mode 6, halve/double). **ctrl 2 = CTRL 3 → bra exit at 0x0C0CBBA2 (unused).**
- Display 0x0C0CB5D4 switches on page+4 (mode 0–7); mode 7: reverse on → "---", sync 0 → "OFF", 1 → "PTN",
  else param44 shown via (x+5)/10. Show-string 0x0C0CFF84, show-number 0x0C0CFFF8.

## Voice pipeline, mapped (no per-voice pitch hardware found)
- Samples stream from SD. Per pad 90,684-byte preload/head buffer at 0x0C18B32C + pad×90684.
- Voice feeder objects `CSmplCharge` (0x0CC1CBA0 + n×0xC05C); per-format subclasses WAV/AIF × Mn/St (+Rev).
  Fill 0x0C0914B8: copies source → 4096-frame L/R 16-bit blocks (double-buffered) inside the player object.
  Copy routines are 1:1 deinterleave (e.g. WAV stereo 0x0C090760, mono 0x0C09066C): no rate conversion.
- Voice start/service 0x0C0963C0 (player object ≥ 0x140C8 bytes, L/R blocks at +126, +0x2000, +0x4000, +0x6000).
- Time Modify = C++ state classes TIME_MODIFY::{Copy, CrossFade, Compress*, Stretch*, ModifyState*} working on
  16-bit L/R streams on the CPU (Copy method 0x0C08D950). Blocks are then handed off (0x0C08EE9C, on-chip RAM
  bookkeeping at 0xFFF80000) toward the effects chip.
- "WSP" (CWSPDrv, 2 register banks 0x30000000 / 0x30000200, objects 0x0CDA95C0 / 0x0CDA95CC) loads 192-word
  programs and 1536-entry tables → behaves like the effects/mixing DSP with its own SDRAM ("SDRAM (WSP)" in the
  service-notes test mode), not a pitchable voice generator.
- ⇒ Varispeed must be a CPU resampler in the voice path (best spot: the CSmplCharge copy stage, consuming
  ratio × frames per output frame), with Time Modify running on the result.

## Room for new code
- VBR = 0x0C080000: the image's zero block at 0x0C080010–0x0C0807FF holds the runtime vector table (not free).
- RAM below the image (0x0C001000–0x0C07FFFF) is essentially unreferenced by the app (likely boot loader work RAM).
- Reset code's first call (0x0C080808 → 0x0C0EF038) is an empty `rts` hook: can run a relocator that copies an
  appended code blob below the image before anything else starts.

## Hardware tests
- Test 1 (2 bytes: version shows E.01): flashed and worked on the test unit (2026-10-30).
- Minimal-change packer `sp404fw.splice`: keeps Roland's own LZSS tokens and only turns tokens touching changed
  bytes into literals. With no changes it reproduces the official file byte for byte. Padding: L aligned to 16.
- Boot loader uses a zero-filled LZSS ring: the official stream builds the reset code at 0x0C080800 from
  initial-ring references, so any other fill would break stock firmware.
- Free code space: 0x0C080400–0x0C0807FF (1 KB). VBR = 0x0C080000, vectors 0–255 only (installer 0x0C0E6154 rejects ≥256).
  Zero runs at 0x0C116D16 / 0x0C11754A are inside effects-DSP program data: NOT free. Image end is followed
  directly by .bss at 0x0C139000. Low RAM 0x0C000000+ is used by the file system (0x0C0E36DA).
- Test 2: version routine 0x0C0F112C → jump to cave function writing "E.02" (tools/build_test2.py, tools/sha.py).
  Worked on the test unit; normal operation fine; reflash to stock fine (2026-10-30).
- Test 3 (tools/build_test3.py, src3/): CTRL 3 on TIME/BPM → per-pad knob position in RAM table (cave, 120 B),
  display mode 8 shows base BPM × ratio (Q14 table, −12…+7 st, centre detent 62–65). Display only.
  Hooks: CPageSmplEdit vtable slot 0x0C12F414 (dispatcher 0x0C0CC078 → ev_stub) and a 12-byte trampoline at
  0x0C0CB5D4 (display) → disp_stub, which replays the 6 replaced instructions and continues at 0x0C0CB5E0.
  Event struct: +0 type (1 = knob), +4 ctrl id (u8), +5 value (s8). Page: +4 mode.
  Roland helpers: show number 0x0C0CFFF8(n), show text 0x0C0CFF84(str, 1), div 0x0C0F48AC(a, b).
- Toolchain: Ubuntu gcc-sh4-linux-gnu with -m4-nofpu -mb (SH-2A flags are rejected); the build checks every
  instruction against an SH-2A allow-list. Roland's ABI keeps MACH/MACL across calls (GCC doesn't), so the
  entry stubs save them.
- Test 3 result on the unit: CTRL 1/2 normal, display correct when switching pads, CTRL 3 still the effect knob
  outside TIME/BPM (2026-10-30).

## Audio plan (build 4)
- Fill = vtable slot 44 of every CSmplCharge class (forward 0x0C0914B8, reverse 0x0C0921FC):
  fill(this, L*, R*, flag*, int *frames) → produces *frames 16-bit L/R frames; handles SD streaming, loops,
  end-of-sample internally via slots 52 (frames→bytes), 60 (bytes→frames), 68 (copy), 76 (silence), 84 (end).
  Callers: voice service 0x0C09645C / 0x0C0964B8 (4096-frame double buffers in the player object).
- Varispeed = wrapper on slot 44 (pointer swap per class, 9 vtables): request floor(φ + F·r) source frames from
  the original fill into the destination buffer itself, then resample in place (r ≥ 1: source placed at +1,
  several passes; r < 1: source placed at the tail, one pass). Per-voice state: last two frames + phase.
  Linear interpolation by default.
- Code space: hidden PAD_INFO debug dump 0x0C089EF4–0x0C08A35F (1,132 bytes) is reached only through the literals
  0x0C0CFE48 (version page) and 0x0C0C26FC (another page); point both at a `return 1` stub, then reuse the space.

## Build 4 (tools/build_test4.py, src4/)
- Audible varispeed. vari_fill wraps fill (all 10 CSmplCharge vtables; stubs fill_fwd/fill_rev pass the original
  as a 6th argument). Unity → Roland's fill untouched. Otherwise floor(ph + (F-1)·r) source frames are fetched
  into the block itself and resampled in place (linear, Q16 phase, per-voice state indexed by obj+24, reset when
  obj+16 cursor isn't where we left it; nxt = cursor+1 so zeroed RAM means "not running").
- Ratio table now u8: ratio = (x+128)/256 (0.5..1.496), in the free area with the UI code. Resampler code in the
  reclaimed debug-dump area (1,032/1,132 B). Free area: 596 B code/data, RAM 0x0C080654–0x0C0807CC.
- Host tests (same C source): 400 randomized cases bit-exact vs a whole-stream reference (speeds, sizes 1–5000,
  end-of-sample); 300M samples under ASan/UBSan with random speed changes and retriggers.
- 1,630 program bytes changed, all inside: free area, reclaimed area, display jump, 2 dump literals,
  TIME/BPM vtable slot, 10 feeder vtable slots.

## Test 4 bug and 4b fix
- Test 4 on the unit: varispeed worked, but pad copy hung on "CPY", recording/resampling froze, and LO-FI in
  sampling didn't respond.
- Cause: the PAD_INFO save routine 0x0C089DE4 (11 callers) loads memcpy (0x0C0F4004) and PAD_INFO (0x0C17A244)
  from 0x0C08A13C/0x0C08A140, inside the reclaimed debug-dump area. Overwriting it made every save jump to garbage.
  My branch/pointer checks missed PC-relative literal loads.
- 4b: constants moved into the dead filename helper (0x0C089E9C/0x0C089EA0, only caller was the dump); loads at
  0x0C089E80/82 changed d3ae→d306, d4af→d407. build_test4.py now asserts that no live code loads a constant from
  inside any patched area (const_loads_into), before and after patching.
- The second dump caller (literal 0x0C0C26FC) is CPageHardChk (factory hardware check), not sampling.

## Build 5 (tools/build_test5.py, src5/)
- Test 4b results: copy/record/resample/delete fixed; LO-FI in sampling still not lighting (stock behaviour unconfirmed);
  exchange left varispeed on the pad; copy didn't copy it; power cycle forgot it.
- Persistence: table {u32 "VSP1"; 120 × {s8 pos, u8 fp}} = 244 B in RAM, saved verbatim as
  /ROLAND/SP-404SX/SMPL/VARISPD.BIN. fp = 1-byte hash of PAD_INFO data end (+4), form (+21), ch (+22); 0 = empty.
- Hooks (pointer swaps): 0x0C093D78 → load_stub (runs Roland's PAD_INFO load 0x0C08A68C, then sets vari_need_load);
  0x0C0EF4E8 → tick_stub (runs Roland's 100 ms auto-save 0x0C08A700, then vari_tick(ret, ms)).
- vari_tick (background task, every 100 ms while a card is ready): if need_load → read the file under lock 14 (fresh
  table if missing/invalid), resync, clear dirty, never write. Else resync (changed fp → take the speed from the pad
  that held that fp before, else 0) and write the file 1 s after the last change (CTRL 3 sets dirty too).
  vari_need_load starts at 1 in the image, so a tick before the first load can never overwrite the card file.
- Roland file API: lock/unlock 0x0C0EE838/844 (#14), create 0x0C0D2A04, open 0x0C0D2940 (mode 1 = read),
  read 0x0C0D2A80, write 0x0C0D2B30, close 0x0C0D2B8C, delete 0x0C0D2C50, card ready 0x0C0EF484.
  PAD_INFO save 0x0C089DE4 keeps a last-saved copy at 0x0C17A244; auto-save 0x0C08A700 is driven by the loop at 0x0C0EF458.
- More reclaimed space (unreferenced by branch, aligned pointer and constant load): unused OS service routine
  0x0C0EB5A0–0x0C0EB757 (resync + tick), unused effects method 0x0C0A9558–0x0C0A9603 (ratio table + path),
  rest of the dead filename helper 0x0C089EA4–0x0C089EF3 (pad_fp). vari_save sits at the start of the dump area.
- MAXV = 12 voices (polyphony); a voice index ≥ 12 plays at normal speed.
- Host tests: persistence 16/16 (boot order, missing/corrupt/short file, copy, exchange, delete, record, edits, reload,
  moved samples), resampler 400/400 + 300M sanitizer samples. 2,362 program bytes changed, all in expected areas.

## Build 6 (build_test5.py → out/test6, src5/)
- LO-FI in sampling is a real-time effect, not a bug (tester confirmed).
- CTRL 3 knob 0–2 = OFF (ratio 1.0, display Roland's "OFF" at 0x0C12F3EC); 3 = −12 st, 62–65 = 1.0, 127 = +7 st.
  "---" is Roland's string at 0x0C12F3E8. stub_table/persist_table removed (build reads linker symbols).
- Free area is now exactly full (RAM ends at 0x0C080800); OS area 4 B, effects 8 B, dump 8 B, helper 0 B left.
- Requested next: CTRL 3 fully right = PTN (tape sync to pattern tempo). Pattern tempo ×10 = 0x0C0A9E5C(2)
  (system value store 0x0CDAA010). Needs ~100–150 B more code space.

## Next
1. Board photos → confirm CPU/flash part numbers and a recovery path.
2. Patch plan: CTRL 3 hook at 0x0C0CBBA2 + new display mode; sidecar file; resampling copy routines + fill change.
3. First hardware test: version-display-only change.
4. Mute groups: voice start is 0x0C0963C0; still need the voice stop routine.

## Build 7 (test7, MD5 713010d0091128adfefb7ac004e53587, 2,700 program bytes changed)
- **Factory test mode removed for code space.** Boot (0x0C0D1840..) calls detect 0x0C0BFDE0 (FuncTest/LineTest/
  ServTest file on card or key combos 26+29+1 / 26+29+3) -> kind; if kind != 0: set kind (0x0C0BFC10) and
  setpage(0x0C0BFC40() = slot 44 = CPageTestVer). Test pages (slots 44..57, objects 0x0CDAFC9C..) navigate only
  among themselves (0x0C0BF800 table 0x0C138AE4, next/prev 0x0C0BF874/0x0C0BF8E0).
  Gate patch: 0x0C0D1852 `mov r0,r3` -> `mov #0,r3` (normal-boot value). 0x0C0BFC40 has no other reference.
- Page registry: 0x0CDAF84C[slot] filled by 0x0C0BEED4 from table 0x0C138C20[idx]; setpage = 0x0C0BE884(r5 = slot).
  Vtable entries are 8 bytes {s16 this_adj, pad, func}; typeinfo name = vtable[1]+4.
- Reused: CPageTestDevice method 5 (0x0C0CF568..0x0C0CF7B3, vtable slot 0x0C130318 -> 0x0C0CF7B8 `return 0`).
  0x0C0CF5D0..0x0C0CF61F is the page file's shared literal pool (kept); code at 0x0C0CF620..0x0C0CF7B3 (404 B).
  CPageVersion (power-on MARK version) is a separate class, unaffected.
- **PTN:** knob 125..127 (pos >= 61). pad_ratio(): table for pos < 61; else pattern tempo x10 (0x0C0A9E5C(2),
  plain halfword read) * 65536 / base_bpm, clamped 0x8000..0x17F00; unity if either <= 0.
  base_bpm: param40 == 1 (Time Modify unavailable) -> orig; sync 2 -> mod; sync 1 -> pattern; else orig.
  Computed in vari_fill per call (getters are lock-free reads of PAD_INFO / system store).
- Knob: 0..2 OFF, 3..61 -1200..0 c, 62..65 unity, 66..124 to +700 c, 125..127 PTN.
- **Display:** stock modes 6 (CTRL2: param42, or pattern tempo if page+12 == 1) and 7 (CTRL1: "---" if param40,
  OFF/PTN by sync, else param44). Hook shows value x pad_ratio when ratio != unity (mode 6 with page+12 == 1,
  and mode 7 texts stay stock). CTRL3 (mode 8): OFF / PTN text, else base_bpm x ratio.
- Layout: cave 536 B + RAM to 0x0C0807D0; dev 400/404 B (base_bpm, pad_ratio, show_bpm, vari_event).
- Host tests: resampler 560 cases bit-exact incl. PTN ratios; ASan/UBSan stress 300M samples with
  random tempos/params/stored bytes; persistence 16 checks; UI 31 checks (display numbers per mode/sync/PTN).

## Build 8 (test8, MD5 04e6a51c41f76fbd106337de58171668, 2,955 program bytes changed)
- PTN clamp widened to 0x4000..0x3FFFF (0.25x .. <4x). Resampler bound: after run() ph < UNITY + r, so the
  one-at-a-time path needs floor(ph) <= 4 -> any r < 4x is safe; frames_needed stays < 2^32 for F up to ~16k.
- Stock CTRL2 handler (0x0C0CBD14..): sets param42 (orig) then ALWAYS sets param44 (mod) = round(new orig),
  i.e. resets Time Modify on any CTRL2 move. CTRL1: mod = orig*(0.5 + 0.8*v/127), clamp 400..2000, rounded to 10.
- keep_tm (CPageTestMidi method 2, 0x0C0CDDDC, slot 0x0C12FD24 -> 0x0C0CE074 return 0): for CTRL2 with sync == 2
  and page+12 == 0, snapshot orig/mod, call Roland's dispatcher 0x0C0CC078 directly, then
  mod = clamp(round(mod*new_orig/orig), 400..2000) rounded to 10, R_SET(44) (0x0C082B98(id, v, pad, 0)), redisplay.
- vari_event moved to CPageTestLED method 5 (0x0C0CD838, slot 0x0C12F9A4 -> 0x0C0CD900 return 0).
- Host tests: resampler 760 cases bit-exact incl. 0.25x/0.3x/2.5x/3.9x; ASan stress 300M samples;
  UI 41 checks incl. CTRL2 keep-TM cases; persistence 16 checks.

## Build 9 (test9, MD5 da71ed7e5d998e8dedc2d1d83318ce17) — stock CTRL2 behaviour chosen
- keep_tm dropped (decision: since resetting TM is stock, CTRL2 resets varispeed too). vari_event (now in
  CPageTestMidi method 2): CTRL2 (id 1) in modes 5..8 with page+12 == 0 sets pos = 0 (normal) and dirty, then
  returns 0 so Roland's handler runs (it resets TM). CPageTestLED is untouched again. PTN range 0.25x..4x kept.

## Build 10 (test10, MD5 6e87ae195bd617ac36d70b667c7e0bc3) — PTN tempo source fixed
- Tester report: varispeed PTN did not lock to the pattern BPM. 0x0C0A9E5C(2) is NOT the pattern tempo (its store 0x0CDAA010
  is the effects parameter table; id 2 is only read by the TIME/BPM display when page+12 == 1).
- Stock Time Modify rate (per mille) = 0x0C09611C(PAD_INFO rec): sync 1 -> (0x0C0BAF24()/10)*1000/orig,
  sync 2 -> mod*1000/orig, then folded by octaves into 500..1300. 0x0C0BAF24 = u16 @(6, *0x0C13B230) =
  pattern tempo x100 (default 10200; valid 4000..20000).
- base_bpm now = orig * tm_rate / 1000 (tm_rate in CPageTestLED method 5, re-reused); PTN ratio = tempo100<<16 /
  (base*10), clamp 0.25x..4x; TM PTN -> varispeed PTN = unity. Guards: tempo/base outside 1..30000 -> unity.
- Host tests: UI 44 checks incl. half-time fold and 0.01 BPM precision; resampler 760; stress; persistence.

## Build 11 (test11, MD5 4000340f464174bb2894e6671628888c) — TIME/BPM LED
- Tester report: CTRL 3 turned the TIME/BPM light off. LED update 0x0C0CBEFC (called from the page refresh 0x0C0CC0D8 and
  0x0C0CBFD0): mode 1..4 -> LED 21 on; mode 5..7 -> LED 22 (TIME/BPM) on; else both off -> our mode 8 = off.
  Patch 0x0C0CBF0C `mov #6,r4` -> `mov #7,r4` (modes 5..8). One byte vs test 10. LED set = 0x0C0AD8F4(id, on).

## Full review (build 12, test12 MD5 878d193fe1be766c5fdb18396fe8e71a)
- Inventory: every changed byte vs stock maps to a planned patch (29 regions, 2,924 bytes); checksum/tag OK.
- Mode 8 audit (CPageSmplEdit vtable 0x0C12F3F8: enter 0x0C0CBF58, leave 0x0C0CBFE0, event, refresh 0x0C0CC0D8).
  Mode readers: display (bounded), button handler 0x0C0CB738 and dispatcher (only see 7, ev_stub maps 8->7 first),
  enter (keeps mode; key 39 -> 1, key 40 -> 5), LED (fixed in 11), refresh (pattern-mode auto exit when
  page+12 == 1 and modes 5..7) -> patched 0x0C0CC0E6 `mov #2,r4` -> `mov #3,r4`. page+12 = global flag *0x0CE327C8.
- Card writes: loop 0x0C0EF458 calls r11 (our tick_stub, was 0x0C08A700 auto-save) and r10 0x0C082EFC only while
  *0x0C138F40 != 0; pages with card work disable it (0x0C0EF4AC, 21 callers) and re-enable (0x0C0EF4BC, 30).
  So VARISPD.BIN I/O runs under exactly the same gate, task and lock 14 as Roland's PAD_INFO auto-save.
- Audio: stack (-fstack-usage) vari_fill 124, run 32, fetch 32, pad_ratio 12, base_bpm 16, tm_rate 4; PTN lookups
  return before Roland's fill is called, so max depth is unchanged from the hardware-tested builds. All divisors
  non-zero; products bounded (t<<16 with t <= 30000; tm_rate target*1000 with target <= 30000).
- Number display 0x0C0CFFF8 uses (n/100)%100 as a digit index: n >= 1000 would show a garbage glyph (no crash);
  our values stay < 1000 for orig <= 200 BPM (max CTRL 2 with PTN = 2 x pattern tempo).
- Known limitations (not fixed): 1-byte sample fingerprint can collide (~1/255) -> a re-recorded pad may inherit
  another pad's speed; CTRL 3 also works on the TIME/BPM page in pattern mode (stock CTRL 1 is inactive there).

## Build 13 (test13, MD5 05f576c1b53c919e7c12c01aead09bb0) — CTRL 3 view persists
- Tester report: after CTRL 3 stops, display returns to "OFF" (mode 7 TM view). Mode 7 is only set by Roland's CTRL1 handler
  (0x0C0CBBB0) and by our hook's 8->7 on every non-CTRL3 event; the UI loop (0x0C0D1A4E) calls page refresh
  (slot 5 -> display) every other timer tick, which then draws mode 7.
- Event sources: knobs 0x0C0AD538 (ids 0..2, hysteresis, direct dispatch via 0x0C0BDEE0, positions in
  0x0CDAA428[id] s16); buttons via queue 0x0C0BE0C0 / pump 0x0C0BDFC8 (type 0). Button handler 0x0C0CB738 only
  differs between modes 7 and 8 for id 40 (0x0C0CB8A0: modes 5..7 -> "EDT", mode 6; else exit).
- vari_event now: CTRL3 -> ctrl3() (LED region): set pos, snapshot KNOBPOS[0..1], mode 8, show. CTRL1/CTRL2 in mode 8:
  |val - snapshot| <= 2 -> swallowed (return 1); else mode 7 and to Roland (CTRL2 also resets speed). Type 0 id 40
  in mode 8 -> mode 7. All other events keep mode 8 and go to Roland. vari_snap s16[2] in .vbss.
- Host tests: UI 52 checks; resampler 760; stress; persistence 16.

## Decision: PTN and mark edits (after build 13)
- Moving the start mark (param 31 setter 0x0C08B04A -> 0x0C08AF14) re-estimates orig BPM from the marked length via
  0x0C08F13C (closest to the old BPM, 40..200) and sets mod = orig. PTN (pattern / base BPM) therefore re-fits the
  marked region, same rule as stock Time Modify PTN. Decided to leave this as is (options offered: CTRL 2 keeps
  PTN; freeze the reference BPM when PTN is set).

## Release v2.00 (= build 13 + version screen)
- Name "SP-404SX/A EXTRAfirmware", release v2.00 (a full new version over Roland's 1.07).
- Version screen: Roland's text routine 0x0C0F112C builds the digits from the number 107 (0x0C0F1128, also read
  only by the removed CPageTestVer page) by adding '0'. Only the text is changed: ones digit `add #48` at
  0x0C0F1142 -> `add #41` (7 -> '0'), hundreds digit `add #48` at 0x0C0F116C -> `add #49` (1 -> '2'): "2.00".
  Same mechanism as test build 1 (0x0C0F116C +'0' -> +'D', shown as E.01 on hardware). Two bytes vs build 13.
- Output SHA-256 3093b7e677e285e4dd1f612d9f9ccc0df6e364998bc79f1ec52df0b9c11a7ffd, MD5 9e9afbcef474e57e49d2f3d53eea403c.
