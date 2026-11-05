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

## Chromatic mode (test build C1, after v2.00)
Design: the 12 pads of the current bank play one source pad at 0..+11 semitones (pad 1 = own pitch), on top of
its varispeed. Live mode only (nothing on the card, patterns untouched); "spread to bank" left for later.

Note path, mapped:
- Every note is a 4-byte MIDI-style event {+0 note, +1 source kind, +2 channel (bank half), +3 velocity},
  queued (push 0x0C09290C, pop 0x0C092934) and handled by 0x0C089330 (from the loop 0x0C08F708). pad <-> note:
  0x0C0F15F0 / 0x0C0F1610 (pads 0..59 = ch 0 notes 47..106, pads 60..119 = ch 1 notes 47..106). Panel pad
  buttons arrive as raw 0xA0 events and are converted with the current bank (param 53): pad = bank*12 + n.
- Kinds: 6 = panel pads (play_pad selects the current pad for kind 6), 3 = MIDI in (channel minus param 2,
  the MIDI channel). Note 45 with kind 6 = SUB PAD (replays the last pad, 0x0C131BA0).
- 0x0C0892AC(kind, pad, vel): vel 0 -> release 0x0C093DE4(req, pad) -> 0x0C084B48(pad) (stops it if the pad's
  gate is on); else play request 0x0C093D28 -> **play_pad 0x0C084974(kind, pad, vel)**: stops the pad's running
  voice (0x0C0842CC / 0x0C084400) => one voice per pad; allocates voice(s) (0x0C084874, list *0x0CCB7ABC) and
  starts them via 0x0C093FB8(voice, pad, x). Voice objects 0x0CCAE0C8 + 24*i (i < 12): +0 feeder, +8 player,
  +22 s8 pad (-1 = none), +23 index (= feeder +24 = the voice index vari_fill already uses).
- The only pointers: play_pad at literal 0x0C093F44 (loaded only at 0x0C093DC8), 0x0C084B48 at literal
  0x0C093F38 (loaded only at 0x0C093DF2). Both swapped to stubs (play_stub, rel_stub).

UI, mapped:
- Pages: page manager 0x0CDAF934, +0 = page shown now (object, +0 = vtable). CPagePlay vtable 0x0C12E044
  (event 0x0C0C5384 -> buttons 0x0C0C4C68), CPageSmplEdit 0x0C12F3F8, CPagePtnRec (pattern recording) 0x0C12EF24.
  Event {+0 type (0 button, 1 knob), +4 id, +5 value}; buttons: value != 0 = press. Button ids 1..12 = pads of
  the current bank, 40 = TIME/BPM.
- 0x0C0BEA7C(id) = 1 while button id is held (Roland uses it for its own combinations, e.g. ids 20, 34, 44).
- Play page messages are drawn straight from the button handler (0x0C0D0150(str), as P.ON/P.OF at
  0x0C12E034); the play page refresh does not redraw the display. The TIME/BPM page redraws on every refresh.

