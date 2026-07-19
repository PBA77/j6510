// Measurable real-code benchmark for the cached-block executor.
//
// Runs the Klaus 6502 functional test through run_cached() instead of step()
// and reports pass/fail, throughput, and how much of the program actually
// executed in cached IR vs the interpreter fallback. The functional test is a
// good stand-in for real 6502 workloads: dense branches, subroutines, decimal
// arithmetic, and self-modifying code spread over a 64 KB image.

#include "cpu6510_bus.h"
#include "cpu6510_core.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

using namespace j6510;

namespace {

void load_image(RamBus& bus, const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "failed to open Klaus functional test image: " << path << '\n';
        std::exit(2);
    }

    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() != bus.memory.size()) {
        std::cerr << "expected 64 KB image, got " << bytes.size() << " bytes\n";
        std::exit(2);
    }

    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bus.memory[i] = static_cast<uint8_t>(bytes[i]);
    }
}

} // namespace

int main(int argc, char** argv) {
    const char* image_path = argc > 1 ? argv[1] : "third_party/klaus/6502_functional_test.bin";
    RamBus bus;
    load_image(bus, image_path);

    Cpu6510 cpu(bus, Cpu6510Config{false});
    cpu.state().pc = 0x0400;
    cpu.state().sp = 0xFD;
    cpu.state().p = FLAG_U | FLAG_I;

    constexpr uint16_t success_pc = 0x3469;
    constexpr uint64_t max_instructions = 100000000;
    uint64_t executed = 0;
    uint16_t prev_stop = 0xFFFF;
    int stuck_slices = 0;
    // A genuine trap loop keeps the same stop_pc across slices of any size,
    // while a long running loop only repeats stop_pc when the slice happens to
    // be a multiple of its period. Alternating slice sizes avoids false traps.
    constexpr uint32_t slice_sizes[] = {100000, 99991, 100037};

    const auto start = std::chrono::steady_clock::now();
    while (executed < max_instructions) {
        RunResult result = cpu.run_cached(slice_sizes[stuck_slices % 3]);
        executed += result.instructions_executed;
        if (result.result != StepResult::Ok) {
            std::cerr << "Klaus cached benchmark hit illegal opcode, test_case=$" << std::hex
                      << static_cast<int>(bus.memory[0x0200]) << '\n';
            return 1;
        }
        stuck_slices = (result.stop_pc == prev_stop) ? stuck_slices + 1 : 0;
        if (stuck_slices >= 3) {
            const auto finish = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(finish - start).count();
            if (result.stop_pc == success_pc) {
                const BlockCacheStats& stats = cpu.block_cache_stats();
                const uint64_t total = stats.ir_instructions + stats.fallback_instructions;
                const double ir_share = total > 0 ? 100.0 * stats.ir_instructions / total : 0.0;
                std::cout << "Klaus cached benchmark passed after ~" << executed << " instructions\n";
                std::cout << "elapsed: " << seconds << " s\n";
                std::cout << "throughput: " << executed / seconds / 1e6 << " M instr/s\n";
                std::cout << "ir/fallback instructions: " << stats.ir_instructions << " / "
                          << stats.fallback_instructions << " (" << ir_share << "% in IR)\n";
                std::cout << "cache hits/misses/invalidations: " << stats.hits << " / " << stats.misses
                          << " / " << stats.invalidations << '\n';
                return 0;
            }
            std::cerr << "Klaus cached benchmark trapped at $" << std::hex << result.stop_pc
                      << " test_case=$" << static_cast<int>(bus.memory[0x0200]) << '\n';
            return 1;
        }
        prev_stop = result.stop_pc;
    }

    std::cerr << "Klaus cached benchmark exceeded max instruction budget\n";
    return 1;
}
