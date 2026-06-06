#include "cpu6510_bus.h"
#include "cpu6510_core.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <iomanip>
#include <iostream>
#include <string>

using namespace j6510;

namespace {

uint64_t parse_iterations(int argc, char** argv, const std::string& mode) {
    if (argc < 3) {
        if (mode == "realish" || mode == "profile") {
            return 100000;
        }
        return 5000000;
    }

    char* end = nullptr;
    const unsigned long long value = std::strtoull(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || value == 0) {
        std::cerr << "usage: j6510_benchmark [step|run|block|cached|both|mixed|realish|profile] [positive_iterations]\n";
        std::exit(2);
    }
    return static_cast<uint64_t>(value);
}

std::string parse_mode(int argc, char** argv) {
    if (argc < 2) {
        return "both";
    }

    std::string mode = argv[1];
    if (mode != "step" && mode != "run" && mode != "block" && mode != "cached" && mode != "both" &&
        mode != "mixed" && mode != "realish" && mode != "profile") {
        std::cerr << "usage: j6510_benchmark [step|run|block|cached|both|mixed|realish|profile] [positive_iterations]\n";
        std::exit(2);
    }
    return mode;
}

void load_benchmark_program(RamBus& bus) {
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA9, 0x55,       // LDA #$55     2 cycles
        0x8D, 0x00, 0x30, // STA $3000    4 cycles
        0xAE, 0x00, 0x30, // LDX $3000    4 cycles
        0xE8,             // INX          2 cycles
        0x8E, 0x01, 0x30, // STX $3001    4 cycles
        0xAD, 0x01, 0x30, // LDA $3001    4 cycles
        0xAA,             // TAX          2 cycles
        0xCA,             // DEX          2 cycles
        0x4C, 0x00, 0x02, // JMP $0200    3 cycles
    };
    bus.load(0x0200, program, sizeof(program));
}

void load_mixed_program(RamBus& bus) {
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA2, 0x08,       // LDX #$08      2 cycles
        0xA0, 0x04,       // LDY #$04      2 cycles
        0xA9, 0x10,       // LDA #$10      2 cycles
        0x85, 0x20,       // STA $20       3 cycles
        0xB5, 0x18,       // LDA $18,X     4 cycles
        0xE8,             // INX           2 cycles
        0x95, 0x18,       // STA $18,X     4 cycles
        0xB9, 0x00, 0x30, // LDA $3000,Y   4 cycles
        0x99, 0x10, 0x30, // STA $3010,Y   5 cycles
        0x88,             // DEY           2 cycles
        0xD0, 0xF6,       // BNE $020C     3 cycles taken / 2 not taken
        0xCA,             // DEX           2 cycles
        0x8A,             // TXA           2 cycles
        0xAA,             // TAX           2 cycles
        0x4C, 0x00, 0x02, // JMP $0200     3 cycles
    };
    bus.load(0x0200, program, sizeof(program));
    bus.memory[0x3020] = 0x44;
    bus.memory[0x3021] = 0x55;
    bus.memory[0x3022] = 0x66;
    bus.memory[0x3023] = 0x77;
    bus.memory[0x3024] = 0x88;
}

void load_realish_program(RamBus& bus) {
    bus.set_reset_vector(0x0400);
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
    bus.load(0x0400, program, sizeof(program));
    for (uint16_t i = 0; i < 0x20; ++i) {
        bus.memory[static_cast<uint16_t>(0x4000 + i)] = static_cast<uint8_t>(0x90 + i);
    }
}

void print_result(const char* mode, double seconds, uint64_t total_instructions, uint64_t total_cycles) {
    const double instructions_per_second = static_cast<double>(total_instructions) / seconds;
    const double equivalent_mhz = static_cast<double>(total_cycles) / seconds / 1000000.0;

    std::cout << std::fixed << std::setprecision(2)
              << "j6510 benchmark (" << mode << ")\n"
              << "  instructions: " << total_instructions << '\n'
              << "  nominal 6502 cycles: " << total_cycles << '\n'
              << "  elapsed: " << seconds << " s\n"
              << "  throughput: " << (instructions_per_second / 1000000.0) << " M instr/s\n"
              << "  6502 equivalent: " << equivalent_mhz << " MHz\n";
}

