#include "cpu6510_bus.h"
#include "cpu6510_core.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace j6510;

namespace {

constexpr uint16_t kProgramStart = 0x0400;
constexpr uint16_t kSuccessTrap = 0x06F5;
constexpr uint16_t kFeedbackRegister = 0xBFFC;
constexpr uint8_t kIrqBit = 0x01;
constexpr uint8_t kNmiBit = 0x02;

void load_image_at(RamBus& bus, const char* path, uint16_t base) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "failed to open Klaus interrupt test image: " << path << '\n';
        std::exit(2);
    }

    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (base + bytes.size() > bus.memory.size()) {
        std::cerr << "interrupt image does not fit memory\n";
        std::exit(2);
    }

    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bus.memory[static_cast<uint16_t>(base + i)] = static_cast<uint8_t>(bytes[i]);
    }
}

void print_state(const Cpu6510& cpu, const RamBus& bus, uint64_t steps, uint16_t pc_before) {
    const auto& s = cpu.state();
    std::cerr << "steps=" << std::dec << steps
              << " pc_before=$" << std::hex << std::setw(4) << std::setfill('0') << pc_before
              << " pc=$" << std::setw(4) << s.pc
              << " feedback=$" << std::setw(2) << static_cast<int>(bus.memory[kFeedbackRegister])
              << " A=$" << std::setw(2) << static_cast<int>(s.a)
              << " X=$" << std::setw(2) << static_cast<int>(s.x)
              << " Y=$" << std::setw(2) << static_cast<int>(s.y)
              << " SP=$" << std::setw(2) << static_cast<int>(s.sp)
              << " P=$" << std::setw(2) << static_cast<int>(s.p)
              << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const char* image_path = argc > 1 ? argv[1] : "third_party/klaus/6502_interrupt_test.bin";
    RamBus bus;
    load_image_at(bus, image_path, 0x000A);
    bus.memory[kFeedbackRegister] = 0;

    Cpu6510 cpu(bus, Cpu6510Config{false});
    cpu.state().pc = kProgramStart;
    cpu.state().sp = 0xFD;
    cpu.state().p = FLAG_U | FLAG_I;

    uint8_t previous_feedback = bus.memory[kFeedbackRegister];
    cpu.set_interrupt_poll_callback([&](Cpu6510& polled_cpu) {
        const uint8_t feedback = bus.memory[kFeedbackRegister];
        if ((feedback & kNmiBit) != 0 && (previous_feedback & kNmiBit) == 0) {
            polled_cpu.pulse_nmi();
        }
        polled_cpu.set_irq_level((feedback & kIrqBit) != 0);
        previous_feedback = feedback;
    });

    constexpr uint64_t max_steps = 10000000;
    for (uint64_t steps = 0; steps < max_steps; ++steps) {
        const uint16_t pc_before = cpu.state().pc;
        const StepResult result = cpu.step();
        if (result != StepResult::Ok) {
            std::cerr << "Klaus interrupt test hit illegal opcode\n";
            print_state(cpu, bus, steps, pc_before);
            return 1;
        }
        if (cpu.state().pc == pc_before) {
            if (pc_before == kSuccessTrap) {
                std::cout << "Klaus interrupt test passed after " << std::dec << (steps + 1) << " instructions\n";
                return 0;
            }
            print_state(cpu, bus, steps + 1, pc_before);
            return 1;
        }
    }

    std::cerr << "Klaus interrupt test exceeded max instruction budget\n";
    print_state(cpu, bus, max_steps, cpu.state().pc);
    return 1;
}
