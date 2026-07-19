# j6510 Android BASIC

This ARM64 Android terminal runs the OSI Microsoft BASIC ROM on the native C++
`j6510` CPU emulator. Tap the terminal and type directly at its on-screen
cursor, like on a classic home computer. It also provides quick `RUN`, `LIST`,
and `NEW` commands, reset, and a BASIC break button.

Build from the repository root in the configured Termux environment:

```sh
./android-basic/build.sh
```

The signed APK is written to:

```text
android-basic/build/j6510-basic-arm64.apk
```

The app supports Android 7.0 (API 24) and newer on `arm64-v8a` devices.

Example program:

```basic
10 FOR I=1 TO 10
20 PRINT I;" HELLO FROM J6510"
30 NEXT I
RUN
```
