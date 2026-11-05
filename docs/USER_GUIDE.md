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

## Chromatic mode

Chromatic mode turns a bank into a keyboard for one of its samples. Pitch and speed change together, like
varispeed, so higher notes are also shorter. Any number of banks can be chromatic at once, each with its own
sample, MIDI channel and octave.

### Turning it on: the chromatic menu

Play the pad you want, then press **SHIFT + TIME/BPM**. The chromatic menu opens and the TIME/BPM button
blinks. That pad's bank is now chromatic with it as the source: pad 1 plays it at its own pitch, pad 2 one
semitone higher, up to pad 12 (+11). Empty pads in the bank play too.

| Control | Display | Sets |
|---|---|---|
| CTRL 1 | `OFF`, `C 1` … `C16` | MIDI channel for this bank. Only channels that are free are offered (not the SP-404SX/A's own two MIDI channels: its channel for banks A–E and the next one for banks F–J; not another chromatic bank's). Fully left = `OFF`: the bank goes back to normal pads. |
| CTRL 2 | `MON` | Mono playback (poly will be added later) |
| CTRL 3 | `o-2` … `o 2` | Octave of the 12 pads: two octaves down to two octaves up |

Press **TIME/BPM** to leave the menu (SHIFT + TIME/BPM in the menu keeps it open and turns the bank back on if it
is `OFF`). With CTRL 1 at `OFF`, pressing another pad also leaves
it, as on the TIME/BPM page; while the bank is chromatic, pads in the menu just play. To change the settings later, play a pad of that bank and press
SHIFT + TIME/BPM again; opening the menu on a different pad of the bank makes that pad the source.

### Turning it off

Hold **SHIFT + TIME/BPM** (the menu opens) and, while still holding both, press **CANCEL**. The bank goes back to
normal pads and you're back on the main screen. You can also turn CTRL 1 fully left in the menu (`OFF`) and then
press TIME/BPM to leave. The setting is saved. To turn it on again, play one of the bank's pads and press
SHIFT + TIME/BPM (or, with the menu still open at `OFF`, press SHIFT + TIME/BPM there).

### Playing

- **Pads:** the bank's 12 pads, as above, on the main screen, the TIME/BPM page and while resampling (`RDY` /
  `REC`). The main screen shows `C.ON` while you're on a chromatic bank.
- **MIDI:** on the bank's channel, note 60 (middle C) plays the sample at its own pitch; every note up or down
  is a semitone. The highest note that can play is B6 (note 95, about 7.5× speed); notes above it are silent.
  Low notes go all the way down to note 0 (very slow and dark). The octave setting applies to the pads only.
- **Gate, loop, reverse, Time Modify, level and effects** come from the source pad. With gate on, releasing
  the key (or the MIDI note) stops the note.
- **TIME/BPM and START/END/LEVEL pages:** pressing a pad of a chromatic bank plays it and keeps the page open.
- **HOLD** works on every pad of a chromatic bank (it follows the source pad's GATE setting): each key latches
  like a normal pad, and pressing the sounding key again releases it.
- **One note at a time per bank** for now: a new note cuts the previous one. With a loop pad, another key
  switches to its note; the key that is sounding stops the loop when pressed again.
- **Varispeed** (CTRL 3 on the TIME/BPM page) of the source still applies and moves the whole keyboard.
- **MARK** while a key sounds keeps that note's pitch.
- **Recording a melody:** use RESAMPLE. Choose the destination as usual (pads behave as stock while you
  choose); once the display shows `RDY` or `REC`, the pads are keys again and MIDI notes on chromatic banks'
  channels play too. In fact every MIDI note plays there, like a pad press (the SP-404SX/A's own channel plays its
  pads), and at `RDY` the first pad or MIDI note starts the recording. While choosing the destination, and while
  sampling an external input, MIDI notes are ignored as on the stock SP-404SX/A. (Other MIDI notes are ignored while sampling or resampling, as on the stock SP-404SX/A.)
- Pattern mode and pattern recording use normal pads; patterns record pads, not pitches.

### Saving

The chromatic banks and their settings are saved on the card in `/ROLAND/SP-404SX/SMPL/CHROMA.BIN` and come
back at power-on. If a bank's source sample is deleted, replaced or recorded over, that bank stops being
chromatic.

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
- The **power-on version display** (hold MARK) shows `2.10` instead of `1.07`.

## Limits and notes

- Varispeed applies to the first 12 voices, which is the SP-404SX/A's polyphony; anything beyond plays at
  normal speed.
- At very high PTN speeds a pad reads its sample up to 4× faster. If you hear dropouts with many fast PTN
  pads playing at once, please report it.
- Copy and exchange on the SP-404SX/A move the speeds directly. Otherwise (e.g. a card from another unit),
  each pad's saved speed is matched to its sample by a short fingerprint. Very rarely (about 1 in 255) a
  newly recorded sample can match another pad's fingerprint and take that pad's speed instead of normal;
  turning CTRL 3 again fixes it.
- CTRL 3 also sets the current pad's speed on the TIME/BPM page in pattern mode.
