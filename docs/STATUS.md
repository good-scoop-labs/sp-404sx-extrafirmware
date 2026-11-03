# Status

What has been confirmed on hardware, and what hasn't yet. Release v2.00 = internal build 13 plus the `2.00`
version screen. The SP-404SX and SP-404A run the same firmware, so this applies to both.

## v2.10 (in testing, not released yet)

Confirmed on hardware (test builds E1 … v2.10c):
- Chromatic mode: SHIFT + TIME/BPM opens the menu (TIME/BPM blinks) and makes the last pad's bank a keyboard;
  all 12 pads play in semitones, including empty ones; loop pads switch notes correctly.
- Menu: CTRL 1 MIDI channel (free ones only, OFF fully left), CTRL 2 `MON`, CTRL 3 octave; leaving with
  TIME/BPM or SHIFT + TIME/BPM, and with another pad when the bank is OFF.
- MIDI on a bank's channel (note 60 = own pitch); `C.ON` on the main screen.
- Resampling: chromatic pads and MIDI play at `RDY` / `REC`, and any MIDI note starts the recording.
- TIME/BPM and START/END/LEVEL pages stay open for pads of a chromatic bank; HOLD on every key; MARK keeps
  the chromatic pitch.
- Varispeed follows copy and exchange, including between identical samples.
- Version screen `2.10`.

Not tested yet:
- Several chromatic banks at once on different channels, and `CHROMA.BIN` across power cycles with them.
- A chromatic source whose sample is deleted or replaced (the bank should turn off).
- Pattern playback and pattern recording with chromatic banks (should play the source at its own pitch).

## v2.00

### Confirmed on hardware

- Installing through Roland's normal card update, and going back to stock with Roland's original file.
- CTRL 3 varispeed: audible speed change, display BPM, OFF position, normal playback elsewhere.
- PTN locking to the pattern tempo and following tempo changes.
- CTRL 1 / CTRL 2 showing the tempo you hear; CTRL 2 resetting varispeed.
- Saving across power cycles; copy, exchange, delete, recording and resampling with varispeed pads.
- TIME/BPM light staying on, and the CTRL 3 view staying on screen.
- Everyday use of the rest of the sampler (effects, recording, patterns) unchanged.

### Not tested yet

- The `2.00` version screen text (two digit offsets in Roland's version routine; the same kind of change
  as internal test build 1, which was confirmed on hardware).
- **Many fast PTN pads at once** (up to 4× sample reading): possible dropouts.
- The TIME/BPM page's automatic exit in pattern mode while the CTRL 3 view is shown (one-byte fix found
  in a code review; matches Roland's behaviour for the CTRL 1 / CTRL 2 views).

### Checked in software

- Every changed byte maps to a planned patch; Roland code that would branch into, point into or load
  constants from reused areas was ruled out by the build checks.
- Host tests (`src/tests`): resampler bit-exact against a reference in 760 cases; 300 million samples under
  AddressSanitizer / UBSan; 16 persistence cases; 52 TIME/BPM page cases.
- Both patchers give a byte-identical file to the build (`src/tools/check_web_patch.js`).

## Known limits

See "Limits and notes" in the [user guide](USER_GUIDE.md#limits-and-notes).
