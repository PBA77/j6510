// Embedded benchmark sketch for the j6510 core.
//
// The core sources are included directly so the example can be built without a
// separate Arduino library package.

#ifndef J6510_ENABLE_BLOCK_CACHE
#define J6510_ENABLE_BLOCK_CACHE 1
#endif

#include <cstdlib>
#include <new>

#include <Arduino.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <Esp.h>
#endif

// Arduino defines DEC for Serial formatting and interrupts() as a macro.
// The CPU core uses Operation::DEC and Cpu6510::interrupts(), so clear those
// names before including the platform-neutral core.
#ifdef DEC
#undef DEC
#endif

#ifdef interrupts
#undef interrupts
#endif

#include "../../src/cpu6510_bus.h"
#include "../../src/cpu6510_core.h"

#include "../../src/cpu6510_bus.cpp"
#include "../../src/cpu6510_opcode_table.cpp"
#include "../../src/cpu6510_core.cpp"

using namespace j6510;

RamBus* bus = nullptr;
Cpu6510* cpu = nullptr;

#if !defined(ARDUINO_ARCH_ESP32)
RamBus static_bus;
Cpu6510 static_cpu(static_bus, Cpu6510Config{false});
#endif

struct BenchmarkCase {
    const char* name;
    uint32_t iterations;
    uint32_t instructions_per_iteration;
    uint32_t cycles_per_iteration;
    void (*load_program)();
};

void clear_bus() {
    bus->memory.fill(0);
    cpu->clear_block_cache();
}

void load_basic_program() {
    clear_bus();
    bus->set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA9, 0x55,       // LDA #$55
        0x8D, 0x00, 0x30, // STA $3000
        0xAE, 0x00, 0x30, // LDX $3000
        0xE8,             // INX
        0x8E, 0x01, 0x30, // STX $3001
        0xAD, 0x01, 0x30, // LDA $3001
        0xAA,             // TAX
        0xCA,             // DEX
        0x4C, 0x00, 0x02, // JMP $0200
    };
    bus->load(0x0200, program, sizeof(program));
}

void load_mixed_program() {
    clear_bus();
    bus->set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA2, 0x08,       // LDX #$08
        0xA0, 0x04,       // LDY #$04
        0xA9, 0x10,       // LDA #$10
        0x85, 0x20,       // STA $20
        0xB5, 0x18,       // LDA $18,X
        0xE8,             // INX
        0x95, 0x18,       // STA $18,X
        0xB9, 0x00, 0x30, // LDA $3000,Y
        0x99, 0x10, 0x30, // STA $3010,Y
        0x88,             // DEY
        0xD0, 0xF6,       // BNE $020C
        0xCA,             // DEX
        0x8A,             // TXA
        0xAA,             // TAX
        0x4C, 0x00, 0x02, // JMP $0200
    };
    bus->load(0x0200, program, sizeof(program));
    bus->memory[0x3020] = 0x44;
    bus->memory[0x3021] = 0x55;
    bus->memory[0x3022] = 0x66;
    bus->memory[0x3023] = 0x77;
    bus->memory[0x3024] = 0x88;
}

void load_realish_program() {
    clear_bus();
    bus->set_reset_vector(0x0400);
    const uint8_t program[] = {
        0xA2, 0x1F,       // LDX #$1F
        0xA9, 0x00,       // LDA #$00
        0x85, 0x30,       // STA $30
        0xBD, 0x00, 0x40, // LDA $4000,X
        0x18,             // CLC
        0x69, 0x03,       // ADC #$03
        0x9D, 0x80, 0x40, // STA $4080,X
        0xC9, 0x80,       // CMP #$80
        0x90, 0x02,       // BCC $0414
        0xE6, 0x30,       // INC $30
        0xCA,             // DEX
        0x10, 0xEE,       // BPL $0406
        0x4C, 0x00, 0x04, // JMP $0400
    };
    bus->load(0x0400, program, sizeof(program));
    for (uint16_t i = 0; i < 0x20; ++i) {
        bus->memory[static_cast<uint16_t>(0x4000 + i)] = static_cast<uint8_t>(0x90 + i);
    }
}

