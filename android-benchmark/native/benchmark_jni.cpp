#include <jni.h>

#include "cpu6510_bus.h"
#include "cpu6510_core.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

using namespace j6510;

namespace {

constexpr uint64_t kInstructionsPerIteration = 9;
constexpr uint64_t kCyclesPerIteration = 27;

void load_benchmark_program(RamBus& bus) {
    bus.set_reset_vector(0x0200);
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
    bus.load(0x0200, program, sizeof(program));
}

struct Measurement {
    double seconds = 0.0;
    bool valid = false;
    BlockCacheStats cache_stats{};
};

template <typename Runner>
Measurement measure(Runner runner) {
    auto bus = std::make_unique<RamBus>();
    load_benchmark_program(*bus);
    auto cpu = std::make_unique<Cpu6510>(*bus, Cpu6510Config{false});
    cpu->reset();

    const auto start = std::chrono::steady_clock::now();
    const bool valid = runner(*cpu);
    const auto end = std::chrono::steady_clock::now();

    Measurement result;
    result.seconds = std::chrono::duration<double>(end - start).count();
    result.valid = valid;
    result.cache_stats = cpu->block_cache_stats();
    return result;
}

void append_result(std::ostringstream& output,
                   const char* label,
                   const Measurement& result,
                   uint64_t instructions,
                   uint64_t cycles) {
    output << std::left << std::setw(12) << label;
    if (!result.valid || result.seconds <= 0.0) {
        output << " BŁĄD\n";
        return;
    }

    const double mips = static_cast<double>(instructions) / result.seconds / 1'000'000.0;
    const double mhz = static_cast<double>(cycles) / result.seconds / 1'000'000.0;
    output << std::right << std::fixed << std::setprecision(2)
           << std::setw(9) << mhz << " MHz  "
           << std::setw(8) << mips << " MIPS  "
           << std::setw(7) << result.seconds << " s\n";
}

std::string run_all(uint64_t iterations) {
    const uint64_t instructions = iterations * kInstructionsPerIteration;
    const uint64_t cycles = iterations * kCyclesPerIteration;
    const uint32_t budget = static_cast<uint32_t>(instructions);

    const Measurement step = measure([instructions](Cpu6510& cpu) {
        for (uint64_t i = 0; i < instructions; ++i) {
            if (cpu.step() != StepResult::Ok) {
                return false;
            }
        }
        return true;
    });

    const Measurement run = measure([budget](Cpu6510& cpu) {
        const RunResult result = cpu.run(budget);
        return result.result == StepResult::Ok && result.instructions_executed == budget;
    });

    const Measurement block = measure([instructions](Cpu6510& cpu) {
        uint64_t executed = 0;
        while (executed < instructions) {
            const uint32_t remaining = static_cast<uint32_t>(instructions - executed);
            const BlockRunResult result = cpu.run_block(remaining);
            if (result.result != StepResult::Ok || result.instructions_executed == 0) {
                return false;
            }
            executed += result.instructions_executed;
        }
        return true;
    });

    const Measurement cached = measure([budget](Cpu6510& cpu) {
        const RunResult result = cpu.run_cached(budget);
        return result.result == StepResult::Ok && result.instructions_executed == budget;
    });

    std::ostringstream output;
    output << "j6510 • ARM64 • Release -O3\n"
           << "Iteracje: " << iterations << "\n"
           << "Instrukcje: " << instructions << "\n"
           << "Cykle 6502: " << cycles << "\n\n"
           << "TRYB          6502 EQ.      PRĘDKOŚĆ     CZAS\n"
           << "------------------------------------------------\n";

    append_result(output, "step()", step, instructions, cycles);
    append_result(output, "run()", run, instructions, cycles);
    append_result(output, "run_block()", block, instructions, cycles);
    append_result(output, "run_cached()", cached, instructions, cycles);

    output << "\nCache:\n"
           << "  hits: " << cached.cache_stats.hits << "\n"
           << "  misses: " << cached.cache_stats.misses << "\n"
           << "  invalidations: " << cached.cache_stats.invalidations << "\n"
           << "  IR instructions: " << cached.cache_stats.ir_instructions << "\n"
           << "  fallback instructions: " << cached.cache_stats.fallback_instructions << "\n";
    return output.str();
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_pba77_j6510benchmark_MainActivity_runBenchmark(
        JNIEnv* env, jclass, jlong iterations) {
    const uint64_t safe_iterations =
            iterations < 1 ? 1 : static_cast<uint64_t>(iterations);
    const std::string result = run_all(safe_iterations);
    return env->NewStringUTF(result.c_str());
}
