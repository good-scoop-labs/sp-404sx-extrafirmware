# SP-404SX/A EXTRAfirmware

**SP-404SX/A EXTRAfirmware** is custom system firmware for the **Roland SP-404SX and SP-404A**. The two are
the same instrument in a different case and run the same firmware, so it works on both in exactly the same
way. It adds tape-style **varispeed** to every pad (pitch and tempo change together, like speeding up or
slowing down a tape or a turntable) and a **chromatic mode** that turns a bank into a keyboard for one of its
samples, playable from the pads and over MIDI.

> **Current release: v2.12** (the version screen shows `2.12`), based on Roland's system 1.07.
> Output file: `sp404sx_up.bin`, SHA-256 `a717922dc42cadd133f5f23719e112817bd3239bc18460e5407f6a049a6aeeb1`.

## What it adds

### Varispeed

On the **TIME/BPM** page, the previously unused **CTRL 3** knob sets each pad's speed:

| CTRL 3 position | Speed |
|---|---|
| Fully left | **OFF**: plays exactly as stock |
| Left half | −12 semitones (half speed) up to normal |
| Centre | Normal |
| Right half | Normal up to +7 semitones |
| Fully right | **PTN**: follows the pattern tempo (0.25× to 4×) |

- The display shows the **BPM you hear**. CTRL 1 (Time Modify) and CTRL 2 (original BPM) also show their
  value × the pad's speed, so all three agree.
- Works together with **Time Modify**. Moving CTRL 2 resets varispeed, the same way Roland's CTRL 2 resets
  Time Modify.
- Speeds are **saved on the card** (`/ROLAND/SP-404SX/SMPL/VARISPD.BIN`) and **follow the sample**: copy and
  exchange take the speed along; delete, a new recording or an import on a pad resets it to normal.
- Pads at normal speed or OFF play bit-for-bit as with the stock firmware.

### Chromatic mode

Play a pad, then press **SHIFT + TIME/BPM**: that pad's bank becomes a keyboard for it. Pad 1 plays the
sample at its own pitch, each next pad one semitone higher. A menu opens (TIME/BPM blinks):

| Control | Sets |
|---|---|
| CTRL 1 | MIDI channel for the bank (only free channels), or OFF to turn chromatic mode off (or hold SHIFT + TIME/BPM and press CANCEL) |
| CTRL 2 | `MON`: one note at a time (poly is planned) |
| CTRL 3 | Octave of the pads, −2 to +2 |

- Several banks can be chromatic at once, each with its own sample and MIDI channel.
- **MIDI:** on the bank's channel, middle C (note 60) plays the sample at its own pitch; every note is a
  semitone, up to B6.
- Works on the main screen, the TIME/BPM page and while **resampling**: play a melody into a new sample.
  While resampling, any MIDI note plays and the first one starts the recording.
- HOLD works on every key, MARK keeps the note's pitch, and varispeed still applies on top.
- Settings are saved on the card (`/ROLAND/SP-404SX/SMPL/CHROMA.BIN`).

### Mute groups

Each pad can be in one of ten **mute groups**: when a pad of a group starts, the other playing pads of that
group stop (a "choke", e.g. open and closed hi-hat), in any bank, from pads, MIDI or patterns. Play a pad, then
press **SHIFT + MARK** on the main screen (MARK blinks): CTRL 1 sets its group, `OFF` or `M 1` … `M10`; MARK
closes the menu. Saved on the card (`/ROLAND/SP-404SX/SMPL/MUTEGRP.BIN`), following the sample like the speeds.

### Pan

Each pad has its own **pan**. Play a pad, then press **SHIFT + START/END/LEVEL** (START/END/LEVEL blinks):
CTRL 3 sets it from `L50` through `C` to `R50`. Panning turns the far side down (balance law, no boost), on
stereo and mono pads, wherever the pad plays: pads, MIDI, patterns, resampling, together with varispeed and
chromatic mode. Pads at `C` play bit-for-bit as stock. Saved on the card (`/ROLAND/SP-404SX/SMPL/PAN.BIN`),
following the sample like the speeds.

Full details: [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

## How to install

This repository contains **no Roland code**. You make the firmware file from Roland's own update:

1. Download **SP-404SX System Update Version 1.07** from
   [Roland's support page](https://www.roland.com/us/support/by_product/sp-404sx/updates_drivers/39ba2f11-6740-46d0-9286-0229f3c71bf7/)
   and unzip it to get `sp404sx_up.bin` (the SP-404A uses this same file).
2. Turn it into the EXTRAfirmware file (either way gives the identical file):
   - **In the browser (easiest):** open the [web patcher](https://good-scoop-labs.github.io/sp-404sx-extrafirmware/)
     and drop the file onto it. Nothing is uploaded; it all runs in the page.
   - **With Python 3.8+** (no packages needed):
     ```
     python3 patch.py path/to/sp404sx_up.bin
     # -> out/sp404sx_up.bin
     ```
3. Install it like a normal Roland update: see [docs/INSTALL.md](docs/INSTALL.md).

Both patchers refuse any file that is not the official 1.07 update and check the result against the
release hash before giving it to you. To go back to stock, install Roland's original file the same way.

## Status

Works on the SP-404SX and the SP-404A alike (same hardware, same firmware, same update file).
See [docs/STATUS.md](docs/STATUS.md) for what has been confirmed on hardware.

## Documentation

- [docs/INSTALL.md](docs/INSTALL.md): making the file, installing, checking, going back to stock
- [docs/USER_GUIDE.md](docs/USER_GUIDE.md): everything the firmware adds and changes
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md): problems and questions
- [CHANGELOG.md](CHANGELOG.md)
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md): how it works, building, tests, releasing
- [docs/DEV_NOTES.md](docs/DEV_NOTES.md): the full engineering log

## Disclaimer

Unofficial and not affiliated with or endorsed by Roland. Installing custom firmware is at your own risk.
The update process is Roland's own and Roland's original update file always restores the stock firmware,
but never power off while the display shows `UPd`. Please don't post `sp404sx_up.bin` files (stock or
modified); they are Roland's code. Share the patcher instead.

The code in this repository is MIT licensed (see [LICENSE](LICENSE)). SP-404SX and SP-404A are trademarks
of Roland Corporation.
