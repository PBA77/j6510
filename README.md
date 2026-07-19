# j6510

`j6510` is a small, local-first MOS 6510 / NMOS 6502 CPU emulator core written
in C++17.

The project is intentionally focused on the CPU before any full Commodore 64
machine emulation. There is no VIC-II, SID, CIA, C64 memory mapper, Arduino
adapter, ESP-IDF adapter, or host ISR code in the core.

## What Works

- All documented NMOS 6502 opcodes are implemented.
- Illegal or undocumented opcodes return a controlled `IllegalOpcode` result by
  default.
- Stable undocumented NMOS 6502/6510 opcodes can be enabled explicitly through
  `Cpu6510Config::undocumented_opcodes_enabled`.
- All common 6502 addressing modes are implemented, including zero-page wrap,
  relative branches, indexed indirect modes, and the NMOS `JMP ($xxFF)` bug.
- Binary and decimal `ADC/SBC` are implemented.
- `BRK`, `RTI`, `RESET`, `NMI`, and level-triggered `IRQ` basics are present.
- Host interrupt polling is exposed through a callback, so desktop tests and
  embedded adapters can drive the same CPU core without platform code inside
  the emulator.
- The 6510 I/O port at `$0000/$0001` is implemented behind a profile flag.
- The CPU talks to an abstract bus, with a simple 64 KB RAM bus for tests.

## CPU Profiles

By default, `Cpu6510` enables the 6510 port at `$0000/$0001`.

For pure NMOS 6502 tests, construct the CPU with:

```cpp
Cpu6510 cpu(bus, Cpu6510Config{false});
```

That disables the 6510 port and leaves `$0000/$0001` as normal RAM bus
addresses.

For software that depends on stable undocumented NMOS opcodes, enable the
explicit compatibility profile:

```cpp
Cpu6510 cpu(bus, Cpu6510Config{true, ExecutionMode::InstructionFast, true});
```

The profile covers the common stable families `SLO`, `RLA`, `SRE`, `RRA`,
`SAX`, `LAX`, `DCP`, `ISC`, unofficial immediate `SBC`, and operand/implied
`NOP` variants. Unstable analog-effect opcodes and `KIL/JAM` remain unsupported
and still report `IllegalOpcode`.

The default execution mode remains the instruction-oriented fast path. For a
cycle-counted reference path, construct the CPU with `CycleExact`:

```cpp
Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
```

In this mode `step()` remains instruction-compatible, while `tick()` advances
one counted CPU cycle and `run_cycles(max_cycles)` runs a cycle budget. The
cycle-exact path covers documented NMOS opcodes, variable branch/page-cross
cycles, read-modify-write dummy writes, stack/control-flow timing, and the 6510
port through the normal bus helpers. `run_cached()` remains a fast
instruction-mode API and is not used by `CycleExact` stepping.

## Build And Test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

This always runs the local unit and E2E tests.

For a smaller embedded-oriented build that omits the cached-block executor:

```sh
cmake -S . -B build-embedded -DJ6510_ENABLE_BLOCK_CACHE=OFF
cmake --build build-embedded
```

That keeps `run_cached()` available as an API, but it falls back to `run()`.

The cached-block executor also exposes compile-time tuning knobs:

- `J6510_ENABLE_CACHE_STATS` controls cache hit/miss/fallback counters. It is
  enabled by default for desktop diagnostics and can be disabled for embedded
  fast builds.
- `J6510_BLOCK_CACHE_SLOTS` controls the number of cached block slots. The
  default is `4096` for host builds; the PlatformIO embedded environments pin
  it back to `256` to fit their RAM budgets.
- `J6510_CACHED_BLOCK_MAX_OPS` controls the maximum decoded operations per
  cached block. The default is `32`.

## Embedded Benchmarks

The core includes an Arduino-compatible embedded benchmark sketch at
`examples/j6510_benchmark/j6510_benchmark.ino`.

Open that folder in Arduino IDE or Teensyduino, select the target board, and
build/upload the sketch. The example includes the core sources directly, runs
fixed benchmark programs, and prints throughput over USB serial.

The repository also includes a PlatformIO stack:

```sh
pio run -e teensy40-benchmark
pio run -e rp2040-pico-benchmark
pio run -e esp32s2-saola-benchmark
pio run -e esp32s2-saola-fast
pio run -e esp32s3-fast
pio run -e teensy40-benchmark -t upload
pio device monitor -b 115200
```

If PlatformIO Core is installed but `pio` is not on `PATH`, use
`~/.platformio/penv/bin/pio` or add `~/.platformio/penv/bin` to your shell path.

