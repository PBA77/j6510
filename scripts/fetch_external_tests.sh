#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="$ROOT_DIR/third_party/klaus"

mkdir -p "$OUT_DIR"

curl -L --fail \
  https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin \
  -o "$OUT_DIR/6502_functional_test.bin"

curl -L --fail \
  https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/6502_functional_test.a65 \
  -o "$OUT_DIR/6502_functional_test.a65"

curl -L --fail \
  https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/readme.txt \
  -o "$OUT_DIR/readme.txt"

curl -L --fail \
  https://raw.githubusercontent.com/analog-hors/pones/main/pones-6502/tests/decimal/bin/6502_decimal_test.bin \
  -o "$OUT_DIR/6502_decimal_test.bin"

curl -L --fail \
  https://raw.githubusercontent.com/analog-hors/pones/main/pones-6502/tests/decimal/6502_decimal_test.a65 \
  -o "$OUT_DIR/6502_decimal_test.a65"

printf '%s\n' "External test assets downloaded to $OUT_DIR"