Implementation (vari.c "chromatic mode"):
- Gesture: TIME/BPM held + pad press (on the play page through a new CPagePlay event hook, slot 0x0C12E060 ->
  pev_stub; on the TIME/BPM page in vari_event). Same pad as the source = off, other pad = new source.
  C.ON / C.OF on the play page (Roland's message call); on the TIME/BPM page while TIME/BPM is held.
- chr_play: kinds 6 and 3, only while the shown page is CPagePlay or CPageSmplEdit and not pattern mode
  (*0x0CE327C8): a pad of the current bank (key 0..11) plays the source instead; afterwards every voice whose
  +22 is the played pad gets vkey = key + 1 (0 for a stock start, so stale tags are cleared).
- chr_release: the sounding key releases the source; other keys do nothing; everything else stock.
- vari_fill: ratio = chr_ratio(voice, pad, pad_ratio(pad)) = pad speed x 2^(key/12) (Q16 table), clamped to
  < 4x. Root key and non-chromatic voices return the pad speed unchanged, so normal speed stays Roland's fill.
- TIME/BPM page while on: plain pad presses are swallowed (they are keys), so the page keeps showing the
  source and CTRL 3 keeps setting the source's varispeed.
- Code/data: 11 more factory test page methods (event / method 5 slots, the same kind already reused), each
  with no branch in, no constant load from outside and only its own vtable pointer; slots pointed at the same
  page's `return 0` method. State in CPageTestCompleted method 2 (SDRAM, zero in the image -> power-on off).
- Host tests: test_chroma (34 checks: gesture, keys, banks, kinds, page/pattern guards, release, pitch table,
  clamps, fill rate, root key bit-exact path).
- Not verified on hardware: pattern-mode flag meaning (*0x0CE327C8, from build 12), kind numbers in practice,
  the held-button query during a page switch, display texts.
- Layout: vari_show moved out of the free area (CPageTestVer method 5) and vari_save out of the debug dump area
  (CPageTestLine method 2) to make room; vari_ev (free area) wraps vari_event so the full CPageTestMidi area
  is unchanged in size; chr_ok in the free area. Every area now has a few bytes or more left (build.py prints usage).
- build.py: the new areas count as ours in the leftover-pointer check; the three hook sites may point at their
  stubs only; after patching, literal-pool words of the stock image stay data (0x0C0CD700 is a literal whose only
  loader was in the reused CPageTestExtract method 2, and otherwise decoded as a load into our area).
- Toolchain check: GCC 13.3.0 built on macOS (binutils 2.42, `--with-multilib-list=m4,m4-nofpu`, system zlib)
  rebuilds v2.00 byte-identical (SHA-256 3093b7e6...). Test build C1: MD5 fb34470b82bc73857d41d4bf0bda53e5,
  3,959 program bytes changed, all checks passed. Not yet tested on hardware.

## Test build C2 (MD5 3f3bdbff5447d1ec6f8b641a15627e4b) — C1 hardware report fixed
- C1 report: C.ON shown, but only some pads played, and their pitches seemed random/inconsistent.
- Cause 1: the play request 0x0C093D28 drops a pad whose sample isn't loaded (0x0C08A848(head buffer of the
  pressed pad): 1 if +90152 >= 0) before play_pad, so empty keys never reached chr_play. Fix: hook the note
  handler's own literal for the play request (0x0C08948C, loaded only at 0x0C0892DE; the two other literals of
  0x0C093D28 stay stock) -> req_stub -> chr_request, which remaps keys before that check.
- Cause 2: voice start 0x0C0963AC fills the first two 4096-frame blocks (~0.19 s) through fill() synchronously,
  inside play_pad, but C1 tagged the voice only after play_pad returned: each attack used the voice's previous
  tag. Fix: chr_request sets `pend` (key + 1) before the request; the play_pad hook sets the source's `cur` from
  it just before Roland starts the voice (0 for any other start of the source); vari_fill pitches every voice
  of the source by `cur`. One value is enough because a pad plays on one voice; the per-voice tags and the
  voice table read (0x0CCAE0C8 +22) are gone.
- Host mocks now model both (request drops unloaded pads; voice start renders its first block at the speed
  vari_fill computes then); test_chroma checks all 12 keys incl. empty pads and each note's attack pitch.

## Test build C3 (MD5 c5018c9b15b822e1a03b6abeb29a9457) — loop pads
- C2 report: all 12 keys play, semitones steady. With LOOP on, every second press stopped the note, and looping
  felt inconsistent.
- play_pad: if the pad is playing (0x0C0842CC: a voice slot of the pad in state 1/2) it runs stop(pad, 0)
  0x0C084400, 0x0C0845AC(7), 0x0C0845B8() (voice list upkeep), then returns if gate (+19) is off and loop
  (+18) is on: the stock "press a loop pad again to stop". Every key is the source pad, so keys alternated.
