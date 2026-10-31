# Installing SP-404SX/A EXTRAfirmware

## What you need

- A Roland **SP-404SX** or **SP-404A** with its **AC adaptor** (don't update on batteries).
- An SD card formatted by the SP-404SX/A, with at least 2 MB free. **Back up the card first.**
- Roland's **SP-404SX System Update Version 1.07** from
  [Roland's support page](https://www.roland.com/us/support/by_product/sp-404sx/updates_drivers/39ba2f11-6740-46d0-9286-0229f3c71bf7/).
  Unzip it; you need `sp404sx_up.bin` from inside, unchanged
  (SHA-256 `be533c349cf248ed9bf7236c926b11bb58bafc7946e5314a8736391e7b71b13e`).

## 1. Make the firmware file

**Easiest: the web patcher.** Open the project's web patcher page, drop the official `sp404sx_up.bin`
onto it and click **Download sp404sx_up.bin**. The page checks the file and the result exactly like the
Python patcher, and nothing leaves your computer.

**Or with Python** (3.8 or newer, no packages): download this repository, open a terminal in its folder
and run

```
python3 patch.py path/to/sp404sx_up.bin
```

It prints

```
SP-404SX/A EXTRAfirmware v2.00 written to out/sp404sx_up.bin
SHA-256 3093b7e677e285e4dd1f612d9f9ccc0df6e364998bc79f1ec52df0b9c11a7ffd
```

Either way the file you get is named `sp404sx_up.bin` and has that SHA-256. The SP-404SX/A only installs
a file with exactly that name.

## 2. Install

1. Copy the new `sp404sx_up.bin` to the **root** of the SD card (not into a folder).
2. Power off the SP-404SX/A and insert the card.
3. Connect the AC adaptor and power on. The update starts by itself.
4. The display shows `UPd` (the dot blinks), then `Fin` after about ten seconds.
   **Never power off while it shows `UPd`.**
5. Power off, take out the card and delete `sp404sx_up.bin` from it (otherwise it updates again at
   every power-on).

## 3. Check it

Power off, then hold **MARK** while powering on: the display shows the version, **`2.00`**
(Roland's firmware shows `1.07`). You can also select a pad, press **TIME/BPM** and turn **CTRL 3**: the
display shows a BPM that changes as you turn it.

The first time you save a speed, the firmware creates `/ROLAND/SP-404SX/SMPL/VARISPD.BIN` on the card.

## Going back to stock

Install Roland's original `sp404sx_up.bin` (straight from Roland's zip) the same way. Your samples and
patterns are not affected. `VARISPD.BIN` stays on the card; the stock firmware ignores it, and it is used
again if you reinstall EXTRAfirmware.

## SP-404A

The SP-404A is the same instrument as the SP-404SX in a different case and runs the same firmware. It
installs the same `sp404sx_up.bin` (Roland publishes the update on the SP-404SX page), and EXTRAfirmware
works on it in exactly the same way. Everything in these guides applies to both.
