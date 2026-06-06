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

void print_hex_state(const Cpu6510& cpu, const RamBus& bus, uint64_t steps, uint16_t loop_pc) {
    const auto& s = cpu.state();
    std::cerr << "steps=" << std::dec << steps
              << " loop_pc=$" << std::hex << std::setw(4) << std::setfill('0') << loop_pc
              << " test_case=$" << std::setw(2) << static_cast<int>(bus.memory[0x0200])
              << " A=$" << std::setw(2) << static_cast<int>(s.a)
              << " X=$" << std::setw(2) << static_cast<int>(s.x)
              << " Y=$" << std::setw(2) << static_cast<int>(s.y)
              << " SP=$" << std::setw(2) << static_cast<int>(s.sp)
              << " P=$" << std::setw(2) << static_cast<int>(s.p)
              << '\n';
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
    constexpr uint64_t max_steps = 100000000;
    for (uint64_t steps = 0; steps < max_steps; ++steps) {
        const uint16_t pc_before = cpu.state().pc;
        const StepResult result = cpu.step();
        if (result != StepResult::Ok) {
            std::cerr << "Klaus functional test hit illegal opcode at $"
                      << std::hex << std::setw(4) << std::setfill('0') << cpu.state().pc << '\n';
            print_hex_state(cpu, bus, steps, pc_before);
            return 1;
        }
        if (cpu.state().pc == pc_before) {
            if (pc_before == success_pc) {
                std::cout << "Klaus functional test passed after " << std::dec << (steps + 1) << " instructions\n";
                return 0;
            }
            print_hex_state(cpu, bus, steps + 1, pc_before);
            return 1;
        }
    }

    std::cerr << "Klaus functional test exceeded max instruction budget\n";
    print_hex_state(cpu, bus, max_steps, cpu.state().pc);
    return 1;
}
