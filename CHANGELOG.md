# Changelog

## Unreleased

- **Turning chromatic mode off:** hold SHIFT + TIME/BPM (the chromatic menu opens) and press CANCEL while both
  are still held: the bank goes back to normal pads (same as CTRL 1 fully left) and the menu closes for the main
  screen. Any other CANCEL works as stock (issue #2).
- SHIFT + TIME/BPM in the chromatic menu no longer leaves it: it works as from the main screen, turning the bank
  back on for the current pad if it's `OFF`, and the menu stays open. TIME/BPM alone leaves.
- **HOLD on every key** of a chromatic bank, as v2.10 intended: only the source pad latched, because Roland
  releases latched pads that have no voice of their own playing, and a key's note plays on the source's voice. A new note also drops the latch of a key held
  earlier, so that key plays on its next press. Turning a bank off (CANCEL gesture or CTRL 1 fully left) releases
  the note it's playing, so a held note doesn't keep sounding.

## v2.10

The version screen shows `2.10`. Output `sp404sx_up.bin` SHA-256
`733885e5935d0c2c4c887f0446afa8e2546d2520223cec7ccbe9297c08066b54`.

- **Chromatic mode per bank:** SHIFT + TIME/BPM opens a chromatic menu for the last pad played (TIME/BPM
  blinks) and makes its bank a keyboard for it: pad 1 = own pitch, + 1 semitone per pad. CTRL 1 MIDI channel
  (free ones only) or OFF, CTRL 2 mono (poly later), CTRL 3 octave −2 … +2. Several banks at once.
  MIDI on a bank's channel: note 60 = own pitch, silent above B6. Saved in `CHROMA.BIN`. SHIFT + TIME/BPM or
  TIME/BPM leaves the menu. Pads of a chromatic bank don't close the TIME/BPM or START/END/LEVEL page, and HOLD
  works on every pad of the bank.
- While resampling (`RDY` / `REC`) every MIDI note plays like a pad press, and the first one starts the
  recording (stock ignores MIDI notes while sampling).
- Varispeed now follows the sample on copy and exchange even between identical samples (it is moved when
  Roland's copy / exchange finishes, instead of only by matching sample fingerprints).
- The resampler handles speeds up to just under 8× (chromatic notes); PTN keeps its 4× limit.

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
- [Web patcher](https://good-scoop-labs.github.io/sp-404sx-extrafirmware/) (GitHub Pages, `docs/index.html`) and Python patcher (`patch.py`) with identical results.

Development history (internal test builds 1 to 13) is in [docs/DEV_NOTES.md](docs/DEV_NOTES.md).
