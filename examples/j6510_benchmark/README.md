# Embedded Benchmark Sketch

This sketch runs j6510 CPU benchmark programs on Arduino-compatible embedded
boards and prints throughput over USB serial at 115200 baud.

## PlatformIO

From the repository root:

```sh
pio run -e teensy40-benchmark
pio run -e rp2040-pico-benchmark
pio run -e esp32s2-saola-benchmark
pio device monitor -b 115200
```

If `pio` is not on `PATH`, use `~/.platformio/penv/bin/pio`.

## Arduino IDE / Teensyduino

Open `examples/j6510_benchmark/j6510_benchmark.ino`, select **Teensy 4.0**, and
build/upload.

The PlatformIO environments enable the cached-block executor. On ESP32-S2 with
PSRAM, the emulated 64 KB bus RAM is allocated in PSRAM while the CPU and block
cache stay in local heap when possible.
