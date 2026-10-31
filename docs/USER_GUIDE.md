# SP-404SX/A EXTRAfirmware user guide

Everything here applies to the SP-404SX and SP-404A alike. It all happens on the **TIME/BPM** page (select a
pad, press TIME/BPM); everything else works as with Roland's firmware.

## Varispeed (CTRL 3)

Varispeed plays a pad faster or slower, with pitch and tempo changing together, like a tape or turntable.
Each pad has its own setting.

| CTRL 3 position | Display | Speed |
|---|---|---|
| Fully left (last few positions) | `OFF` | Normal; the pad plays exactly as stock |
| From just right of OFF to the centre | BPM | −12 semitones (half speed) up to normal |
| Centre | BPM | Normal |
| From the centre to just left of fully right | BPM | Normal up to +7 semitones (about 1.5×) |
| Fully right (last few positions) | `PTN` | Follows the pattern tempo |

The steps are smooth rather than whole semitones, so every BPM step on the display is a small change in
pitch. The display shows the **BPM you hear**: the pad's tempo (with Time Modify) × its speed.

After you turn CTRL 3, the display keeps showing the varispeed BPM until you turn CTRL 1 or CTRL 2 or press
TIME/BPM. The TIME/BPM button stays lit.

### PTN: follow the pattern tempo

With CTRL 3 fully right, the pad's speed is set so that its tempo matches the pattern tempo, tape style
(the pitch moves with it). It follows the pattern tempo live: change the pattern BPM and the pad follows.
The speed can go from 0.25× to 4× (two octaves down to two octaves up).

- The pad's tempo is the one CTRL 2 shows (the original BPM), or the Time Modify BPM if CTRL 1 is set to a BPM.
- If Time Modify is set to `PTN` (CTRL 1 fully right), Roland's Time Modify already follows the pattern, so
  varispeed PTN leaves the speed at normal.
- Moving the start or end mark makes the SP-404SX/A recalculate the sample's BPM from the new marked length
  (Roland's behaviour), so PTN re-fits the marked region to the pattern and the speed can change.

### With Time Modify (CTRL 1)

Time Modify and varispeed work together: Time Modify stretches the sample to its BPM without changing
pitch, then varispeed speeds the result up or down like tape.

- **CTRL 1** shows the Time Modify BPM × the pad's speed (`OFF`, `PTN` and `---` show as before).
- **CTRL 2** shows the original BPM × the pad's speed.
- **Moving CTRL 2 resets varispeed** to normal, just as Roland's CTRL 2 resets Time Modify.

## Saving and copying

- Speeds are saved on the card in `/ROLAND/SP-404SX/SMPL/VARISPD.BIN`, about a second after the last
  change, and restored at power-on.
- The setting follows the **sample**, not the pad position:
  - **Copy:** the new pad gets the same speed.
  - **Exchange:** each speed moves with its sample.
  - **Delete, new recording, resampling or import onto a pad:** that pad goes back to normal speed.
  - **Editing marks, level, loop or Time Modify:** the speed is kept.
- Each SD card has its own `VARISPD.BIN`, so speeds go with the card.
- The file is written at the same times Roland saves its own pad settings, and never while sampling or
  during card operations.

## What else changes

- Roland's **factory hardware-test mode** (the technician test screens started from a test file on the card
  or a key combination at power-on) is removed to make room for the new code. Nothing a player uses
  depends on it. Installing Roland's original update brings it back.
- The **power-on version display** (hold MARK) shows `2.00` instead of `1.07`.

## Limits and notes

- Varispeed applies to the first 12 voices, which is the SP-404SX/A's polyphony; anything beyond plays at
  normal speed.
- At very high PTN speeds a pad reads its sample up to 4× faster. If you hear dropouts with many fast PTN
  pads playing at once, please report it.
- Each pad's saved speed is matched to its sample by a short fingerprint. Very rarely (about 1 in 255) a
  newly recorded sample can match another pad's fingerprint and take that pad's speed instead of normal;
  turning CTRL 3 again fixes it.
- CTRL 3 also sets the current pad's speed on the TIME/BPM page in pattern mode.
