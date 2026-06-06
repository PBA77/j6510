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
pio run -e teensy40-benchmark -t upload
pio device monitor -b 115200
```

If PlatformIO Core is installed but `pio` is not on `PATH`, use
`~/.platformio/penv/bin/pio` or add `~/.platformio/penv/bin` to your shell path.

The `esp32s2-saola-fast` environment builds the same sketch with `-O3` and marks
the cached executor for IRAM placement through `J6510_FAST_CODE_ATTR=IRAM_ATTR`.

This is not a full C64 target. It does not include VIC-II, SID, CIA, keyboard,
video, storage, or real host interrupt wiring.

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
```

Current local Release baseline after the first cached-block IR pass, measured
on an Apple M1 Max with `./scripts/run_release_benchmarks.sh`:

```text
both iterations: 20000000
instructions: 180000000
nominal 6502 cycles: 540000000
step 6502 equivalent:   ~438 MHz
run 6502 equivalent:    ~647 MHz
block 6502 equivalent:  ~446 MHz
cached 6502 equivalent: ~1390 MHz
cached hits/misses/invalidations: 19999999/1/0
cached IR/fallback instructions: 180000000/0

mixed iterations: 10000000
mixed step equivalent:   ~502 MHz
mixed run equivalent:    ~727 MHz
mixed block equivalent:  ~499 MHz
mixed cached equivalent: ~1165 MHz
mixed cached hits/misses/invalidations: 49999997/3/0
mixed cached IR/fallback instructions: 300000000/0

realish iterations: 200000
realish step equivalent:   ~417 MHz
realish run equivalent:    ~433 MHz
realish block equivalent:  ~329 MHz
realish cached equivalent: ~946 MHz
realish cached hits/misses/invalidations: 12999996/4/0
realish cached IR/fallback instructions: 58400000/0
```

`run_cached()` now uses a small executable cached payload for selected hot
documented opcodes and falls back to the reference interpreter path for the rest.
Self-modifying code remains conservative: cached blocks are invalidated by page,
and blocks with static writes to their own code page do not use the executable
payload. Direct 64 KB RAM buses use a faster read/write path when the 6510 port
is disabled, and executable cached blocks use a tight direct-memory loop with
local CPU registers. The `realish` diagnostic now stays fully in IR after adding
the previously dominant `ADC #`, `CMP #`, and `INC zp` cached operations.

Current decision: the easy cached-IR wins are mostly consumed. Further widening
should wait for a real workload histogram or for a deliberate larger design such
as partial fallback inside executable blocks.

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
