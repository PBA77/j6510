# j6510

`j6510` is a small, local-first MOS 6510 / NMOS 6502 CPU emulator core written
in C++17.

The project is intentionally focused on the CPU before any full Commodore 64
machine emulation. There is no VIC-II, SID, CIA, C64 memory mapper, Arduino
adapter, ESP-IDF adapter, or host ISR code in the core.

## What Works

- All documented NMOS 6502 opcodes are implemented.
- Illegal or undocumented opcodes return a controlled `IllegalOpcode` result.
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

## Build And Test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

This always runs the local unit and E2E tests.

## Benchmark

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target j6510_benchmark
./build-release/j6510_benchmark both
```

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

Current local Release baseline after the first cached-block IR pass:

```text
iterations: 10000000
instructions: 90000000
nominal 6502 cycles: 270000000
step 6502 equivalent:   ~444 MHz
run 6502 equivalent:    ~651 MHz
block 6502 equivalent:  ~460 MHz
cached 6502 equivalent: ~1448 MHz
cached hits/misses/invalidations: 9999999/1/0
cached IR/fallback instructions: 90000000/0

mixed step equivalent:   ~501 MHz
mixed run equivalent:    ~715 MHz
mixed block equivalent:  ~500 MHz
mixed cached equivalent: ~1468 MHz
mixed cached hits/misses/invalidations: 24999997/3/0
mixed cached IR/fallback instructions: 150000000/0

realish step equivalent:   ~406 MHz
realish run equivalent:    ~439 MHz
realish block equivalent:  ~332 MHz
realish cached equivalent: ~272 MHz
realish cached IR/fallback instructions: 100000/29100000
realish unsupported fallback opcodes: $69 ADC #, $C9 CMP #, $E6 INC zp
```

`run_cached()` now uses a small executable cached payload for selected hot
documented opcodes and falls back to the reference interpreter path for the rest.
Self-modifying code remains conservative: cached blocks are invalidated by page,
and blocks with static writes to their own code page do not use the executable
payload. Direct 64 KB RAM buses use a faster read/write path when the 6510 port
is disabled, and executable cached blocks use a tight direct-memory loop with
local CPU registers. This backend is currently treated as a frozen optimization
stage: synthetic hot loops are fully covered by IR, while the `realish` diagnostic
shows that adding more IR should be driven by unsupported fallback opcode data,
not by guessing.

Current decision: do not keep widening IR blindly. If the next optimization pass
is needed, the first measurable candidates are `ADC #`, `CMP #`, and `INC zp`,
because they dominate unsupported fallback in the `realish` program.

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