- Fix in chr_play (the play_pad hook, so after Roland's busy/loaded checks): when the press is a key (pend) other
  than the one sounding (cur) and the source is playing, run that same stop sequence first; play_pad then sees
  a stopped pad and starts the new note. The sounding key again goes to Roland unchanged (loop stops).
- Loop restarts were checked: the sample player task's restarts (0x0C093E00 / 0x0C093E48 -> voice methods
  0x0C094130 / 0x0C0941F4) and the new-sample auto-play (0x0C08B290) do not go through play_pad, so they
  keep the note's pitch. If looping still sounds off in C3, it is something else (asked for details).
- chr_play grew to 124 B: moved to CPageTestVer method 0 (0x0C0CEC38, slot 0x0C130124 -> 0x0C0CEF48),
  passing the same checks as the other reused methods.

## Test build C4 (MD5 b6e4f6ebe0408794a3094d4c9c3a0faa) — resampling
- Report: chromatic keys stop working on the resample screen, and the gesture doesn't work there (chr_ok and the
  gesture hooks only covered CPagePlay / CPageSmplEdit; resampling is CPageReSmpling, vtable 0x0C12F270).
- CPageReSmpling: event 0x0C0CAF40 (slot 0x0C12F28C) -> buttons 0x0C0CA7C4. Page +8 = state: 1..2 choosing the
  destination (pad button: if the pad is empty, 0x0C0903D4, it becomes the destination), 3 "---", 4 RDY, 5 REC
  (texts at 0x0C12F23C...; display code 0x0C0CA64E). Pad buttons only act in states 1..2; TIME/BPM (id 40) is
  ignored on this page in every state, so the hold-TIME/BPM gesture is safe there.
- chr_ok: also CPageReSmpling with state >= 3, so choosing the destination stays stock (otherwise empty keys
  would both play and move the destination).
