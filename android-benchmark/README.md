# j6510 Android benchmark

This ARM64 Android app benchmarks the native C++ `j6510` core through JNI. It
compares `step()`, `run()`, `run_block()`, and `run_cached()` using the same
nine-instruction workload as `j6510_benchmark both`.

Build from the repository root in the configured Termux environment:

```sh
./android-benchmark/build.sh
```

The signed APK is written to:

```text
android-benchmark/build/j6510-benchmark-arm64.apk
```

The app supports Android 7.0 (API 24) and newer on `arm64-v8a` devices.