void print_u64(uint64_t value) {
    char buffer[21];
    char* cursor = buffer + sizeof(buffer);
    *--cursor = '\0';
    if (value == 0) {
        *--cursor = '0';
    } else {
        while (value != 0 && cursor != buffer) {
            *--cursor = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
    }
    Serial.print(cursor);
}

void print_result(const char* suite, const char* mode, uint64_t instructions, uint64_t cycles, uint32_t elapsed_us) {
    const double seconds = static_cast<double>(elapsed_us) / 1000000.0;
    const double mips = static_cast<double>(instructions) / seconds / 1000000.0;
    const double mhz6502 = static_cast<double>(cycles) / seconds / 1000000.0;

    Serial.print(suite);
    Serial.print(' ');
    Serial.print(mode);
    Serial.print(": instr=");
    print_u64(instructions);
    Serial.print(" elapsed_us=");
    Serial.print(elapsed_us);
    Serial.print(" mips=");
    Serial.print(mips, 2);
    Serial.print(" 6502_mhz=");
    Serial.println(mhz6502, 2);
}

void run_one(const BenchmarkCase& bench, bool cached) {
    const uint64_t instructions = static_cast<uint64_t>(bench.iterations) * bench.instructions_per_iteration;
    const uint64_t cycles = static_cast<uint64_t>(bench.iterations) * bench.cycles_per_iteration;

    bench.load_program();
    cpu->reset();

    const uint32_t start = micros();
    const RunResult result =
        cached ? cpu->run_cached(static_cast<uint32_t>(instructions)) : cpu->run(static_cast<uint32_t>(instructions));
    const uint32_t elapsed = micros() - start;

    if (result.result != StepResult::Ok || result.instructions_executed != static_cast<uint32_t>(instructions)) {
        Serial.print(bench.name);
        Serial.print(cached ? " cached" : " run");
        Serial.print(": stopped early pc=$");
        Serial.println(result.stop_pc, HEX);
        return;
    }

    print_result(bench.name, cached ? "cached" : "run", instructions, cycles, elapsed);
}

void initialize_emulator() {
#if defined(ARDUINO_ARCH_ESP32)
    void* bus_memory = nullptr;
    void* cpu_memory = nullptr;
    bool bus_in_psram = false;
    bool cpu_in_psram = false;

#if defined(J6510_ESP32_PREFER_INTERNAL_RAM) && J6510_ESP32_PREFER_INTERNAL_RAM
    bus_memory = std::malloc(sizeof(RamBus));
#else
    if (psramFound()) {
        bus_memory = ps_malloc(sizeof(RamBus));
        bus_in_psram = bus_memory != nullptr;
    }
#endif

    cpu_memory = std::malloc(sizeof(Cpu6510));

    if (bus_memory == nullptr) {
        bus_memory = std::malloc(sizeof(RamBus));
    }
    if (bus_memory == nullptr && psramFound()) {
        bus_memory = ps_malloc(sizeof(RamBus));
        bus_in_psram = bus_memory != nullptr;
    }
    if (cpu_memory == nullptr && psramFound()) {
        cpu_memory = ps_malloc(sizeof(Cpu6510));
        cpu_in_psram = cpu_memory != nullptr;
    }
    if (bus_memory == nullptr || cpu_memory == nullptr) {
        Serial.println("failed to allocate emulator state");
        while (true) {
            delay(1000);
        }
    }
    bus = new (bus_memory) RamBus();
    cpu = new (cpu_memory) Cpu6510(*bus, Cpu6510Config{false});
    Serial.print("alloc bus=");
    Serial.print(bus_in_psram ? "psram" : "heap");
    Serial.print(" cpu=");
    Serial.print(cpu_in_psram ? "psram" : "heap");
    Serial.print(" sizeof_bus=");
    Serial.print(sizeof(RamBus));
    Serial.print(" sizeof_cpu=");
    Serial.println(sizeof(Cpu6510));
#else
    bus = &static_bus;
    cpu = &static_cpu;
#endif
}

void run_benchmarks() {
    const BenchmarkCase benches[] = {
#if defined(ARDUINO_ARCH_RP2040)
        {"basic", 200000, 9, 27, load_basic_program},
        {"mixed", 100000, 15, 41, load_mixed_program},
        {"realish", 8000, 292, 873, load_realish_program},
#else
        {"basic", 1000000, 9, 27, load_basic_program},
        {"mixed", 500000, 15, 41, load_mixed_program},
        {"realish", 40000, 292, 873, load_realish_program},
#endif
    };

#if defined(ARDUINO_ARCH_RP2040)
    Serial.println("j6510 RP2040 Pico benchmark");
#elif defined(ARDUINO_ARCH_ESP32)
    Serial.println("j6510 ESP32 benchmark");
#else
    Serial.println("j6510 Teensy 4.0 benchmark");
#endif
    Serial.print("J6510_ENABLE_BLOCK_CACHE=");
    Serial.println(J6510_ENABLE_BLOCK_CACHE);
#if J6510_ENABLE_BLOCK_CACHE
    Serial.print("J6510_ENABLE_CACHE_STATS=");
    Serial.println(J6510_ENABLE_CACHE_STATS);
    Serial.print("J6510_BLOCK_CACHE_SLOTS=");
    Serial.println(J6510_BLOCK_CACHE_SLOTS);
    Serial.print("J6510_CACHED_BLOCK_MAX_OPS=");
    Serial.println(J6510_CACHED_BLOCK_MAX_OPS);
#endif
#if defined(ARDUINO_ARCH_ESP32)
    Serial.print("heap free=");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" max_alloc=");
    Serial.print(ESP.getMaxAllocHeap());
    Serial.print(" psram_found=");
    Serial.print(psramFound() ? "yes" : "no");
    Serial.print(" psram_size=");
    Serial.print(ESP.getPsramSize());
    Serial.print(" psram_free=");
    Serial.println(ESP.getFreePsram());
#endif

    for (const BenchmarkCase& bench : benches) {
        run_one(bench, false);
        run_one(bench, true);
    }
    Serial.println("benchmark done");
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
    }
    delay(1000);
    initialize_emulator();
    run_benchmarks();
}

void loop() {
    static uint32_t last_report_ms = 0;
    const uint32_t now = millis();
    if (now - last_report_ms >= 15000) {
        last_report_ms = now;
        run_benchmarks();
    }
}