- Gesture on the resample page: slot 0x0C12F28C -> rsm_stub. pev_stub / rsm_stub share chr_page_event(page, e,
  Roland's handler, show); the resample page passes show = 0 so RDY / REC is never overwritten.

## Test build C5 (MD5 cda07ba1db88e4e653dca75c2dbb517f) — five octaves, CANCEL exits
- Requested: CANCEL leaves chromatic mode (a separate mode), and banks A..E as a keyboard with bank C pad 1 at
  the source's own pitch (A/B lower, D/E higher). Bank E needs up to +35 st = 7.55x; owner chose to extend
  the engine to 8x. Automatic jump to bank C not done (bank select 0x0C0D0B10 is tangled with A/F toggling
  and other state); the bank stays where it is.
- Buttons: switch test order table 0x0C12FFC4 + CPageTestSw texts: 34 = CANCEL ("CAN", 0x0C130030), 35 = REMAIN.
  Bank buttons A..E = ids 25..29 (play page: 0x0C0D0B10(id - 25)). On the play page CANCEL is only a modifier
  (Roland checks it held: 0x0C0BEA7C(34)); on the resample page id 34 has its own action (0x0C0CAA72).
- Keys = pads 0..59 (banks A..E), semitones = pad - 24; banks F..J stock. MIDI: base channel notes 47..106 =
  pads 0..59, so note 71 = root, one contiguous 60-note range.
- CANCEL: a tap (press, release, no other button press in between) on the play / TIME/BPM page turns the mode
  off (C.OF); the resample page hook ignores CANCEL (show = 0) so it keeps cancelling the resample. TIME/BPM +
  pad now only selects the source (the source again keeps it on).
- chr_ratio: semitones -> octave shift (exact) x 2^(s/12) table, no division (GCC used dmulu.l for / 12 and
  % 12; not on the allow-list, so a subtraction loop). Worst error 0.05 cent over all 60 keys. Stored key
  bytes above 60 are ignored (stress test found the overflow). Clamp V_MAX just under 8x; PTN keeps R_MAX 4x.
- Resampler: the one-at-a-time path consumes floor(ph) <= floor(r) + 1 frames; tL/tR 4 -> 8 frames supports
  r < 8x (ASan: the old 4-frame buffer overflows above 4x, confirmed with the new cases). frames_needed stays
  < 2^32 (ph + 4095 r < 2^16 + 2^31). vari_fill frame 108 B (+16).
- Host tests: resampler 1080 cases bit-exact (new: 4x..7.9x and 1/8x); stress now randomizes chromatic keys and
  stored bytes (420M source frames for 307M outputs); test_chroma covers the 60-key layout, F..J, CANCEL taps
  and combinations, resample CANCEL, clamps, every key x speed.
- One more reused method: CPageTestDevice method 0 (0x0C0CF49C, slot 0x0C1302F0 -> 0x0C0CF7B8).
- To watch on hardware: SD read speed with long samples at 6..7.5x (bank E).

## Test build C6 (MD5 ba79701d3039baa55de0081b2209c019) — empty banks, SHIFT combination, C.ON on screen
- C5 report: banks C..E didn't play from the panel (MIDI did); in fact any bank without samples. The flag
  *0x0CE327C8 (taken from build 12 as "pattern mode") is "no current pad": set by the parameter code when a bank
  is selected (0x0C082B80 / 0x0C082C7C -> setter 0x0C0F1A3C(1)), cleared by play_pad for panel presses
  (0x0C0F1A3C(0)); getter 0x0C0F1A34 (the TIME/BPM page shows the pattern tempo in that state). chr_ok refused
  keys while it was set: in a bank with samples the first press played its own pad and cleared it; in an empty
  bank Roland dropped the press, so it stayed set. Pattern mode is a page of its own (CPagePtnSel, slot 18;
  pattern recording CPagePtnRec), so chr_page() now decides on the page only.
- Page slots (registry 0x0CDAF84C filled at 0x0C0BEF24 from 0x0C138C20): 1 Play, 2 MFXSelect, 3 TrigLevel,
  4 Smpling, 5 ReSmpling, 6 SmplEdit, 8 PadDel, 9 PadDelAll, 10 PadExch, 11 PadCopy, 13 SdFormat, 14 Remain,
  18 PtnSel.
- SHIFT = button 20: no action of its own on the play page (handler table 0x0C0C4EF4, index id - 15), only
  checked held (with 39 / 40: BWD / FWD while *0x0C138F5C is set; with 44). Not confirmed on hardware.
- Combination SHIFT + TIME/BPM + pad (outside FWD mode): the TIME/BPM press is kept from Roland (chr_eat: play
  page hook before Roland's handler, vari_ev on the TIME/BPM page); the pad selects the source; chr_request
  drops panel notes and chr_release ignores releases while SHIFT and TIME/BPM are held on a chromatic page.
- C.ON: the play page's idle display is blank (enter clears it, 0x0C0CFF34); Roland draws values / messages
  over it. New hooks: CPagePlay enter (slot 0x0C12E050 -> pen_stub) and refresh (slot 0x0C12E078 -> pref_stub;
  the UI loop calls vtable +52 of the shown page every other tick, 0x0C0BEA48). chr_st.show is set on enter and
  by every chromatic note (note task); the refresh (UI task) draws C.ON with Roland's message call.
- More reused methods (same checks): CPageTestMute method 3, CPageTestSw method 0, CPageTestCompleted method 0,
  CPageTestVolume method 5. Stubs split in two groups.

## Test build E1 (MD5 55170ff23b200b76bcb328a3aa7c89cc) — chromatic mode per bank, menu, MIDI channels, saving (replaces the C builds' design)
- Owner's redesign: SHIFT + TIME/BPM turns the last pad played into its bank's chromatic source and opens a
  chromatic menu (TIME/BPM blinking); CTRL 1 MIDI channel (lowest free by default; only free ones offered;
  fully left OFF), CTRL 2 mono / poly, CTRL 3 octave -2..+2; any number of banks; MIDI note 60 = own pitch,
  silent above B6 (owner's choice), octave knob for pads only; saved on the card. Poly is left for E2 (one
  risky change per build): stored and shown, plays mono.
- Menu = TIME/BPM page mode 9. The play page event hook only flags SHIFT + TIME/BPM (Roland still opens the
  page); the page's first display (our hook) opens the menu. In mode 9 Roland ignores the knobs (dispatcher
  handles modes 1..7) and leaves the page on TIME/BPM (handler 0x0C0CB8A0: modes other than 5..7 -> exit).
  LED: the TIME/BPM page refresh draws the display (0x0C0CC10C) and then the LEDs (0x0C0CC110, 0x0C0CBEFC); the
  LED routine's path for modes outside 1..8 now writes 2 (blink) to the TIME/BPM LED (0x0C0CBF12 e500 -> e502).
  LED driver: per-LED patterns in a multi-plane buffer scanned on a global clock (0x0C0ADEF8).
- Display font (0x0C0AE9E4, tables at 0x0C12CA94.. for digits, 0x0C12CA8F + 'A'): every letter has a glyph.
- MIDI in: the receiver posts note events for every channel (0x0C0F2670..: note <= 127 and MIDI on); the note
  handler 0x0C089330 subtracts the SP's channel (param 2) and drops channels other than it and the next. Hook:
  the event loop's literal for it (0x0C08F778, the loop calls it with r4 = event, result ignored) ->
  note_stub -> chr_note: kind 3 on a chromatic bank's channel plays its source through Roland's play request
  (object 0x0CCAE0C4) with the pitch pending; note off (velocity 0 or 255) releases it via 0x0C084B48.
  Pattern playback posts kind 2 (sequencer 0x0C0BBB48).
- State: chr_tab (CHROMA.BIN: "CHR1" + 10 x {src + 1, channel, mode: octave + 2 in bits 0..2, poly bit 3,
  fingerprint}), chr_st (per bank: sounding key / MIDI note, current pitch; pending pitch; menu). Mono pitch
  per source; the per-voice fill path is unchanged. chr_check turns off entries that don't fit or whose
  sample fingerprint changed (load and every tick). chr_tick wraps vari_tick (tick_stub literal) for load/save.
- Code space (E1 needs ~2.9 KB): more factory test page methods, with new build checks.
  * Pool-sharing methods (chrx/chry/chrz: CPageTestExtract / Midi / Mute method 5): constant loads into them
    only from their page's earlier methods (POOL_SPANS); each span + area has no branch in from outside, no
    pointer in except from test page vtables, and no test vtable load (constructors run at boot). Rejected:
    CPageTestVer m3, CPageTestVolume m3, CPageTestSw m2 (loaders after a constructor, in code not proven dead).
  * First methods starting right after a literal pool word (chrs/chru/chrv): start check accepts a stock pool
    word before the prologue.
  * chrw = 0x0C0CE860..0x0C0CEC34 (980 B): helper functions after the CPageTestSw constructor (0x0C0CE838..,
    live via the init table at 0x0C131B04, kept), reached only by 6 branches from CPageTestVer methods (4 in
    chrl, already ours). The build checks exactly those sources and their span.
  * Every function in its own section; placement found with a small bin-packing search (link.ld).
  * Allow-list kept as is: GCC's dmulu.l (/ 12) and rotcl (bit packing) avoided by code shape (loops, mode
    layout with poly in bit 3).
