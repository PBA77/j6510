#!/data/data/com.termux/files/usr/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
SDK_DIR=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-"$PROJECT_DIR/../android-sdk"}}
PLATFORM_DIR="$SDK_DIR/platforms/android-36"
NDK_DIR="$SDK_DIR/ndk/27.1.12297006"
BUILD_DIR="$SCRIPT_DIR/build"
PACKAGE_DIR="$BUILD_DIR/package"
LIB_DIR="$PACKAGE_DIR/lib/arm64-v8a"
CLASSES_DIR="$BUILD_DIR/classes"
DEX_DIR="$BUILD_DIR/dex"
APK_PATH="$BUILD_DIR/j6510-benchmark-arm64.apk"

if [ ! -f "$PLATFORM_DIR/android.jar" ]; then
    echo "Brak Android SDK: $PLATFORM_DIR/android.jar" >&2
    exit 1
fi
if [ ! -f "$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/jni.h" ]; then
    echo "Brak nagłówków JNI w: $NDK_DIR" >&2
    exit 1
fi

rm -rf "$BUILD_DIR"
mkdir -p "$LIB_DIR" "$CLASSES_DIR" "$DEX_DIR"

clang++ \
    -std=c++17 -O3 -DNDEBUG -fPIC -shared \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -Wl,-soname,libj6510_benchmark.so \
    -DJ6510_ENABLE_BLOCK_CACHE=1 \
    -DJ6510_ENABLE_CACHE_STATS=1 \
    -idirafter "$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include" \
    -I"$PROJECT_DIR/src" \
    "$SCRIPT_DIR/native/benchmark_jni.cpp" \
    "$PROJECT_DIR/src/cpu6510_bus.cpp" \
    "$PROJECT_DIR/src/cpu6510_core.cpp" \
    "$PROJECT_DIR/src/cpu6510_opcode_table.cpp" \
    -o "$LIB_DIR/libj6510_benchmark.so"

cp /data/data/com.termux/files/usr/lib/libc++_shared.so "$LIB_DIR/libc++_shared.so"

javac \
    -source 8 -target 8 \
    -bootclasspath "$PLATFORM_DIR/android.jar" \
    -d "$CLASSES_DIR" \
    "$SCRIPT_DIR/java/com/pba77/j6510benchmark/MainActivity.java"

jar --create --file "$BUILD_DIR/classes.jar" -C "$CLASSES_DIR" .

d8 \
    --min-api 24 \
    --lib "$PLATFORM_DIR/android.jar" \
    --output "$DEX_DIR" \
    "$BUILD_DIR/classes.jar"

aapt2 link \
    -o "$BUILD_DIR/base-unsigned.apk" \
    -I "$PLATFORM_DIR/android.jar" \
    --manifest "$SCRIPT_DIR/AndroidManifest.xml" \
    --min-sdk-version 24 \
    --target-sdk-version 36

cp "$DEX_DIR/classes.dex" "$PACKAGE_DIR/classes.dex"
cp "$BUILD_DIR/base-unsigned.apk" "$BUILD_DIR/with-payload-unsigned.apk"
jar --update --file "$BUILD_DIR/with-payload-unsigned.apk" \
    -C "$PACKAGE_DIR" classes.dex \
    -C "$PACKAGE_DIR" lib

keytool -genkeypair \
    -keystore "$BUILD_DIR/debug.keystore" \
    -storepass android \
    -alias androiddebugkey \
    -keypass android \
    -keyalg RSA \
    -keysize 2048 \
    -validity 10000 \
    -dname "CN=Android Debug,O=j6510,C=PL" \
    >/dev/null 2>&1

apksigner sign \
    --ks "$BUILD_DIR/debug.keystore" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --out "$APK_PATH" \
    "$BUILD_DIR/with-payload-unsigned.apk"

apksigner verify --verbose "$APK_PATH"
echo "$APK_PATH"