void print_cache_stats(const BlockCacheStats& stats) {
    std::cout << "  cache hits/misses/invalidations: "
              << stats.hits << '/' << stats.misses << '/' << stats.invalidations << '\n'
              << "  cache ir/fallback blocks: "
              << stats.ir_blocks << '/' << stats.fallback_blocks << '\n'
              << "  cache ir/fallback instructions: "
              << stats.ir_instructions << '/' << stats.fallback_instructions << '\n';

    if (stats.fallback_instructions == 0) {
        return;
    }

    std::array<bool, 256> printed{};
    std::cout << "  top fallback opcodes:\n";
    for (int rank = 0; rank < 8; ++rank) {
        int best_opcode = -1;
        uint64_t best_count = 0;
        for (int opcode = 0; opcode < 256; ++opcode) {
            if (!printed[opcode] && stats.fallback_opcodes[opcode] > best_count) {
                best_count = stats.fallback_opcodes[opcode];
                best_opcode = opcode;
            }
        }
        if (best_opcode < 0 || best_count == 0) {
            break;
        }
        printed[best_opcode] = true;
        std::cout << "    $" << std::hex << std::setw(2) << std::setfill('0') << best_opcode
                  << std::dec << std::setfill(' ') << ": " << best_count << '\n';
    }

    printed.fill(false);
    std::cout << "  top unsupported fallback opcodes:\n";
    for (int rank = 0; rank < 8; ++rank) {
        int best_opcode = -1;
        uint64_t best_count = 0;
        for (int opcode = 0; opcode < 256; ++opcode) {
            if (!printed[opcode] && stats.unsupported_fallback_opcodes[opcode] > best_count) {
                best_count = stats.unsupported_fallback_opcodes[opcode];
                best_opcode = opcode;
            }
        }
        if (best_opcode < 0 || best_count == 0) {
            break;
        }
        printed[best_opcode] = true;
        std::cout << "    $" << std::hex << std::setw(2) << std::setfill('0') << best_opcode
                  << std::dec << std::setfill(' ') << ": " << best_count << '\n';
    }
}

