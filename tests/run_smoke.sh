#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN="$ROOT/bin/tarsau"
OUT="$ROOT/tests/out"
ARCHIVE="$OUT/s1.sau"
DEST="$OUT/d1"

rm -rf "$OUT"
mkdir -p "$OUT"

"$BIN" -b \
    "$ROOT/tests/sample/t1" \
    "$ROOT/tests/sample/t2" \
    "$ROOT/tests/sample/t3" \
    "$ROOT/tests/sample/t4.txt" \
    "$ROOT/tests/sample/t5.dat" \
    -o "$ARCHIVE"

"$BIN" -a "$ARCHIVE" "$DEST"

cmp "$ROOT/tests/sample/t1" "$DEST/t1"
cmp "$ROOT/tests/sample/t2" "$DEST/t2"
cmp "$ROOT/tests/sample/t3" "$DEST/t3"
cmp "$ROOT/tests/sample/t4.txt" "$DEST/t4.txt"
cmp "$ROOT/tests/sample/t5.dat" "$DEST/t5.dat"

printf "Smoke test gecti.\n"