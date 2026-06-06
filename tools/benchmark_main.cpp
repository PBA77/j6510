#include "cpu6510_bus.h"
#include "cpu6510_core.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <iomanip>
#include <iostream>
#include <string>

using namespace j6510;

namespace {

uint64_t parse_iterations(int argc, char** argv) {
    if (argc < 3) {
        return 5000000;
    }

    char* end = nullptr;
    const unsigned long long value = std::strtoull(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || value == 0) {
        std::cerr << "usage: j6510_benchmark [step|run|both] [positive_iterations]\n";
        std::exit(2);
    }
    return static_cast<uint64_t>(value);
}

std::string parse_mode(int argc, char** argv) {
    if (argc < 2) {
        return "both";
    }

    std::string mode = argv[1];
    if (mode != "step" && mode != "run" && mode != "both") {
        std::cerr << "usage: j6510_benchmark [step|run|both] [positive_iterations]\n";
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

} // namespace

int main(int argc, char** argv) {
    const std::string mode = parse_mode(argc, argv);
    const uint64_t iterations = parse_iterations(argc, argv);

    constexpr uint64_t instructions_per_iteration = 9;
    constexpr uint64_t cycles_per_iteration = 27;
    const uint64_t total_instructions = iterations * instructions_per_iteration;
    const uint64_t total_cycles = iterations * cycles_per_iteration;

    std::cout << "iterations: " << iterations << '\n';
    if (mode == "step" || mode == "both") {
        run_step_benchmark(total_instructions, total_cycles);
    }
    if (mode == "run" || mode == "both") {
        run_batch_benchmark(total_instructions, total_cycles);
    }

    return 0;
}