The ESP32 fast environments build the same sketch with `-O3`, a 240 MHz CPU
frequency setting, cache statistics disabled, and cached executor hot paths
marked for IRAM placement through `J6510_FAST_CODE_ATTR=IRAM_ATTR`. They also
prefer internal heap for the emulator state instead of placing the 64 KB CPU RAM
in PSRAM when PSRAM is present.

This is not a full C64 target. It does not include VIC-II, SID, CIA, keyboard,
video, storage, or real host interrupt wiring.

## Microsoft BASIC Host Demo

`msbasic_demo` runs the OSI Microsoft BASIC ROM from
`examples/j6510_basic_demo/msbasic_osi_rom.h` on the host, using the same
machine model as the ESP32-S2 sketch: ROM at `$A000` (write-protected, cold
start `$BD11`), serial output at `$D001`, serial status/input at `$D002/$D003`,
and monitor trampolines at `$FFEB/$FFEE/$FFF1`. stdin/stdout play the serial
console, so it works both scripted and interactively:

```sh
cmake --build build-release --target msbasic_demo
./build-release/msbasic_demo                              # default FOR/NEXT benchmark
./build-release/msbasic_demo 20000000 "10 PRINT 2+2"      # custom program
./build-release/msbasic_demo                              # then type lines at the OK prompt
```

The default program sums 1..50000 and prints the result. Statistics go to
stderr: on an Android ARM64 phone the interpreter loop runs at ~34 M instr/s
with ~99% of executed instructions in cached IR. This bus cannot expose
`direct_memory()` because the serial registers are memory-mapped, so cached
execution goes through the bus IR path (per-op `read()`/`write()`) instead of
the direct-memory loop — RAM-only buses are roughly 3-4x faster on the same
code. The demo also runs as a `ctest` smoke test that checks the program
reaches its final `DONE` marker.

