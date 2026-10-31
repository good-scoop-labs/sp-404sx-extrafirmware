# Changelog

## v2.00

First public release, a full new version over Roland's system 1.07, for the SP-404SX and SP-404A.
The version screen shows `2.00`. Output `sp404sx_up.bin`
SHA-256 `3093b7e677e285e4dd1f612d9f9ccc0df6e364998bc79f1ec52df0b9c11a7ffd`.

- **Varispeed on CTRL 3** (TIME/BPM page): per-pad tape-style speed. Fully left OFF, −12 to +7 semitones,
  fully right PTN.
- **PTN** follows the pattern tempo live, 0.25× to 4×, the same tempo Roland's Time Modify PTN uses.
- The display shows the BPM you hear on CTRL 3, CTRL 1 and CTRL 2; the CTRL 3 view stays on screen and the
  TIME/BPM button stays lit.
- Works with Time Modify; CTRL 2 resets varispeed as it resets Time Modify.
- Speeds saved on the card (`VARISPD.BIN`) and following the sample on copy and exchange.
- Version screen (hold MARK at power-on) shows `2.00`.
- Roland's factory hardware-test mode removed to make room.
- Web patcher (GitHub Pages, `docs/index.html`) and Python patcher (`patch.py`) with identical results.

Development history (internal test builds 1 to 13) is in [docs/DEV_NOTES.md](docs/DEV_NOTES.md).
