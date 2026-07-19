// Lockstep validation of the cached executor against the reference interpreter.
//
// Executes the Klaus functional test on two CPUs in parallel: one stepping
// through step(), one through run_cached(1). After every instruction the full
// CPU state must match exactly. Any divergence points at a cached-IR bug with
// the precise program counter and opcode.

#include "cpu6510_bus.h"
#include "cpu6510_core.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
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

void print_state(const char* tag, const Cpu6510& cpu) {
    const Cpu6510State& s = cpu.state();
    std::cerr << tag << " pc=$" << std::hex << std::setw(4) << std::setfill('0') << s.pc << " a=$"
              << std::setw(2) << static_cast<int>(s.a) << " x=$" << std::setw(2) << static_cast<int>(s.x)
              << " y=$" << std::setw(2) << static_cast<int>(s.y) << " sp=$" << std::setw(2)
              << static_cast<int>(s.sp) << " p=$" << std::setw(2) << static_cast<int>(s.p) << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const char* image_path = argc > 1 ? argv[1] : "third_party/klaus/6502_functional_test.bin";
    const uint64_t max_instructions = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 50000000;

    RamBus step_bus;
    RamBus cached_bus;
    load_image(step_bus, image_path);
    load_image(cached_bus, image_path);

    Cpu6510 reference(step_bus, Cpu6510Config{false});
    Cpu6510 cached(cached_bus, Cpu6510Config{false});
    reference.state().pc = 0x0400;
    reference.state().sp = 0xFD;
    reference.state().p = FLAG_U | FLAG_I;
    cached.state().pc = 0x0400;
    cached.state().sp = 0xFD;
    cached.state().p = FLAG_U | FLAG_I;

    constexpr uint16_t success_pc = 0x3469;
    for (uint64_t step_index = 1; step_index <= max_instructions; ++step_index) {
        const uint16_t pc_before = reference.state().pc;
        if (reference.step() != StepResult::Ok) {
            std::cerr << "reference interpreter hit illegal opcode at $" << std::hex << pc_before << '\n';
            return 1;
        }
        const RunResult result = cached.run_cached(1);
        if (result.result != StepResult::Ok) {
            std::cerr << "cached executor hit illegal opcode at $" << std::hex << pc_before << '\n';
            return 1;
        }
        const Cpu6510State& a = reference.state();
        const Cpu6510State& b = cached.state();
        if (a.pc != b.pc || a.a != b.a || a.x != b.x || a.y != b.y || a.sp != b.sp || a.p != b.p) {
            std::cerr << "DIVERGENCE at instruction " << std::dec << step_index << " pc=$" << std::hex
                      << std::setw(4) << std::setfill('0') << pc_before << " opcode=$" << std::setw(2)
                      << static_cast<int>(step_bus.memory[pc_before]) << '\n';
            print_state("reference: ", reference);
            print_state("cached:    ", cached);
            return 1;
        }
        if (a.pc == pc_before) {
            if (a.pc == success_pc) {
                std::cout << "Klaus lockstep passed: step() and run_cached() agree for " << std::dec
                          << step_index << " instructions\n";
                return 0;
            }
            std::cerr << "both executors trapped early at $" << std::hex << a.pc << '\n';
            return 1;
        }
    }

    std::cerr << "lockstep exceeded instruction budget without reaching the success trap\n";
    return 1;
}