## Benchmark

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target j6510_benchmark
./build-release/j6510_benchmark both
```

For a sequential Release benchmark run with longer default durations:

```sh
./scripts/run_release_benchmarks.sh
```

The script builds `j6510_benchmark` once, then runs `both`, `mixed`, and
`realish` one after another. Its default iteration counts are twice the manual
examples below and can be overridden with `BOTH_ITERATIONS`, `MIXED_ITERATIONS`,
and `REALISH_ITERATIONS`.

The benchmark runs fixed documented-opcode loops and reports throughput as
equivalent original 6502 MHz, using nominal NMOS 6502 cycle counts for those
instruction mixes. It can compare public `step()` execution, batch `run()`,
terminator-aware `run_block()`, and conservative cached-block `run_cached()`.
`run()` uses a small optimized hot-opcode executor with fallback to the
reference instruction path. You can pass an iteration count:

```sh
./build-release/j6510_benchmark both 10000000
./build-release/j6510_benchmark mixed 5000000
./build-release/j6510_benchmark realish 100000
./build-release/j6510_benchmark profile 100000
```

Current local Release sample after the cached-pair fusion pass and cached hot
path branch-prediction hints, measured on an Apple M1 Max:

| Suite | Iterations | Instructions | Nominal 6502 cycles | `step` | `run` | `run_block` | `run_cached` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `both` | 5,000,000 | 45,000,000 | 135,000,000 | 440 MHz | 643 MHz | 452 MHz | 1,424 MHz |
| `mixed` | 2,000,000 | 60,000,000 | 202,000,000 | 496 MHz | 725 MHz | 503 MHz | 1,533 MHz |
| `realish` | 50,000 | 14,600,000 | 43,650,000 | 421 MHz | 436 MHz | 331 MHz | 1,313 MHz |

Cached-block coverage for that sample:

| Suite | Cache hits/misses/invalidations | IR/fallback instructions |
| --- | ---: | ---: |
| `both` | 4,999,999 / 1 / 0 | 45,000,000 / 0 |
| `mixed` | 9,999,997 / 3 / 0 | 60,000,000 / 0 |
| `realish` | 3,249,996 / 4 / 0 | 14,600,000 / 0 |

ESP32-S2 USB benchmark on an `esp32-s2-saola-1` connected as
`/dev/cu.usbserial-110`:

| Suite | Mode | Baseline build | Fast build | Improvement |
| --- | --- | ---: | ---: | ---: |
| `basic` | `run` | 3.25 MHz | 4.89 MHz | +50% |
| `basic` | `run_cached` | 5.83 MHz | 8.26 MHz | +42% |
| `mixed` | `run` | 3.18 MHz | 4.71 MHz | +48% |
| `mixed` | `run_cached` | 5.36 MHz | 7.83 MHz | +46% |
| `realish` | `run` | 2.38 MHz | 3.22 MHz | +35% |
| `realish` | `run_cached` | 5.08 MHz | 7.15 MHz | +41% |

The baseline build used cache statistics and allocated the 64 KB `RamBus` in
PSRAM on that board. The fast build disabled cache statistics, marked the hot
cached executor for IRAM, and allocated both CPU and bus state from internal
heap.

### Real-code cached benchmark

`klaus_cached_benchmark` measures the cached executor on real 6502 code instead
of fixed loops: it runs the full Klaus functional test through `run_cached()`
and reports throughput plus how much of the program executed in cached IR vs
the interpreter fallback. It needs the external test image:

```sh
sh scripts/fetch_external_tests.sh
cmake --build build-release --target klaus_cached_benchmark
./build-release/klaus_cached_benchmark third_party/klaus/6502_functional_test.bin
```

It also runs as a `ctest` test when the image is present, so the cached path is
validated on the whole functional suite. Sample run on an Android ARM64 phone
(Termux, clang `-O3`, ~31.0M executed instructions; throughput ranges come from
interleaved old/new runs, best-of-6, to bound the thermal governor variance):

| Build | Instructions in IR | Throughput |
| --- | ---: | ---: |
| before the cached-block speedups (commit `4a316d9`) | 1.4% | 23-45 M instr/s |
| current | 99.998% | 97-145 M instr/s |

The remaining fallback is the handful of blocks containing `BRK` (510 of
31.0M instructions here); it stays in the reference interpreter by design. Self-modifying regions of the test invalidate and
re-decode blocks on the fly (~0.3M invalidations in this run).

`run_cached()` now uses a small executable cached payload for selected hot
documented opcodes and falls back to the reference interpreter path for the rest.
The cached IR covers every documented opcode except `BRK`:
loads/stores, the full ALU set in all addressing modes, accumulator and memory
shifts/rotates, stack operations, `BIT`, `INC`/`DEC`, `JSR`/`RTS`/`RTI`, and
both indirect modes. `STA (zp,X)` and `STA (zp),Y` check the computed target
against the executing block's own bytes at runtime and bail out to the
interpreter for the rest of the block when a store modifies its own code.
Cache slots are hashed on the full PC instead of just its low byte, so
page-aligned blocks no longer collide in slot zero, and executed blocks link
directly to their successors so consecutive block executions skip the slot
lookup. Self-modifying code remains conservative: cached blocks record their
exact byte range, a write invalidates only blocks whose bytes contain the
written address, and blocks with static writes overlapping their own bytes do
not use the executable payload. Direct 64 KB RAM buses use a faster read/write path, including the
default 6510 profile; accesses to `$0000/$0001` keep the normal 6510 port
semantics while other addresses use the direct RAM pointer. Executable cached
blocks use a tight direct-memory loop with local CPU registers. During decode,
selected adjacent branch pairs such as `DEX`/`BPL`, `DEY`/`BNE`, and
`CMP #`/`BCC` are fused so the hot loop can execute both instructions with one
dispatch when the caller's instruction budget includes the pair. The `realish`
diagnostic stays fully in IR with fused compare/decrement branches on its inner
loop. The cached dispatcher marks hot hits and executable direct-memory blocks
as the likely path so branch prediction does not overpay for cold misses and
fallback cases. The `profile` benchmark mode runs the `realish` cached workload
and prints the cache histogram when `J6510_ENABLE_CACHE_STATS` is enabled.

Current decision: cached IR covers every documented opcode except `BRK`,
including the NMOS `JMP ($xxFF)` indirect-jump bug behavior, and `run_cached`
chains consecutive cached blocks instead of re-looking them up. The undocumented families, cycle accounting in the cached
path, and any further specialization should wait for a real workload histogram.

## External CPU Test Suites

The repository does not vendor third-party test binaries or source files.
Download them locally with:

```sh
./scripts/fetch_external_tests.sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

When present, CTest also runs:

- Klaus Dormann `6502_functional_test.bin`
- Bruce Clark / Klaus decimal-mode test for BCD `ADC/SBC`
- Klaus Dormann interrupt test through a test-only feedback register adapter at
  `$BFFC`

## Interrupt Adapters

The core only understands logical CPU lines:

- `request_reset()`
- `pulse_nmi()`
- `set_irq_level(bool)`

Platform-specific sources are expected to live outside the core and drive those
lines from `set_interrupt_poll_callback(...)`. The callback runs on instruction
boundaries before pending interrupts are serviced.

For desktop tests, `tests/klaus_interrupt_main.cpp` polls a memory-mapped
feedback register at `$BFFC`. For an Arduino or ESP32 adapter, an ISR should set
a small host-side flag only; the poll callback can then translate that flag into
`pulse_nmi()` or `set_irq_level(true)` on the next instruction boundary.

## License

The emulator code in this repository is licensed under the MIT License. External
test assets downloaded by `scripts/fetch_external_tests.sh` keep their original
upstream licenses and are not part of this repository.
