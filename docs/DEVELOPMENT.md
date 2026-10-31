# Development

## How it works

The SP-404SX runs a Renesas SH-2A (big-endian). Roland's update file `sp404sx_up.bin` is an LZSS-compressed
program image (757,748 bytes, loaded at `0x0C080000`) with a 16-bit checksum and a version tag; the
SP-404SX's own updater copies it to flash. EXTRAfirmware adds code to that image and hooks a few places:

| Area | What |
|---|---|
| `0x0C080400`–`0x0C0807FF` | Free 1 KB above vector 255: entry stubs, TIME/BPM page UI, RAM (per-pad speeds, per-voice resampler state) |
| `0x0C089EF4`–`0x0C08A35F` | Roland's hidden PAD_INFO debug dump (reclaimed): resampler and fill wrapper |
| `0x0C0EB5A0`, `0x0C0A9558` | Two unreferenced Roland routines: persistence code; ratio table and file path |
| Factory test pages | Test mode disabled at boot; methods of CPageTestDevice / Midi / LED reused for code |
| CPageSmplEdit (TIME/BPM page) | Event and display hooks; light and refresh mode ranges include the CTRL 3 view |
| 10 CSmplCharge vtables | `fill()` wrapped: asks Roland's fill for `ratio ×` the frames and resamples them (linear interpolation) |
| Version text (`0x0C0F1142`, `0x0C0F116C`) | Version screen shows `2.00` |
| PAD_INFO load / 100 ms auto-save | Load and save `VARISPD.BIN` alongside Roland's pad settings |

`src/tools/build.py` documents every patch site; `docs/DEV_NOTES.md` has the reverse-engineering log.

## Layout

```
CLAUDE.md                     working rules and context for AI assistants (read first)
Dockerfile                    Linux build environment (SH cross compiler, Python, Node)
patch.py                      user patcher (Python, no packages)
docs/index.html               web patcher (GitHub Pages)
docs/patcher-core.js          patch logic shared by the page and the Node check
docs/extrafirmware-patch.js   release patch data (generated)
src/firmware/                 vari.c (C), stubs.S (entry stubs), link.ld, gen_ratio.py (knob table)
src/tools/build.py            builds sp404sx_up.bin from the official file and src/firmware, with safety checks
src/tools/sp404fw.py          update container: unpack, pack, minimal-change repack (splice)
src/tools/make_patch_data.py  writes docs/extrafirmware-patch.js from a build
src/tools/check_web_patch.js  runs the web patcher's code in Node and compares results
src/tools/check_web_page.js   optional browser test of the page (Playwright)
src/tools/sh.py, deadcode.py  research tools (disassembly, unreferenced-code finder; need capstone)
src/tests/                    host tests for the C code (run_tests.sh)
official/                     put Roland's sp404sx_up.bin here (gitignored)
```

## Setup (PyCharm or any editor)

- Python 3.8+ interpreter, no packages needed except `capstone` for the research tools (`pip install capstone`).
- Mark `src/tools` as a Sources Root (the scripts import `sp404fw` from there).
- Put Roland's official `sp404sx_up.bin` in `official/` (gitignored).
- The host tests need a C compiler (gcc/clang); the build needs the SH cross compiler, which is Linux-only:
  use Linux, WSL on Windows, or the `Dockerfile` on macOS/Windows.

## Building

Needs Python 3.8+ and an SH cross toolchain. On Ubuntu 24.04 (or inside the `Dockerfile` image):

```
sudo apt install gcc-sh4-linux-gnu binutils-sh4-linux-gnu
python3 src/tools/build.py official/sp404sx_up.bin -o out/sp404sx_up.bin
```

The code is compiled for SH-4 without FPU (`-m4-nofpu -mb -Os`), and every generated instruction is checked
against an SH-2A allow-list. Release v2.00 was built with GCC 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04); another
compiler version can produce a different (but equally valid) file, so the release is defined by the patch
data and its SHA-256, not by rebuilding.

`build.py` refuses anything but the official 1.07 file (MD5 `96a4e2622738acd48cbbbfc667ebcfe5`) and checks,
before writing:
- each patch site holds the expected stock bytes;
- nothing in Roland's code branches into, points into or loads constants from the reused areas;
- every changed byte lies inside a planned patch area;
- the file re-decodes to the new image and its checksum and tag are valid.

The packer keeps Roland's compressed stream and rewrites only the tokens that touch changed bytes, so the
file differs from Roland's as little as possible.

## Tests

```
sh src/tests/run_tests.sh                                            # host tests (gcc; ASan/UBSan)
node src/tools/check_web_patch.js official/sp404sx_up.bin out/sp404sx_up.bin  # web patcher vs build
python3 patch.py official/sp404sx_up.bin -o /tmp/p/sp404sx_up.bin && cmp /tmp/p/sp404sx_up.bin out/sp404sx_up.bin
```

The host tests compile `vari.c` for the PC with Roland's functions replaced by mocks:
`test_resampler` (bit-exact against a reference), `test_stress` (randomized, sanitizers), `test_persist`
(`VARISPD.BIN` cases), `test_ui` (TIME/BPM page logic).

## Releasing

1. Build: `python3 src/tools/build.py official/sp404sx_up.bin -o out/sp404sx_up.bin`.
2. Test the file on hardware.
3. Set `RELEASE` in `src/tools/make_patch_data.py`, then
   `python3 src/tools/make_patch_data.py official/sp404sx_up.bin out/sp404sx_up.bin`.
4. Run the three checks above (and `node src/tools/check_web_page.js official/sp404sx_up.bin` if Playwright
   is installed).
5. Update `CHANGELOG.md`, the SHA-256 in `README.md` and `docs/INSTALL.md`, and `docs/STATUS.md`.
6. Commit and tag (`git tag -a v2.00`).

Never commit `sp404sx_up.bin` files (stock or built): they are Roland's code. `.gitignore` excludes them.

## GitHub Pages

Settings → Pages → Source: *Deploy from a branch*, branch `main`, folder `/docs`. The page links back to
the repository automatically when served from `<user>.github.io/<repo>/`.
