#include "cpu6510_bus.h"
#include "cpu6510_core.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace j6510;

namespace {

void load_image_at(RamBus& bus, const char* path, uint16_t base) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "failed to open Klaus decimal test image: " << path << '\n';
        std::exit(2);
    }

    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (base + bytes.size() > bus.memory.size()) {
        std::cerr << "decimal image does not fit memory\n";
        std::exit(2);
    }

    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bus.memory[static_cast<uint16_t>(base + i)] = static_cast<uint8_t>(bytes[i]);
    }
}

} // namespace

int main(int argc, char** argv) {
    const char* image_path = argc > 1 ? argv[1] : "third_party/klaus/6502_decimal_test.bin";
    RamBus bus;
    load_image_at(bus, image_path, 0x0200);

    Cpu6510 cpu(bus, Cpu6510Config{false});
    cpu.state().pc = 0x0200;
    cpu.state().sp = 0xFD;
    cpu.state().p = FLAG_U | FLAG_I;

    constexpr uint16_t done_pc = 0x024B;
    constexpr uint16_t error_addr = 0x000B;
    constexpr uint64_t max_steps = 200000000;

    for (uint64_t steps = 0; steps < max_steps; ++steps) {
        if (cpu.state().pc == done_pc) {
            if (bus.memory[error_addr] == 0) {
                std::cout << "Klaus decimal test passed after " << std::dec << steps << " instructions\n";
                return 0;
            }
            std::cerr << "Klaus decimal test failed with error byte $"
                      << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bus.memory[error_addr])
                      << '\n';
            return 1;
        }

        const StepResult result = cpu.step();
        if (result != StepResult::Ok) {
            std::cerr << "Klaus decimal test hit illegal opcode at $"
                      << std::hex << std::setw(4) << std::setfill('0') << cpu.state().pc << '\n';
            return 1;
        }
    }

    std::cerr << "Klaus decimal test exceeded max instruction budget at $"
              << std::hex << std::setw(4) << std::setfill('0') << cpu.state().pc << '\n';
    return 1;
}
