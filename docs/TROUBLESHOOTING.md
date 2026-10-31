# Troubleshooting

## Making the file

**"This is not the official SP-404SX System Update 1.07 file."**
Use `sp404sx_up.bin` straight from Roland's `sp404_sys_v107.zip`, not a renamed, re-saved or older/newer file.
Its SHA-256 is `be533c349cf248ed9bf7236c926b11bb58bafc7946e5314a8736391e7b71b13e`.

**"This file is already SP-404SX/A EXTRAfirmware."**
You dropped a file the patcher already made. You can install it as it is.

**The web page does nothing.** Use a current browser with JavaScript enabled. Or use `python3 patch.py`,
which gives the identical file.

## Installing

**The update doesn't start.** The file must be named exactly `sp404sx_up.bin` and sit in the root of the
card. The card must be formatted by the SP-404SX/A and have at least 2 MB free. Some browsers add `(1)` to a
second download of the same name; rename it.

**The display shows an error instead of `Fin`.** Power off, re-make the file with the patcher, copy it
again and retry. The update checks the file before and after writing it.

**Something misbehaves after installing.** Power off and on. If it persists, install Roland's original 1.07
`sp404sx_up.bin` the same way to return to stock, and please open an issue. Never power off while the
display shows `UPd`.

**It updates again at every power-on.** Delete `sp404sx_up.bin` from the card after `Fin`.

## Using it

**Turning CTRL 3 does nothing.** Make sure you are on the TIME/BPM page with a pad selected, and check
that EXTRAfirmware is installed (see [INSTALL.md](INSTALL.md), "Check it").

**A pad's speed was reset.** Moving CTRL 2 resets varispeed (as it resets Time Modify). Deleting,
recording, resampling or importing onto a pad also resets it.

**PTN speed changed after I moved the marks.** The SP-404SX/A recalculates the sample's BPM from the marked
length, and PTN follows that BPM. See the [user guide](USER_GUIDE.md#ptn-follow-the-pattern-tempo).

**Speeds are not restored after power-off.** Wait a second after the last change before powering off, and
make sure the card isn't write-protected or full.

**Dropouts with several fast pads.** Very high speeds read samples faster. Please open an issue with what
was playing.

## Reporting a problem

Open an issue with: SP-404SX or SP-404A, release (v2.00, shown at power-on with MARK held), what you did, what you expected and what happened.