- Host tests: test_chroma rewritten (78 checks: menu, knobs, channel lists, OFF, keys and octaves, pages,
  releases, MIDI notes / offs / range / other channels, two banks, loops, every pitch x speed, saving incl.
  random file contents, C.ON); resampler 1080 bit-exact (down to -60 semitones); stress with per-bank state.

## Test build E1b (MD5 884f1a72ddd2cce83cac3eee82175170) — MIDI chromatic while resampling
- E1 report: chromatic pads work while resampling, MIDI doesn't. The MIDI receiver (0x0C0F2528, r8 = event)
  posts a note only if 0x0C08FD70() == 1 (0x0C0F2676..7C); that returns 0 while *0x0CC1CB8C is set (sampler
  recording) or *0x0CCAFCF8 == 1, so stock ignores all MIDI notes while sampling / resampling.
- Hook: the receiver's own literal for it (0x0C0F2784, loaded only at 0x0C0F2676; the note handler's literal
  0x0C089480 stays stock) -> gate_stub (passes r8) -> chr_gate(event, gate): a kind-3 note on a chromatic bank's
  channel while the resample page is at RDY / REC (chr_page() == 2) gets through; everything else gets Roland's
  answer. Host tests: 10 gate checks.

## Test build E1c (MD5 1e7cc6b8ccfb3586c0ce9e1706621e64) — chromatic MIDI starts the resample
- E1b confirmed on hardware (MIDI chromatic plays while resampling). Report: at RDY only a pad press starts the
  resample, not a chromatic MIDI note.
