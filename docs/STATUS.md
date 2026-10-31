# Status

What has been confirmed on hardware, and what hasn't yet. Release v2.00 = internal build 13 plus the `2.00`
version screen. The SP-404SX and SP-404A run the same firmware, so this applies to both.

## Confirmed on hardware

- Installing through Roland's normal card update, and going back to stock with Roland's original file.
- CTRL 3 varispeed: audible speed change, display BPM, OFF position, normal playback elsewhere.
- PTN locking to the pattern tempo and following tempo changes.
- CTRL 1 / CTRL 2 showing the tempo you hear; CTRL 2 resetting varispeed.
- Saving across power cycles; copy, exchange, delete, recording and resampling with varispeed pads.
- TIME/BPM light staying on, and the CTRL 3 view staying on screen.
- Everyday use of the rest of the sampler (effects, recording, patterns) unchanged.

## Not tested yet

- The `2.00` version screen text (two digit offsets in Roland's version routine; the same kind of change
  as internal test build 1, which was confirmed on hardware).
- **Many fast PTN pads at once** (up to 4× sample reading): possible dropouts.
- The TIME/BPM page's automatic exit in pattern mode while the CTRL 3 view is shown (one-byte fix found
  in a code review; matches Roland's behaviour for the CTRL 1 / CTRL 2 views).

## Checked in software

- Every changed byte maps to a planned patch; Roland code that would branch into, point into or load
  constants from reused areas was ruled out by the build checks.
- Host tests (`src/tests`): resampler bit-exact against a reference in 760 cases; 300 million samples under
  AddressSanitizer / UBSan; 16 persistence cases; 52 TIME/BPM page cases.
- Both patchers give a byte-identical file to the build (`src/tools/check_web_patch.js`).

## Known limits

See "Limits and notes" in the [user guide](USER_GUIDE.md#limits-and-notes).
