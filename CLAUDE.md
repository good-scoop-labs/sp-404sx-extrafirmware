# CLAUDE.md: working on SP-404SX/A EXTRAfirmware

Context for AI assistants (and humans) continuing this project. Read `docs/DEVELOPMENT.md` for how it works
and `docs/DEV_NOTES.md` for the full reverse-engineering log (addresses, Roland functions, every decision).

## What this is

Custom firmware for the Roland SP-404SX / SP-404A (identical firmware) built on Roland's system 1.07
(`sp404sx_up.bin`, MD5 `96a4e2622738acd48cbbbfc667ebcfe5`). Current release **v2.00**: per-pad tape-style
varispeed on CTRL 3 of the TIME/BPM page, PTN (follow pattern tempo), BPM displays, `VARISPD.BIN`
persistence, version screen `2.00`. CPU: SH-2A big-endian; image loaded at `0x0C080000`.

## Ground rules

- **Never commit or publish `sp404sx_up.bin` files** (stock or built) or disassembly listings of Roland code.
  Users make the firmware themselves with `patch.py` or the web page. The patch data must only contain
  bytes we change (`make_patch_data.py` guarantees this).
- **Hardware safety:** the owner does not solder; there is no JTAG recovery. Recovery relies on Roland's card
  updater, which runs inside the application. So: keep start-up changes minimal, never break the boot path
  or the card update task, always keep the stock restore path working, and change one thing per test build.
- **Every change goes through `src/tools/build.py`'s checks** (stock bytes at each patch site; no Roland branch,
  pointer or constant load into reused areas; diff confined to planned areas; SH-2A instruction allow-list).
  When reusing new code space, prove it is unreferenced the same way (`sh.py`, `deadcode.py`) and add it to the
  checks. A past bug (internal build 4) came from a Roland routine loading constants from a reclaimed area.
- **Roland's ABI keeps MACH/MACL across calls**; entry stubs from Roland code must save them (see `stubs.S`).
- Calls from the audio path (`vari_fill`) must only use lock-free Roland getters; card I/O happens only in
  `vari_tick` (Roland's auto-save loop, gated like Roland's own PAD_INFO save, lock 14).
- Space is tight: every region is nearly full (`build.py` prints usage). Check `docs/DEV_NOTES.md` for which
  factory test page methods are already reused and which candidates remain.

## Workflow

1. Change `src/firmware/*` (or `build.py` for patch sites).
2. `sh src/tests/run_tests.sh` (update/add host tests in `src/tests` for new logic).
3. `python3 src/tools/build.py official/sp404sx_up.bin -o out/sp404sx_up.bin` (Linux with
   `gcc-sh4-linux-gnu`, or the `Dockerfile`).
4. Flash `out/sp404sx_up.bin` on hardware, test, and log the result in `docs/DEV_NOTES.md`.
5. For a release: `make_patch_data.py`, then `node src/tools/check_web_patch.js official/sp404sx_up.bin out/sp404sx_up.bin`,
   compare `patch.py` output, optionally `src/tools/check_web_page.js`; update CHANGELOG, README/INSTALL hashes,
   STATUS. Release names: "SP-404SX/A EXTRAfirmware vX.YY", repo `sp404sx-extrafirmware`.

## Owner preferences

- Staged test builds with a short README of what to test; the stock file always alongside.
- Varispeed and Time Modify stack; CTRL 2 resets both (stock behaviour extended).
- PTN re-fitting when marks move (Roland's BPM recalculation) is accepted as is.
- Docs say SP-404SX and SP-404A work identically.