- Resample start: Roland's note handler 0x0C089330 first calls 0x0C08FDDC(): if the sampler (0x0CC1CB88) is armed
  (+4 state 1, +8 1, +20 1) it runs 0x0C08FD98: sampler state -> 3 (recording) and message 0 to the UI queue
  0x0CDAF744 (0x0C0D1BA0); the UI loop (0x0C0D1AE0..) hands it to the page's method 5 (vtable +44), and the
  resample page's 0x0C0CAFFC moves RDY -> REC from the sampler's state (query 0x0C0901FC id 0 = *0x0CC1CB8C).
  So the first note event reaching the handler starts the resample. chr_note handled chromatic MIDI before
  Roland's handler and skipped it; it now calls 0x0C08FDDC first for every event on a chromatic channel.
- chr_show moved to chrw (chr_note grew). Host tests: 3 more checks.
- E1c confirmed on hardware (2026-11-02): a chromatic MIDI note at RDY starts the resample.

## Test build E1d (MD5 aa86410c1c0133386cb2f2428b8a5d32) — any MIDI note while resampling; CTRL 2 mono only
- Request: any MIDI note (not only chromatic channels) can start the resample; remove POL from CTRL 2 for now.
- Roland checks "MIDI notes allowed" (0x0C08FD70: 0 while the sampler runs) twice: in the MIDI receiver (literal
  0x0C0F2784, hooked in E1b) and in both note handlers for MIDI events (shared literal 0x0C089480, loaded at
  0x0C08930A in 0x0C0892F0 and 0x0C08935E in 0x0C089330, each right after a kind == 3 test, jsr/n @r3, no
  arguments). Opening only the first would start the recording (0x0C08FDDC runs first) but drop the note.
  Now: chr_gate lets any MIDI note through at resample RDY / REC, and chr_gate2 (gate2_stub on 0x0C089480)
  answers 1 there, Roland's answer elsewhere. Choosing the destination and sampling stay stock.
- CTRL 2 shows MON and keeps the poly bit clear (POL text removed).

## Test build E1e (MD5 5ea84e913f6b901b87230cd4b5139bf7) — menu exits on another pad when OFF
- Stock TIME/BPM page: a pad other than the one shown -> setpage(1) (0x0C0CB8C4), back to the main screen; the
  same pad does nothing. In the chromatic menu, pad presses now go to Roland when the menu's bank is OFF (so the
  same happens); while the bank is chromatic they stay keys (swallowed for the page).
- MIDI channel 2 question: the SP-404SX/A receives on its channel (param 2) for pads 1..60 and on the next one for
  pads 61..120 (0x0C089330: channel - param 2, 0x0C0F1610), so chr_used reserves both. Kept as is (owner didn't
  choose to free it).

