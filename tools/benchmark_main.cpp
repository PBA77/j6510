#include "cpu6510_bus.h"
#include "cpu6510_core.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>

using namespace j6510;

namespace {

uint64_t parse_iterations(int argc, char** argv) {
    if (argc < 2) {
        return 5000000;
    }

    char* end = nullptr;
    const unsigned long long value = std::strtoull(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || value == 0) {
        std::cerr << "usage: j6510_benchmark [positive_iterations]\n";
        std::exit(2);
    }
    return static_cast<uint64_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    const uint64_t iterations = parse_iterations(argc, argv);

    RamBus bus;
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

    Cpu6510 cpu(bus, Cpu6510Config{false});
    cpu.reset();

    constexpr uint64_t instructions_per_iteration = 9;
    constexpr uint64_t cycles_per_iteration = 27;
    const uint64_t total_instructions = iterations * instructions_per_iteration;
    const uint64_t total_cycles = iterations * cycles_per_iteration;

    const auto start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < total_instructions; ++i) {
        if (cpu.step() != StepResult::Ok) {
            std::cerr << "benchmark hit illegal opcode at PC=$"
                      << std::hex << std::setw(4) << std::setfill('0') << cpu.state().pc << '\n';
            return 1;
        }
    }
    const auto end = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(end - start).count();
    const double instructions_per_second = static_cast<double>(total_instructions) / seconds;
    const double equivalent_mhz = static_cast<double>(total_cycles) / seconds / 1000000.0;

    std::cout << std::fixed << std::setprecision(2)
              << "j6510 benchmark\n"
              << "  iterations: " << iterations << '\n'
              << "  instructions: " << total_instructions << '\n'
              << "  nominal 6502 cycles: " << total_cycles << '\n'
              << "  elapsed: " << seconds << " s\n"
              << "  throughput: " << (instructions_per_second / 1000000.0) << " M instr/s\n"
              << "  6502 equivalent: " << equivalent_mhz << " MHz\n";

    return 0;
}
