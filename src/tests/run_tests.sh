#!/bin/sh
# Host tests for the firmware C code (no SP-404 or cross toolchain needed, just a C compiler).
# usage: sh src/tests/run_tests.sh
set -e
cd "$(dirname "$0")"
FW=../firmware
OUT=${TMPDIR:-/tmp}/sp404sx-extrafirmware-tests
mkdir -p "$OUT"
(cd "$FW" && python3 gen_ratio.py)
CC=${CC:-gcc}
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
for t in test_ui test_resampler test_persist; do
  $CC -O1 -w $SAN -I"$FW" -o "$OUT/$t" $t.c
  printf '%-16s ' "$t"
  "$OUT/$t" > "$OUT/$t.log" || { cat "$OUT/$t.log"; echo "FAILED: $t"; exit 1; }
  tail -1 "$OUT/$t.log"
done
$CC -O1 -g -w $SAN -I"$FW" -o "$OUT/test_stress" test_stress.c
printf '%-16s ' test_stress; "$OUT/test_stress"
echo "all host tests passed"