## Test build v2.10a (MD5 e1f88bfa3dfdd68121ceb4cfa6ff52d7) — version 2.10, menu exit, page keys, HOLD
- Version: release name becomes v2.10. Version text routine 0x0C0F112C: digits of 107 at 0x0C0F116C (hundreds),
  0x0C0F115A (tens), 0x0C0F1142 (ones), each `add #48`: now +49, +49, +41 -> "2.10".
- Menu: Roland's TIME/BPM page handler takes SHIFT + TIME/BPM for itself, so in mode 9 chr_menu_ev leaves the page
  itself on any TIME/BPM press (setpage(1), 0x0C0BE884, as Roland's own exit at 0x0C0CB8C4).
- TIME/BPM page outside the menu (TIME/BPM views 5..8 and START/END/LEVEL views 1..4): a pad of a chromatic bank
  is swallowed (chr_padkey), so the page stays (Roland would setpage(1) for another pad). Notes play anyway.
- HOLD: hold state per pressed pad 0x0CE327DC[pad]; on press, 0x0C0F1D50 arms it (1, or 2 with HOLD on,
  *0x0CE327D8) only if R_GET(38 = GATE, pad) == 1; on release 0x0C0F1CB8 (state 2) latches it (3) and drops the
  release; pressing a latched pad turns into its release (0x0C0F1CE8 / 0x0C0F1D34). Keys had their own GATE (off,
  or empty pads), so only the source latched. Hook: that R_GET call's own literal 0x0C0F1DD4 (loaded once, at
  0x0C0F1D5A) -> hold_stub -> chr_holdget: keys answer with the source's GATE. Each key latches like a pad.
- Varispeed copy / exchange (reported not following): nothing in its saving path changed since v2.00 except that
  the tick goes through chr_tick (which calls vari_tick unchanged); the PAD_INFO pointer table 0x0C17B14C is never
  rewritten (only entries are), so the fingerprint reads the right pads. Asked the owner for details.

## Test build v2.10b (MD5 3dcd9d530a3ff3129301c02248b211be) — MARK keeps the chromatic note
- Report: MARK on a sounding chromatic note -> the sample continues at its own pitch until the next key.
- chr_play set cur = pend (0) for every non-key start of the source. MARK has Roland restart the pad itself
  (candidate: 0x0C08B290, which re-estimates the BPM from the marks and requests the pad again with kind 6, through
  the play request's own literal, not the note handler's). Key presses of a chromatic bank always come with a
  pending pitch (the source pad is a key of its own bank), so a kind-6 start without one on a chromatic page is
  Roland restarting the source: cur is kept. Other kinds (pattern playback = 2) and other pages: own pitch.
  The exact MARK path is not traced; the rule covers any such restart.

## Test build v2.10c (MD5 cc470b9ebb1bf00a55dfec7ad36ac864) — varispeed follows copy / exchange explicitly
- Report: after exchanging two pads at different speeds, each pad kept its own speed (same for copy). Cause: the
  v2.00 design moves speeds in resync by matching 1-byte fingerprints (data end, format, channels); identical
  samples (a copy and its source, or two copies) have equal fingerprints, so a swap or copy between them is
  invisible, and with several identical pads resync takes the first match's speed.
- Roland's path: copy page (CPagePadCopy, pad handler 0x0C0C31B4) confirms with 0x0C0904C0(src, dst), exchange page
  with 0x0C090464(a, b). Both set params 60 / 61 and post command 12 (copy) / 11 (exchange) to the file task
  (event 0xA4, byte 3 = command; switch 0x0C08F5B0). Job wrappers 0x0C088BB0 / 0x0C088C6C call the workers
  0x0C087A64 (copy) / 0x0C087DEC (exchange) through their own literals 0x0C088BF8 / 0x0C088CB4 (each loaded
  once, jsr/n; the workers are referenced from nowhere else), then post the result back (byte 2: 1 = done, 2 =
  failed; the worker returned 0 / non-0). The workers read params 60 / 61 themselves and save PAD_INFO.
