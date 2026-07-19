#!/data/data/com.termux/files/usr/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SMOKE_BUILD_DIR="$SCRIPT_DIR/build/smoke"
NATIVE_LIBRARY="$SCRIPT_DIR/build/package/lib/arm64-v8a/libj6510_basic.so"

if [ ! -f "$NATIVE_LIBRARY" ]; then
    "$SCRIPT_DIR/build.sh"
fi

mkdir -p "$SMOKE_BUILD_DIR"
javac \
    -d "$SMOKE_BUILD_DIR" \
    "$SCRIPT_DIR/smoke/com/pba77/j6510basic/MainActivity.java"

java \
    -cp "$SMOKE_BUILD_DIR" \
    com.pba77.j6510basic.MainActivity \
    "$NATIVE_LIBRARY"