void run_step_benchmark(uint64_t total_instructions, uint64_t total_cycles) {
    RamBus bus;
    load_benchmark_program(bus);
    Cpu6510 cpu(bus, Cpu6510Config{false});
    cpu.reset();

    const auto start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < total_instructions; ++i) {
        if (cpu.step() != StepResult::Ok) {
            std::cerr << "step benchmark hit illegal opcode at PC=$"
                      << std::hex << std::setw(4) << std::setfill('0') << cpu.state().pc << '\n';
            std::exit(1);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    print_result("step", std::chrono::duration<double>(end - start).count(), total_instructions, total_cycles);
}

void run_batch_benchmark(uint64_t total_instructions, uint64_t total_cycles) {
    if (total_instructions > std::numeric_limits<uint32_t>::max()) {
        std::cerr << "run benchmark instruction count exceeds uint32_t batch API limit\n";
        std::exit(2);
    }

    RamBus bus;
    load_benchmark_program(bus);
    Cpu6510 cpu(bus, Cpu6510Config{false});
    cpu.reset();

    const auto start = std::chrono::steady_clock::now();
    const RunResult result = cpu.run(static_cast<uint32_t>(total_instructions));
    const auto end = std::chrono::steady_clock::now();

    if (result.result != StepResult::Ok || result.instructions_executed != total_instructions) {
        std::cerr << "run benchmark stopped early at PC=$"
                  << std::hex << std::setw(4) << std::setfill('0') << result.stop_pc << '\n';
        std::exit(1);
    }
    print_result("run", std::chrono::duration<double>(end - start).count(), total_instructions, total_cycles);
}

void run_block_benchmark(uint64_t total_instructions, uint64_t total_cycles) {
    RamBus bus;
    load_benchmark_program(bus);
    Cpu6510 cpu(bus, Cpu6510Config{false});
    cpu.reset();

    uint64_t executed = 0;
    const auto start = std::chrono::steady_clock::now();
    while (executed < total_instructions) {
        const uint32_t remaining = static_cast<uint32_t>(total_instructions - executed);
        const BlockRunResult result = cpu.run_block(remaining);
        if (result.result != StepResult::Ok) {
            std::cerr << "block benchmark stopped with error at PC=$"
                      << std::hex << std::setw(4) << std::setfill('0') << result.stop_pc << '\n';
            std::exit(1);
        }
        executed += result.instructions_executed;
        if (result.instructions_executed == 0) {
            std::cerr << "block benchmark made no progress at PC=$"
                      << std::hex << std::setw(4) << std::setfill('0') << result.stop_pc << '\n';
            std::exit(1);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    print_result("block", std::chrono::duration<double>(end - start).count(), total_instructions, total_cycles);
}

void run_cached_benchmark(uint64_t total_instructions, uint64_t total_cycles) {
    if (total_instructions > std::numeric_limits<uint32_t>::max()) {
        std::cerr << "cached benchmark instruction count exceeds uint32_t batch API limit\n";
        std::exit(2);
    }

    RamBus bus;
    load_benchmark_program(bus);
    Cpu6510 cpu(bus, Cpu6510Config{false});
    cpu.reset();

    const auto start = std::chrono::steady_clock::now();
    const RunResult result = cpu.run_cached(static_cast<uint32_t>(total_instructions));
    const auto end = std::chrono::steady_clock::now();

    if (result.result != StepResult::Ok || result.instructions_executed != total_instructions) {
        std::cerr << "cached benchmark stopped early at PC=$"
                  << std::hex << std::setw(4) << std::setfill('0') << result.stop_pc << '\n';
        std::exit(1);
    }
    print_result("cached", std::chrono::duration<double>(end - start).count(), total_instructions, total_cycles);
    print_cache_stats(cpu.block_cache_stats());
}

void run_program_benchmark(uint64_t iterations, const char* suite, const char* mode, uint64_t instructions_per_iteration,
                           uint64_t cycles_per_iteration, void (*load_program)(RamBus&)) {
    const uint64_t total_instructions = iterations * instructions_per_iteration;
    const uint64_t total_cycles = iterations * cycles_per_iteration;

    RamBus bus;
    load_program(bus);
    Cpu6510 cpu(bus, Cpu6510Config{false});
    cpu.reset();

    const auto start = std::chrono::steady_clock::now();
    if (std::string(mode) == "run") {
        if (total_instructions > std::numeric_limits<uint32_t>::max()) {
            std::cerr << suite << " run benchmark instruction count exceeds uint32_t batch API limit\n";
            std::exit(2);
        }
        const RunResult result = cpu.run(static_cast<uint32_t>(total_instructions));
        if (result.result != StepResult::Ok || result.instructions_executed != total_instructions) {
            std::cerr << suite << " run benchmark stopped early at PC=$"
                      << std::hex << std::setw(4) << std::setfill('0') << result.stop_pc << '\n';
            std::exit(1);
        }
    } else if (std::string(mode) == "cached") {
        if (total_instructions > std::numeric_limits<uint32_t>::max()) {
            std::cerr << suite << " cached benchmark instruction count exceeds uint32_t batch API limit\n";
            std::exit(2);
        }
        const RunResult result = cpu.run_cached(static_cast<uint32_t>(total_instructions));
        if (result.result != StepResult::Ok || result.instructions_executed != total_instructions) {
            std::cerr << suite << " cached benchmark stopped early at PC=$"
                      << std::hex << std::setw(4) << std::setfill('0') << result.stop_pc << '\n';
            std::exit(1);
        }
    } else if (std::string(mode) == "block") {
        uint64_t executed = 0;
        while (executed < total_instructions) {
            const uint32_t remaining = static_cast<uint32_t>(total_instructions - executed);
            const BlockRunResult result = cpu.run_block(remaining);
            if (result.result != StepResult::Ok) {
                std::cerr << suite << " block benchmark stopped with error at PC=$"
                          << std::hex << std::setw(4) << std::setfill('0') << result.stop_pc << '\n';
                std::exit(1);
            }
            executed += result.instructions_executed;
            if (result.instructions_executed == 0) {
                std::cerr << suite << " block benchmark made no progress at PC=$"
                          << std::hex << std::setw(4) << std::setfill('0') << result.stop_pc << '\n';
                std::exit(1);
            }
        }
    } else {
        for (uint64_t i = 0; i < total_instructions; ++i) {
            if (cpu.step() != StepResult::Ok) {
                std::cerr << suite << " step benchmark hit illegal opcode at PC=$"
                          << std::hex << std::setw(4) << std::setfill('0') << cpu.state().pc << '\n';
                std::exit(1);
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();
    std::string label = suite;
    label += ' ';
    label += mode;
    print_result(label.c_str(), std::chrono::duration<double>(end - start).count(), total_instructions, total_cycles);
    if (std::string(mode) == "cached") {
        print_cache_stats(cpu.block_cache_stats());
    }
}

void run_mixed_benchmark(uint64_t iterations, const char* mode) {
    run_program_benchmark(iterations, "mixed", mode, 30, 101, load_mixed_program);
}

void run_realish_benchmark(uint64_t iterations, const char* mode) {
    run_program_benchmark(iterations, "realish", mode, 292, 873, load_realish_program);
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = parse_mode(argc, argv);
    const uint64_t iterations = parse_iterations(argc, argv, mode);

    constexpr uint64_t instructions_per_iteration = 9;
    constexpr uint64_t cycles_per_iteration = 27;
    const uint64_t total_instructions = iterations * instructions_per_iteration;
    const uint64_t total_cycles = iterations * cycles_per_iteration;

    std::cout << "iterations: " << iterations << '\n';
    if (mode == "profile") {
        run_realish_benchmark(iterations, "cached");
    } else if (mode == "mixed") {
        run_mixed_benchmark(iterations, "step");
        run_mixed_benchmark(iterations, "run");
        run_mixed_benchmark(iterations, "block");
        run_mixed_benchmark(iterations, "cached");
        return 0;
    }
    if (mode == "realish") {
        run_realish_benchmark(iterations, "step");
        run_realish_benchmark(iterations, "run");
        run_realish_benchmark(iterations, "block");
        run_realish_benchmark(iterations, "cached");
        return 0;
    }

    if (mode == "step" || mode == "both") {
        run_step_benchmark(total_instructions, total_cycles);
    }
    if (mode == "run" || mode == "both") {
        run_batch_benchmark(total_instructions, total_cycles);
    }
    if (mode == "block" || mode == "both") {
        run_block_benchmark(total_instructions, total_cycles);
    }
    if (mode == "cached" || mode == "both") {
        run_cached_benchmark(total_instructions, total_cycles);
    }

    return 0;
}