- Hook: both literals -> xcopy_stub / xexch_stub -> vari_xop(worker, exch): reads the pads (params 60 / 61) and
  their speeds before the job, runs it, and on success sets dest = source speed (copy) or swaps (exchange), with
  fresh fingerprints and the save timer started. The speeds are taken before the job because resync (100 ms tick,
  other task) may see the half-done PAD_INFO. Chromatic banks are unchanged (a bank whose source sample changes
  still turns off).
- Space: vari_xop 172 B + vari_xset 48 B in chrw; chr_check moved from chrw to chry. Stubs in chrstubs2 (shared tail).

## Release v2.10 (= test build v2.10c, SHA-256 733885e5935d0c2c4c887f0446afa8e2546d2520223cec7ccbe9297c08066b54)
- Built with GCC 13.3.0 / binutils 2.42 built from source on macOS; byte-identical to the v2.10c file tested on
  hardware. Patch data regenerated (RELEASE = '2.10': 246 runs, 6784 bytes).
- Checks: host tests; patch.py output identical to the build; the web patcher's code (patcher-core.js + patch
  data) run in JavaScriptCore (no Node on this machine; same checks as check_web_patch.js): official file
  accepted, result byte-identical with the release hash, patched file recognised, modified input refused.

## After v2.10 — HOLD on every key (reported: only the source latches, keys stop on release, GATE on)
- Roland's HOLD layer 0x0C0F1818 (panel 0xA0 events, converted by 0x0C0F1738 with the current bank into the key's
  own note / channel; pad = 0x0C0F1610(ch, note)) arms and latches per pressed pad, i.e. per key; the v2.10a hook
  makes the arm's GATE lookup answer the source's GATE, so keys did arm and latch.
- Cause: release-all 0x0C0F1E94 (called from many page handlers, e.g. the play page at 0x0C0C4C4A, result = HOLD
  LED). Per bank (0..9, back to 0x0C0F1EDE) it takes m = 0x0C085074(bank) (bit n: pad bank*12 + n has a voice
  playing, from the voice list) and walks the 12 pads: a pad in states 2..4 whose bit is clear is released (release
  event, state 0); if the bit is set it is kept only if R_GET(38 = GATE, pad) == 1. A key never has a voice of its
  own (the source's voice plays it), so every latched key was released on the next pass.
- First attempt (hooking only the GATE lookup) did not help on hardware: keys never get past the bit test.
- Hooks: the mask literal 0x0C0F1FE0 (loaded once at 0x0C0F1EDE, `mov.l @(8,r15),r4; jsr @r3`) -> chr_playmask
  directly (no stub: release-all sets mach / macl only after this call, and Roland's mask function clobbers them
  anyway): on a chromatic bank with anything playing (only the source can play there), all 12 bits are set. The GATE
  literal 0x0C0F1FEC (loaded once at 0x0C0F1EF4, kept in macl; called at 0x0C0F1F26 / 1F8A / 200C / 2074 as
  `sts macl,r1; mov r8,r5; jsr @r1; mov #38,r4`) -> hold_stub -> chr_holdget, so keys answer the source's GATE (the
  stub saves mach / macl, which hold release-all's two function pointers there). The two other R_GET literals of
  this code ask for the bank (53, 0x0C0F1908) and the MIDI channel (2, 0x0C0F1730).
- One voice per bank: a key latched earlier stayed at state 3 after another note took the voice; its next press
  was turned into its release (ignored by chr_release, not the sounding key). chr_holdclr (chrz) clears state 3 of
  the bank's pads when a key or MIDI note starts the source (chr_play with a pending pitch).
- Bank off with a latched key: the source kept sounding (release-all then releases the key pad, which no longer maps
  to the source). chr_off (chrx, 28 B, tail call) releases the source (R_RELPAD) when CTRL 1 goes to OFF; CANCEL
  now goes through chr_knob(0, 0) too.
- Space: chrz 272 of 288 B, chrw 964 of 980 B, chrh 100 of 100 B (chr_playmask), chrx 464 of 464 B.
