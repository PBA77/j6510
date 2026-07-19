#include "cpu6510_bus.h"
#include "cpu6510_core.h"
#include "cpu6510_opcode_table.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace j6510;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void run_steps(Cpu6510& cpu, int steps, const std::string& context) {
    for (int i = 0; i < steps; ++i) {
        require(cpu.step() == StepResult::Ok, context + " step " + std::to_string(i));
    }
}

void require_same_state(const Cpu6510State& lhs, const Cpu6510State& rhs, const std::string& context) {
    require(lhs.a == rhs.a, context + " A matches");
    require(lhs.x == rhs.x, context + " X matches");
    require(lhs.y == rhs.y, context + " Y matches");
    require(lhs.sp == rhs.sp, context + " SP matches");
    require(lhs.pc == rhs.pc, context + " PC matches");
    require(lhs.p == rhs.p, context + " P matches");
}

struct BusEvent {
    char type = 'R';
    uint16_t address = 0;
    uint8_t value = 0;
};

class SpyBus final : public Bus {
public:
    uint8_t read(uint16_t address) override {
        const uint8_t value = memory[address];
        events.push_back(BusEvent{'R', address, value});
        return value;
    }

    void write(uint16_t address, uint8_t value) override {
        memory[address] = value;
        events.push_back(BusEvent{'W', address, value});
    }

    void load(uint16_t start, const uint8_t* data, uint16_t size) {
        for (uint16_t i = 0; i < size; ++i) {
            memory[static_cast<uint16_t>(start + i)] = data[i];
        }
    }

    void set_reset_vector(uint16_t address) {
        memory[0xFFFC] = static_cast<uint8_t>(address & 0x00FF);
        memory[0xFFFD] = static_cast<uint8_t>(address >> 8);
    }

    std::array<uint8_t, 65536> memory{};
    std::vector<BusEvent> events;
};

void require_event(const std::vector<BusEvent>& events, size_t index, char type, uint16_t address, uint8_t value, const std::string& context) {
    require(index < events.size(), context + " has event " + std::to_string(index));
    require(events[index].type == type, context + " event type " + std::to_string(index));
    require(events[index].address == address, context + " event address " + std::to_string(index));
    require(events[index].value == value, context + " event value " + std::to_string(index));
}

void load_realish_program(RamBus& bus, uint16_t address) {
    const uint8_t program[] = {
        0xA2, 0x1F,       // LDX #$1F
        0xA9, 0x00,       // LDA #$00
        0x85, 0x30,       // STA $30
        0xBD, 0x00, 0x40, // LDA $4000,X
        0x18,             // CLC
        0x69, 0x03,       // ADC #$03
        0x9D, 0x80, 0x40, // STA $4080,X
        0xC9, 0x80,       // CMP #$80
        0x90, 0x02,       // BCC over INC
        0xE6, 0x30,       // INC $30
        0xCA,             // DEX
        0x10, 0xEE,       // BPL loop
        0x4C, 0x00, 0x04, // JMP $0400
    };
    bus.load(address, program, sizeof(program));
    for (uint16_t i = 0; i < 0x20; ++i) {
        bus.memory[static_cast<uint16_t>(0x4000 + i)] = static_cast<uint8_t>(0x90 + i);
    }
}

void test_reset_loads_pc_from_vector() {
    RamBus bus;
    bus.set_reset_vector(0xC000);
    Cpu6510 cpu(bus);

    cpu.reset();

    require(cpu.state().pc == 0xC000, "reset loads PC from FFFC/FFFD");
    require(cpu.state().sp == 0xFD, "reset initializes stack pointer");
    require((cpu.state().p & FLAG_I) != 0, "reset sets interrupt disable");
}

void test_stack_wraps_on_page_one() {
    RamBus bus;
    Cpu6510 cpu(bus);
    cpu.state().sp = 0x00;

    cpu.push(0xAB);
    require(bus.memory[0x0100] == 0xAB, "push writes to 0x0100 | SP");
    require(cpu.state().sp == 0xFF, "push wraps SP as 8-bit");
    require(cpu.pull() == 0xAB, "pull reads wrapped stack value");
    require(cpu.state().sp == 0x00, "pull restores SP");
}

void test_opcode_metadata_and_illegal_opcode() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    bus.memory[0x0200] = 0x02;
    Cpu6510 cpu(bus);
    cpu.reset();

    require(opcode_info(0xA9).operation == Operation::LDA, "opcode table exposes LDA immediate");
    require(opcode_info(0x02).operation == Operation::Illegal, "opcode table marks illegal opcode");
    require(cpu.step() == StepResult::IllegalOpcode, "illegal opcode returns controlled error");
    require(cpu.state().pc == 0x0200, "illegal opcode leaves PC on offending opcode");
}

void test_opcode_table_has_all_legal_opcodes() {
    const uint8_t legal_opcodes[] = {
        0x00, 0x01, 0x05, 0x06, 0x08, 0x09, 0x0A, 0x0D, 0x0E,
        0x10, 0x11, 0x15, 0x16, 0x18, 0x19, 0x1D, 0x1E,
        0x20, 0x21, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2A, 0x2C, 0x2D, 0x2E,
        0x30, 0x31, 0x35, 0x36, 0x38, 0x39, 0x3D, 0x3E,
        0x40, 0x41, 0x45, 0x46, 0x48, 0x49, 0x4A, 0x4C, 0x4D, 0x4E,
        0x50, 0x51, 0x55, 0x56, 0x58, 0x59, 0x5D, 0x5E,
        0x60, 0x61, 0x65, 0x66, 0x68, 0x69, 0x6A, 0x6C, 0x6D, 0x6E,
        0x70, 0x71, 0x75, 0x76, 0x78, 0x79, 0x7D, 0x7E,
        0x81, 0x84, 0x85, 0x86, 0x88, 0x8A, 0x8C, 0x8D, 0x8E,
        0x90, 0x91, 0x94, 0x95, 0x96, 0x98, 0x99, 0x9A, 0x9D,
        0xA0, 0xA1, 0xA2, 0xA4, 0xA5, 0xA6, 0xA8, 0xA9, 0xAA, 0xAC, 0xAD, 0xAE,
        0xB0, 0xB1, 0xB4, 0xB5, 0xB6, 0xB8, 0xB9, 0xBA, 0xBC, 0xBD, 0xBE,
        0xC0, 0xC1, 0xC4, 0xC5, 0xC6, 0xC8, 0xC9, 0xCA, 0xCC, 0xCD, 0xCE,
        0xD0, 0xD1, 0xD5, 0xD6, 0xD8, 0xD9, 0xDD, 0xDE,
        0xE0, 0xE1, 0xE4, 0xE5, 0xE6, 0xE8, 0xE9, 0xEA, 0xEC, 0xED, 0xEE,
        0xF0, 0xF1, 0xF5, 0xF6, 0xF8, 0xF9, 0xFD, 0xFE,
    };

    bool legal[256] = {};
    for (uint8_t opcode : legal_opcodes) {
        legal[opcode] = true;
        require(opcode_info(opcode).operation != Operation::Illegal, "legal opcode is present in opcode table");
    }

    int legal_count = 0;
    int table_legal_count = 0;
    for (int opcode = 0; opcode < 256; ++opcode) {
        if (legal[opcode]) {
            ++legal_count;
        }
        if (opcode_info(static_cast<uint8_t>(opcode)).operation != Operation::Illegal) {
            ++table_legal_count;
            require(legal[opcode], "opcode table does not mark undocumented opcode as legal");
        }
    }
    require(legal_count == 151, "legal NMOS opcode fixture has 151 entries");
    require(table_legal_count == 151, "opcode table exposes exactly 151 legal opcodes");
}

void test_undocumented_opcode_profile_metadata() {
    require(opcode_info(0x07).operation == Operation::Illegal, "documented opcode table keeps SLO illegal");
    require(undocumented_opcode_info(0x07).operation == Operation::SLO, "undocumented table exposes SLO zero page");
    require(undocumented_opcode_info(0xA7).operation == Operation::LAX, "undocumented table exposes LAX zero page");
    require(undocumented_opcode_info(0xEB).operation == Operation::SBC, "undocumented table exposes unofficial SBC immediate");
    require(undocumented_opcode_info(0x1C).operation == Operation::NOP, "undocumented table exposes operand NOP");

    RamBus bus;
    bus.set_reset_vector(0x0200);
    bus.memory[0x0200] = 0x07;
    bus.memory[0x0201] = 0x10;
    Cpu6510 default_cpu(bus);
    default_cpu.reset();
    require(default_cpu.step() == StepResult::IllegalOpcode, "default CPU keeps undocumented opcode illegal");
    require(default_cpu.state().pc == 0x0200, "default illegal undocumented opcode leaves PC at opcode");

    Cpu6510 enabled_cpu(bus, Cpu6510Config{true, ExecutionMode::InstructionFast, true});
    enabled_cpu.reset();
    bus.memory[0x0010] = 0x41;
    require(enabled_cpu.step() == StepResult::Ok, "enabled CPU executes undocumented opcode");
    require(enabled_cpu.state().pc == 0x0202, "enabled undocumented opcode consumes operand");
}

void test_undocumented_opcode_families() {
    const auto run_one = [](uint8_t opcode,
                            uint8_t initial_a,
                            uint8_t initial_x,
                            uint8_t initial_p,
                            uint8_t memory_value,
                            uint8_t expected_a,
                            uint8_t expected_x,
                            uint8_t expected_memory,
                            uint8_t expected_set,
                            uint8_t expected_clear,
                            const std::string& context) {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t program[] = {opcode, 0x10};
        bus.load(0x0200, program, sizeof(program));
        bus.memory[0x0010] = memory_value;
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::InstructionFast, true});
        cpu.reset();
        cpu.state().a = initial_a;
        cpu.state().x = initial_x;
        cpu.state().p = static_cast<uint8_t>(initial_p | FLAG_U);

        require(cpu.step() == StepResult::Ok, context + " executes");
        require(cpu.state().pc == 0x0202, context + " advances PC");
        require(cpu.state().a == expected_a, context + " A");
        require(cpu.state().x == expected_x, context + " X");
        require(bus.memory[0x0010] == expected_memory, context + " memory");
        require((cpu.state().p & expected_set) == expected_set, context + " expected flags set");
        require((cpu.state().p & expected_clear) == 0, context + " expected flags clear");
    };

    run_one(0x07, 0x01, 0x00, 0, 0x41, 0x83, 0x00, 0x82, FLAG_N, FLAG_C | FLAG_Z, "SLO zp");
    run_one(0x27, 0xF0, 0x00, FLAG_C, 0x80, 0x00, 0x00, 0x01, FLAG_C | FLAG_Z, FLAG_N, "RLA zp");
    run_one(0x47, 0xFF, 0x00, 0, 0x03, 0xFE, 0x00, 0x01, FLAG_C | FLAG_N, FLAG_Z, "SRE zp");
    run_one(0x67, 0x10, 0x00, 0, 0x02, 0x11, 0x00, 0x01, 0, FLAG_C | FLAG_Z | FLAG_N | FLAG_V, "RRA zp");
    run_one(0x87, 0xF0, 0x0F, 0, 0xAA, 0xF0, 0x0F, 0x00, 0, FLAG_Z | FLAG_N | FLAG_C | FLAG_V, "SAX zp");
    run_one(0xA7, 0x00, 0x00, 0, 0x80, 0x80, 0x80, 0x80, FLAG_N, FLAG_Z, "LAX zp");
    run_one(0xC7, 0x10, 0x00, 0, 0x11, 0x10, 0x00, 0x10, FLAG_C | FLAG_Z, FLAG_N, "DCP zp");
    run_one(0xE7, 0x20, 0x00, FLAG_C, 0x0F, 0x10, 0x00, 0x10, FLAG_C, FLAG_Z | FLAG_N | FLAG_V, "ISC zp");

    {
        RamBus bus;
        bus.set_reset_vector(0x0300);
        const uint8_t program[] = {
            0xA9, 0x20, // LDA #$20
            0x38,       // SEC
            0xEB, 0x01, // unofficial SBC #$01
            0x80, 0xFF, // NOP #$FF
            0x04, 0x10, // NOP $10
            0x1A,       // NOP
        };
        bus.load(0x0300, program, sizeof(program));
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::InstructionFast, true});
        cpu.reset();
        run_steps(cpu, 6, "unofficial SBC and NOP family");
        require(cpu.state().a == 0x1F, "unofficial SBC immediate subtracts");
        require(cpu.state().pc == 0x030A, "NOP family consumes operands");
        require((cpu.state().p & FLAG_C) != 0, "unofficial SBC keeps carry when no borrow");
    }
}

void test_basic_instructions_end_to_end() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA9, 0x41,       // LDA #$41
        0xAA,             // TAX
        0xE8,             // INX
        0x8D, 0x00, 0x30, // STA $3000
        0xA5, 0x10,       // LDA $10
        0x85, 0x11,       // STA $11
        0xEA,             // NOP
        0x4C, 0x20, 0x02, // JMP $0220
    };
    bus.load(0x0200, program, sizeof(program));
    bus.memory[0x0010] = 0x80;
    bus.memory[0x0220] = 0xEA;
    Cpu6510 cpu(bus);
    cpu.reset();

    require(cpu.step() == StepResult::Ok, "LDA immediate executes");
    require(cpu.state().a == 0x41, "LDA immediate sets A");
    require((cpu.state().p & FLAG_Z) == 0, "LDA clears Z for non-zero");
    require((cpu.state().p & FLAG_N) == 0, "LDA clears N for positive");

    require(cpu.step() == StepResult::Ok, "TAX executes");
    require(cpu.state().x == 0x41, "TAX copies A to X");

    require(cpu.step() == StepResult::Ok, "INX executes");
    require(cpu.state().x == 0x42, "INX increments X");

    require(cpu.step() == StepResult::Ok, "STA absolute executes");
    require(bus.memory[0x3000] == 0x41, "STA absolute stores A");

    require(cpu.step() == StepResult::Ok, "LDA zero page executes");
    require(cpu.state().a == 0x80, "LDA zero page reads memory");
    require((cpu.state().p & FLAG_N) != 0, "LDA sets N for bit 7");

    require(cpu.step() == StepResult::Ok, "STA zero page executes");
    require(bus.memory[0x0011] == 0x80, "STA zero page stores A");

    require(cpu.step() == StepResult::Ok, "NOP executes");
    require(cpu.step() == StepResult::Ok, "JMP absolute executes");
    require(cpu.state().pc == 0x0220, "JMP absolute sets PC");
}

void test_jsr_and_rts() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0x20, 0x10, 0x02, // JSR $0210
        0xEA,             // NOP after return
    };
    bus.load(0x0200, program, sizeof(program));
    bus.memory[0x0210] = 0xA9;
    bus.memory[0x0211] = 0x55;
    bus.memory[0x0212] = 0x60;
    Cpu6510 cpu(bus);
    cpu.reset();

    require(cpu.step() == StepResult::Ok, "JSR executes");
    require(cpu.state().pc == 0x0210, "JSR jumps to target");
    require(cpu.state().sp == 0xFB, "JSR pushes two bytes");
    require(bus.memory[0x01FD] == 0x02, "JSR pushes return high byte");
    require(bus.memory[0x01FC] == 0x02, "JSR pushes return low byte");

    require(cpu.step() == StepResult::Ok, "subroutine LDA executes");
    require(cpu.step() == StepResult::Ok, "RTS executes");
    require(cpu.state().pc == 0x0203, "RTS returns to instruction after JSR");
}

void test_brk_and_rti() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    bus.set_irq_brk_vector(0x0300);
    bus.memory[0x0200] = 0x00;
    bus.memory[0x0201] = 0xEA;
    bus.memory[0x0300] = 0x40;
    Cpu6510 cpu(bus);
    cpu.reset();
    cpu.state().p = FLAG_U;

    require(cpu.step() == StepResult::Ok, "BRK executes");
    require(cpu.state().pc == 0x0300, "BRK jumps through IRQ/BRK vector");
    require(cpu.state().sp == 0xFA, "BRK pushes PC and status");
    require(bus.memory[0x01FD] == 0x02, "BRK pushes return high byte");
    require(bus.memory[0x01FC] == 0x02, "BRK pushes return low byte with padding byte skipped");
    require((bus.memory[0x01FB] & FLAG_B) != 0, "BRK pushes status with B set");

    require(cpu.step() == StepResult::Ok, "RTI executes");
    require(cpu.state().pc == 0x0202, "RTI restores BRK return PC");
    require((cpu.state().p & FLAG_B) == 0, "RTI does not keep B as internal status");
    require((cpu.state().p & FLAG_U) != 0, "RTI keeps unused bit set");
}

void test_interrupt_placeholder_state_and_service() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    bus.set_nmi_vector(0x0400);
    bus.set_irq_brk_vector(0x0500);
    Cpu6510 cpu(bus);

    cpu.request_reset();
    require(cpu.interrupts().reset_pending, "request_reset marks reset pending");
    cpu.service_pending_interrupt_if_needed();
    require(cpu.state().pc == 0x0200, "pending reset services reset vector");
    require(!cpu.interrupts().reset_pending, "reset pending clears after service");

    cpu.state().pc = 0x2222;
    cpu.pulse_nmi();
    require(cpu.interrupts().nmi_pending, "pulse_nmi marks NMI pending");
    cpu.service_pending_interrupt_if_needed();
    require(cpu.state().pc == 0x0400, "NMI services NMI vector");
    require(!cpu.interrupts().nmi_pending, "NMI pending clears after service");
    require((bus.memory[0x01FA] & FLAG_B) == 0, "NMI pushes status with B clear");

    cpu.state().pc = 0x3333;
    cpu.state().p = FLAG_U | FLAG_I;
    cpu.set_irq_level(true);
    cpu.service_pending_interrupt_if_needed();
    require(cpu.state().pc == 0x3333, "IRQ is ignored when I flag is set");
    cpu.state().p = FLAG_U;
    cpu.service_pending_interrupt_if_needed();
    require(cpu.state().pc == 0x0500, "IRQ services IRQ vector when I is clear");
    require(cpu.interrupts().irq_level, "IRQ level remains level-triggered");
    cpu.clear_irq_level();
    require(!cpu.interrupts().irq_level, "clear_irq_level clears IRQ line");
}

void test_interrupt_poll_callback_nmi_edge_and_irq_level() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    bus.set_nmi_vector(0x0300);
    bus.set_irq_brk_vector(0x0400);
    const uint8_t main_program[] = {
        0xEA, // $0200 NOP
        0x58, // $0201 CLI
        0xEA, // $0202 NOP, should be interrupted before execution
    };
    bus.load(0x0200, main_program, sizeof(main_program));
    bus.memory[0x0300] = 0x40; // RTI
    bus.memory[0x0400] = 0x40; // RTI

    Cpu6510 cpu(bus);
    cpu.reset();

    uint8_t feedback = 0;
    uint8_t previous_feedback = 0;
    int polls = 0;
    cpu.set_interrupt_poll_callback([&](Cpu6510& polled_cpu) {
        ++polls;
        if ((feedback & 0x02) != 0 && (previous_feedback & 0x02) == 0) {
            polled_cpu.pulse_nmi();
        }
        polled_cpu.set_irq_level((feedback & 0x01) != 0);
        previous_feedback = feedback;
    });

    feedback = 0x02;
    require(cpu.step() == StepResult::Ok, "NMI edge is polled before instruction");
    require(cpu.state().pc == 0x0200, "RTI returns to interrupted PC when NMI handler runs");
    require(cpu.state().sp == 0xFD, "NMI handler balances stack after RTI");
    require(polls == 1, "interrupt poll callback is called once per step");

    feedback = 0x02;
    require(cpu.step() == StepResult::Ok, "held NMI level does not retrigger without a new edge");
    require(cpu.state().pc == 0x0201, "held NMI level allows main program to continue");

    feedback = 0x01;
    require(cpu.step() == StepResult::Ok, "CLI executes while IRQ level is active but I was set at boundary");
    require(cpu.state().pc == 0x0202, "IRQ is not serviced until next instruction boundary after CLI");
    require((cpu.state().p & FLAG_I) == 0, "CLI clears I");

    require(cpu.step() == StepResult::Ok, "level IRQ is serviced at next boundary");
    require(cpu.state().pc == 0x0202, "IRQ RTI returns to interrupted PC");
    require((cpu.state().p & FLAG_I) == 0, "RTI restores pre-IRQ status");
}

void test_indexed_addressing_modes() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xB5, 0xF0,       // LDA $F0,X -> wraps to $0010
        0x9D, 0x00, 0x30, // STA $3000,X
        0xB9, 0x00, 0x40, // LDA $4000,Y
        0x99, 0x00, 0x50, // STA $5000,Y
    };
    bus.load(0x0200, program, sizeof(program));
    bus.memory[0x0010] = 0x44;
    bus.memory[0x4003] = 0x77;
    Cpu6510 cpu(bus);
    cpu.reset();
    cpu.state().x = 0x20;
    cpu.state().y = 0x03;

    require(cpu.step() == StepResult::Ok, "LDA zero page,X executes");
    require(cpu.state().a == 0x44, "LDA zero page,X wraps in zero page");
    require(cpu.step() == StepResult::Ok, "STA absolute,X executes");
    require(bus.memory[0x3020] == 0x44, "STA absolute,X stores at indexed address");
    require(cpu.step() == StepResult::Ok, "LDA absolute,Y executes");
    require(cpu.state().a == 0x77, "LDA absolute,Y reads indexed address");
    require(cpu.step() == StepResult::Ok, "STA absolute,Y executes");
    require(bus.memory[0x5003] == 0x77, "STA absolute,Y stores at indexed address");
}

void test_indirect_addressing_modes() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA1, 0x20, // LDA ($20,X)
        0x91, 0x30, // STA ($30),Y
        0xB1, 0x32, // LDA ($32),Y
        0x81, 0x40, // STA ($40,X)
    };
    bus.load(0x0200, program, sizeof(program));

    bus.memory[0x0024] = 0x00;
    bus.memory[0x0025] = 0x60;
    bus.memory[0x6000] = 0x12;

    bus.memory[0x0030] = 0x00;
    bus.memory[0x0031] = 0x70;

    bus.memory[0x0032] = 0x00;
    bus.memory[0x0033] = 0x80;
    bus.memory[0x8005] = 0x99;

    bus.memory[0x0044] = 0x00;
    bus.memory[0x0045] = 0x90;

    Cpu6510 cpu(bus);
    cpu.reset();
    cpu.state().x = 0x04;
    cpu.state().y = 0x05;

    require(cpu.step() == StepResult::Ok, "LDA indexed indirect executes");
    require(cpu.state().a == 0x12, "LDA indexed indirect resolves pointer after X wrap");
    require(cpu.step() == StepResult::Ok, "STA indirect indexed executes");
    require(bus.memory[0x7005] == 0x12, "STA indirect indexed stores at pointer plus Y");
    require(cpu.step() == StepResult::Ok, "LDA indirect indexed executes");
    require(cpu.state().a == 0x99, "LDA indirect indexed reads pointer plus Y");
    require(cpu.step() == StepResult::Ok, "STA indexed indirect executes");
    require(bus.memory[0x9000] == 0x99, "STA indexed indirect stores through wrapped pointer");
}

void test_zero_page_indirect_pointer_wraps() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA1, 0xFF, // LDA ($FF,X), X=0 reads pointer from $00/$01
    };
    bus.load(0x0200, program, sizeof(program));
    bus.memory[0x00FF] = 0xAA;
    Cpu6510 cpu(bus);
    cpu.reset();
    cpu.state().x = 0x01;
    bus.memory[0xF72F] = 0x5E;

    require(cpu.step() == StepResult::Ok, "indexed indirect zero page operand wraps");
    require(cpu.state().a == 0x5E, "indexed indirect pointer wraps through 6510 port addresses");
}

void test_relative_branches_forward_and_backward() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA9, 0x00, // LDA #$00 sets Z
        0xF0, 0x02, // BEQ +2 to $0206
        0xA9, 0x01, // skipped
        0xA9, 0x03, // LDA #$03 clears Z
        0xD0, 0xFA, // BNE -6 to $0204
    };
    bus.load(0x0200, program, sizeof(program));
    Cpu6510 cpu(bus);
    cpu.reset();

    require(cpu.step() == StepResult::Ok, "LDA #0 executes before forward branch");
    require(cpu.step() == StepResult::Ok, "BEQ forward executes");
    require(cpu.state().pc == 0x0206, "BEQ target is relative to address after branch instruction");
    require(cpu.step() == StepResult::Ok, "LDA after forward branch executes");
    require(cpu.state().a == 0x03, "forward branch skipped untaken path");
    require(cpu.step() == StepResult::Ok, "BNE backward executes");
    require(cpu.state().pc == 0x0204, "BNE supports negative relative offset");
}

void test_branch_conditions() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0x10, 0x01, // BPL taken
        0xEA,
        0x30, 0x01, // BMI taken
        0xEA,
        0x50, 0x01, // BVC taken
        0xEA,
        0x70, 0x01, // BVS taken
        0xEA,
        0x90, 0x01, // BCC taken
        0xEA,
        0xB0, 0x01, // BCS taken
        0xEA,
    };
    bus.load(0x0200, program, sizeof(program));
    Cpu6510 cpu(bus);
    cpu.reset();

    cpu.state().p = FLAG_U;
    require(cpu.step() == StepResult::Ok, "BPL executes");
    require(cpu.state().pc == 0x0203, "BPL branches when N clear");
    cpu.state().p = FLAG_U | FLAG_N;
    require(cpu.step() == StepResult::Ok, "BMI executes");
    require(cpu.state().pc == 0x0206, "BMI branches when N set");
    cpu.state().p = FLAG_U;
    require(cpu.step() == StepResult::Ok, "BVC executes");
    require(cpu.state().pc == 0x0209, "BVC branches when V clear");
    cpu.state().p = FLAG_U | FLAG_V;
    require(cpu.step() == StepResult::Ok, "BVS executes");
    require(cpu.state().pc == 0x020C, "BVS branches when V set");
    cpu.state().p = FLAG_U;
    require(cpu.step() == StepResult::Ok, "BCC executes");
    require(cpu.state().pc == 0x020F, "BCC branches when C clear");
    cpu.state().p = FLAG_U | FLAG_C;
    require(cpu.step() == StepResult::Ok, "BCS executes");
    require(cpu.state().pc == 0x0212, "BCS branches when C set");
}

void test_jmp_indirect_uses_nmos_page_wrap_bug() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0x6C, 0xFF, 0x30, // JMP ($30FF)
    };
    bus.load(0x0200, program, sizeof(program));
    bus.memory[0x30FF] = 0x34;
    bus.memory[0x3000] = 0x12;
    bus.memory[0x3100] = 0x99;
    Cpu6510 cpu(bus);
    cpu.reset();

    require(cpu.step() == StepResult::Ok, "JMP indirect executes");
    require(cpu.state().pc == 0x1234, "JMP ($xxFF) reads high byte from same page on NMOS 6502");
}

void test_ldx_ldy_and_store_modes() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA2, 0x80,       // LDX #$80
        0x86, 0x10,       // STX $10
        0x96, 0xF0,       // STX $F0,Y -> $11
        0x8E, 0x00, 0x30, // STX $3000
        0xA0, 0x00,       // LDY #$00
        0x84, 0x12,       // STY $12
        0x94, 0xF0,       // STY $F0,X -> $70
        0x8C, 0x01, 0x30, // STY $3001
        0xA6, 0x10,       // LDX $10
        0xB6, 0xF1,       // LDX $F1,Y -> $F1
        0xAE, 0x00, 0x30, // LDX $3000
        0xBE, 0x00, 0x40, // LDX $4000,Y
        0xA4, 0x12,       // LDY $12
        0xB4, 0xF0,       // LDY $F0,X
        0xAC, 0x01, 0x30, // LDY $3001
        0xBC, 0x00, 0x50, // LDY $5000,X
    };
    bus.load(0x0200, program, sizeof(program));
    bus.memory[0x00F1] = 0x7F;
    bus.memory[0x4000] = 0x55;
    bus.memory[0x5080] = 0x33;
    Cpu6510 cpu(bus);
    cpu.reset();
    cpu.state().y = 0x21;

    require(cpu.step() == StepResult::Ok, "LDX immediate executes");
    require(cpu.state().x == 0x80, "LDX immediate sets X");
    require((cpu.state().p & FLAG_N) != 0, "LDX sets N");
    const uint8_t flags_after_ldx = cpu.state().p;
    require(cpu.step() == StepResult::Ok, "STX zero page executes");
    require(bus.memory[0x0010] == 0x80, "STX zero page stores X");
    require(cpu.state().p == flags_after_ldx, "STX does not change flags");
    require(cpu.step() == StepResult::Ok, "STX zero page,Y executes");
    require(bus.memory[0x0011] == 0x80, "STX zero page,Y wraps in zero page");
    require(cpu.step() == StepResult::Ok, "STX absolute executes");
    require(bus.memory[0x3000] == 0x80, "STX absolute stores X");

    require(cpu.step() == StepResult::Ok, "LDY immediate executes");
    require(cpu.state().y == 0x00, "LDY immediate sets Y");
    require((cpu.state().p & FLAG_Z) != 0, "LDY sets Z");
    const uint8_t flags_after_ldy = cpu.state().p;
    require(cpu.step() == StepResult::Ok, "STY zero page executes");
    require(bus.memory[0x0012] == 0x00, "STY zero page stores Y");
    require(cpu.state().p == flags_after_ldy, "STY does not change flags");
    require(cpu.step() == StepResult::Ok, "STY zero page,X executes");
    require(bus.memory[0x0070] == 0x00, "STY zero page,X wraps in zero page");
    require(cpu.step() == StepResult::Ok, "STY absolute executes");
    require(bus.memory[0x3001] == 0x00, "STY absolute stores Y");

    require(cpu.step() == StepResult::Ok, "LDX zero page executes");
    require(cpu.state().x == 0x80, "LDX zero page reads memory");
    require(cpu.step() == StepResult::Ok, "LDX zero page,Y executes");
    require(cpu.state().x == 0x7F, "LDX zero page,Y reads indexed memory");
    require(cpu.step() == StepResult::Ok, "LDX absolute executes");
    require(cpu.state().x == 0x80, "LDX absolute reads memory");
    require(cpu.step() == StepResult::Ok, "LDX absolute,Y executes");
    require(cpu.state().x == 0x55, "LDX absolute,Y reads indexed memory");

    require(cpu.step() == StepResult::Ok, "LDY zero page executes");
    require(cpu.state().y == 0x00, "LDY zero page reads memory");
    cpu.state().x = 0x80;
    require(cpu.step() == StepResult::Ok, "LDY zero page,X executes");
    require(cpu.state().y == 0x00, "LDY zero page,X reads indexed memory");
    require(cpu.step() == StepResult::Ok, "LDY absolute executes");
    require(cpu.state().y == 0x00, "LDY absolute reads memory");
    require(cpu.step() == StepResult::Ok, "LDY absolute,X executes");
    require(cpu.state().y == 0x33, "LDY absolute,X reads indexed memory");
}

void test_transfer_instructions_and_flags() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA9, 0x80, // LDA #$80
        0xA8,       // TAY
        0x98,       // TYA
        0xAA,       // TAX
        0x8A,       // TXA
        0x9A,       // TXS
        0xBA,       // TSX
        0xA9, 0x00, // LDA #$00
        0xAA,       // TAX
    };
    bus.load(0x0200, program, sizeof(program));
    Cpu6510 cpu(bus);
    cpu.reset();

    run_steps(cpu, 2, "transfer TAY setup");
    require(cpu.state().y == 0x80, "TAY copies A to Y");
    require((cpu.state().p & FLAG_N) != 0, "TAY sets N");
    require(cpu.step() == StepResult::Ok, "TYA executes");
    require(cpu.state().a == 0x80, "TYA copies Y to A");
    require(cpu.step() == StepResult::Ok, "TAX executes");
    require(cpu.state().x == 0x80, "TAX copies A to X");
    require(cpu.step() == StepResult::Ok, "TXA executes");
    require(cpu.state().a == 0x80, "TXA copies X to A");
    const uint8_t flags_before_txs = cpu.state().p;
    require(cpu.step() == StepResult::Ok, "TXS executes");
    require(cpu.state().sp == 0x80, "TXS copies X to SP");
    require(cpu.state().p == flags_before_txs, "TXS does not change flags");
    require(cpu.step() == StepResult::Ok, "TSX executes");
    require(cpu.state().x == 0x80, "TSX copies SP to X");
    require((cpu.state().p & FLAG_N) != 0, "TSX sets N");
    run_steps(cpu, 2, "transfer zero TAX");
    require(cpu.state().x == 0x00, "TAX copies zero A to X");
    require((cpu.state().p & FLAG_Z) != 0, "TAX sets Z");
}

void test_stack_instructions_and_status_normalization() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA9, 0x80, // LDA #$80
        0x48,       // PHA
        0xA9, 0x00, // LDA #$00
        0x68,       // PLA
        0x08,       // PHP
        0x28,       // PLP
        0x08,       // PHP
    };
    bus.load(0x0200, program, sizeof(program));
    Cpu6510 cpu(bus);
    cpu.reset();

    require(cpu.step() == StepResult::Ok, "stack LDA setup executes");
    const uint8_t flags_after_lda = cpu.state().p;
    require(cpu.step() == StepResult::Ok, "PHA executes");
    require(bus.memory[0x01FD] == 0x80, "PHA pushes A");
    require(cpu.state().p == flags_after_lda, "PHA does not change flags");
    require(cpu.step() == StepResult::Ok, "stack LDA zero executes");
    require(cpu.step() == StepResult::Ok, "PLA executes");
    require(cpu.state().a == 0x80, "PLA pulls A");
    require((cpu.state().p & FLAG_N) != 0, "PLA sets N from pulled value");
    require((cpu.state().p & FLAG_Z) == 0, "PLA clears Z for non-zero value");

    cpu.state().p = static_cast<uint8_t>(FLAG_C | FLAG_Z);
    require(cpu.step() == StepResult::Ok, "PHP executes");
    require(bus.memory[0x01FD] == (FLAG_C | FLAG_Z | FLAG_B | FLAG_U), "PHP pushes B and unused bits set");
    bus.memory[0x01FD] = static_cast<uint8_t>(FLAG_N | FLAG_B);
    require(cpu.step() == StepResult::Ok, "PLP executes");
    require(cpu.state().p == (FLAG_N | FLAG_U), "PLP clears internal B and forces unused bit");
    require(cpu.step() == StepResult::Ok, "PHP after PLP executes");
    require(bus.memory[0x01FD] == (FLAG_N | FLAG_B | FLAG_U), "PHP still pushes B set after normalized PLP");
}

void test_e2e_stack_transfer_program() {
    RamBus bus;
    bus.set_reset_vector(0x0B00);
    const uint8_t program[] = {
        0xA9, 0x42,       // LDA #$42
        0x48,             // PHA
        0xA9, 0x00,       // LDA #$00
        0x68,             // PLA
        0xAA,             // TAX
        0x8A,             // TXA
        0x8D, 0x00, 0x42, // STA $4200
        0xA0, 0x99,       // LDY #$99
        0x8C, 0x01, 0x42, // STY $4201
        0x02,             // illegal sentinel
    };
    bus.load(0x0B00, program, sizeof(program));
    Cpu6510 cpu(bus);
    cpu.reset();

    run_steps(cpu, 9, "E2E stack transfer program");
    require(bus.memory[0x4200] == 0x42, "E2E stack program stores value transferred through stack and X");
    require(bus.memory[0x4201] == 0x99, "E2E stack program stores Y");
    require(cpu.state().sp == 0xFD, "E2E stack program restores stack pointer");
    require(cpu.step() == StepResult::IllegalOpcode, "E2E stack program reaches illegal sentinel");
}

void test_port6510_read_write_masking_and_callback() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    Cpu6510 cpu(bus);
    cpu.reset();

    require(cpu.port().ddr == 0x2F, "6510 port default DDR matches C64 profile");
    require(cpu.port().data == 0x37, "6510 port default DATA matches C64 profile");
    require(cpu.port().active_mask == 0x3F, "6510 port default active mask covers bits 0-5");
    require(cpu.port_output() == 0x27, "6510 port output reflects DATA & DDR & active mask");

    int callback_count = 0;
    uint8_t last_output = 0;
    cpu.set_port_changed_callback([&](uint8_t output) {
        ++callback_count;
        last_output = output;
    });

    const uint8_t program[] = {
        0xA9, 0x0F,       // LDA #$0F
        0x8D, 0x00, 0x00, // STA $0000
        0xA9, 0x05,       // LDA #$05
        0x8D, 0x01, 0x00, // STA $0001
        0xAD, 0x00, 0x00, // LDA $0000
        0x8D, 0x10, 0x30, // STA $3010
        0xAD, 0x01, 0x00, // LDA $0001
        0x8D, 0x11, 0x30, // STA $3011
    };
    bus.load(0x0200, program, sizeof(program));
    cpu.set_port_external_inputs(0xA0);

    run_steps(cpu, 8, "6510 port program");

    require(cpu.port().ddr == 0x0F, "write to $0000 updates port DDR");
    require(cpu.port().data == 0x05, "write to $0001 updates port DATA");
    require(bus.memory[0x3010] == 0x0F, "read $0000 returns DDR");
    require(bus.memory[0x3011] == 0xA5, "read $0001 combines output DATA and external inputs");
    require(callback_count >= 2, "port callback fires when output changes");
    require(last_output == 0x05, "port callback receives current masked output");

    cpu.set_port_active_mask(0x03);
    require(cpu.port_output() == 0x01, "active mask limits published output bits");
    require(last_output == 0x01, "active mask change can notify host mapper");
}

void test_flag_instructions() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0x38, // SEC
        0x18, // CLC
        0x78, // SEI
        0x58, // CLI
        0xF8, // SED
        0xD8, // CLD
        0xB8, // CLV
    };
    bus.load(0x0200, program, sizeof(program));
    Cpu6510 cpu(bus);
    cpu.reset();

    require(cpu.step() == StepResult::Ok, "SEC executes");
    require((cpu.state().p & FLAG_C) != 0, "SEC sets C");
    require(cpu.step() == StepResult::Ok, "CLC executes");
    require((cpu.state().p & FLAG_C) == 0, "CLC clears C");
    require(cpu.step() == StepResult::Ok, "SEI executes");
    require((cpu.state().p & FLAG_I) != 0, "SEI sets I");
    require(cpu.step() == StepResult::Ok, "CLI executes");
    require((cpu.state().p & FLAG_I) == 0, "CLI clears I");
    require(cpu.step() == StepResult::Ok, "SED executes");
    require((cpu.state().p & FLAG_D) != 0, "SED sets D");
    require(cpu.step() == StepResult::Ok, "CLD executes");
    require((cpu.state().p & FLAG_D) == 0, "CLD clears D");
    cpu.state().p |= FLAG_V;
    require(cpu.step() == StepResult::Ok, "CLV executes");
    require((cpu.state().p & FLAG_V) == 0, "CLV clears V");
}

void test_logic_bit_compare_inc_dec() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA9, 0xF0,       // LDA #$F0
        0x29, 0x0F,       // AND #$0F -> 0, Z
        0x09, 0x80,       // ORA #$80 -> N
        0x49, 0xFF,       // EOR #$FF -> $7F
        0x24, 0x10,       // BIT $10
        0xC9, 0x7F,       // CMP #$7F
        0xC9, 0x80,       // CMP #$80
        0xE0, 0x01,       // CPX #$01
        0xC0, 0x02,       // CPY #$02
        0xE6, 0x11,       // INC $11
        0xC6, 0x12,       // DEC $12
        0xE8,             // INX
        0xC8,             // INY
        0xCA,             // DEX
        0x88,             // DEY
    };
    bus.load(0x0200, program, sizeof(program));
    bus.memory[0x0010] = 0xC0;
    bus.memory[0x0011] = 0x7F;
    bus.memory[0x0012] = 0x01;
    Cpu6510 cpu(bus);
    cpu.reset();
    cpu.state().x = 0x01;
    cpu.state().y = 0x02;

    require(cpu.step() == StepResult::Ok, "logic setup LDA executes");
    require(cpu.step() == StepResult::Ok, "AND executes");
    require(cpu.state().a == 0x00, "AND computes result");
    require((cpu.state().p & FLAG_Z) != 0, "AND sets Z");
    require(cpu.step() == StepResult::Ok, "ORA executes");
    require(cpu.state().a == 0x80, "ORA computes result");
    require((cpu.state().p & FLAG_N) != 0, "ORA sets N");
    require(cpu.step() == StepResult::Ok, "EOR executes");
    require(cpu.state().a == 0x7F, "EOR computes result");
    require(cpu.step() == StepResult::Ok, "BIT executes");
    require((cpu.state().p & FLAG_N) != 0, "BIT copies bit 7 to N");
    require((cpu.state().p & FLAG_V) != 0, "BIT copies bit 6 to V");
    require((cpu.state().p & FLAG_Z) == 0, "BIT clears Z when A & value is non-zero");
    require(cpu.step() == StepResult::Ok, "CMP equal executes");
    require((cpu.state().p & FLAG_C) != 0, "CMP equal sets C");
    require((cpu.state().p & FLAG_Z) != 0, "CMP equal sets Z");
    require(cpu.step() == StepResult::Ok, "CMP less executes");
    require((cpu.state().p & FLAG_C) == 0, "CMP less clears C");
    require((cpu.state().p & FLAG_N) != 0, "CMP less sets N from subtraction result");
    require(cpu.step() == StepResult::Ok, "CPX executes");
    require((cpu.state().p & FLAG_Z) != 0, "CPX equal sets Z");
    require(cpu.step() == StepResult::Ok, "CPY executes");
    require((cpu.state().p & FLAG_Z) != 0, "CPY equal sets Z");
    require(cpu.step() == StepResult::Ok, "INC executes");
    require(bus.memory[0x0011] == 0x80, "INC increments memory");
    require((cpu.state().p & FLAG_N) != 0, "INC sets N");
    require(cpu.step() == StepResult::Ok, "DEC executes");
    require(bus.memory[0x0012] == 0x00, "DEC decrements memory");
    require((cpu.state().p & FLAG_Z) != 0, "DEC sets Z");
    require(cpu.step() == StepResult::Ok, "INX executes in group test");
    require(cpu.state().x == 0x02, "INX increments X");
    require(cpu.step() == StepResult::Ok, "INY executes");
    require(cpu.state().y == 0x03, "INY increments Y");
    require(cpu.step() == StepResult::Ok, "DEX executes");
    require(cpu.state().x == 0x01, "DEX decrements X");
    require(cpu.step() == StepResult::Ok, "DEY executes");
    require(cpu.state().y == 0x02, "DEY decrements Y");
}

void test_shift_and_rotate_accumulator_and_memory() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0xA9, 0x81, // LDA #$81
        0x0A,       // ASL A -> $02, C
        0x4A,       // LSR A -> $01
        0x38,       // SEC
        0x2A,       // ROL A -> $03
        0x6A,       // ROR A -> $01, C
        0x06, 0x10, // ASL $10
        0x46, 0x11, // LSR $11
        0x26, 0x12, // ROL $12
        0x66, 0x13, // ROR $13
    };
    bus.load(0x0200, program, sizeof(program));
    bus.memory[0x0010] = 0x80;
    bus.memory[0x0011] = 0x01;
    bus.memory[0x0012] = 0x7F;
    bus.memory[0x0013] = 0x02;
    Cpu6510 cpu(bus);
    cpu.reset();

    require(cpu.step() == StepResult::Ok, "shift setup LDA executes");
    require(cpu.step() == StepResult::Ok, "ASL accumulator executes");
    require(cpu.state().a == 0x02, "ASL accumulator shifts left");
    require((cpu.state().p & FLAG_C) != 0, "ASL accumulator sets C from bit 7");
    require(cpu.step() == StepResult::Ok, "LSR accumulator executes");
    require(cpu.state().a == 0x01, "LSR accumulator shifts right");
    require((cpu.state().p & FLAG_C) == 0, "LSR accumulator clears C when bit 0 is clear");
    require(cpu.step() == StepResult::Ok, "SEC before ROL executes");
    require(cpu.step() == StepResult::Ok, "ROL accumulator executes");
    require(cpu.state().a == 0x03, "ROL accumulator rotates carry in");
    require(cpu.step() == StepResult::Ok, "ROR accumulator executes");
    require(cpu.state().a == 0x01, "ROR accumulator rotates right");
    require((cpu.state().p & FLAG_C) != 0, "ROR accumulator sets C from bit 0");
    require(cpu.step() == StepResult::Ok, "ASL memory executes");
    require(bus.memory[0x0010] == 0x00, "ASL memory writes shifted value");
    require((cpu.state().p & FLAG_C) != 0, "ASL memory sets C");
    require(cpu.step() == StepResult::Ok, "LSR memory executes");
    require(bus.memory[0x0011] == 0x00, "LSR memory writes shifted value");
    require((cpu.state().p & FLAG_C) != 0, "LSR memory sets C");
    require(cpu.step() == StepResult::Ok, "ROL memory executes");
    require(bus.memory[0x0012] == 0xFF, "ROL memory rotates carry in");
    require(cpu.step() == StepResult::Ok, "ROR memory executes");
    require(bus.memory[0x0013] == 0x01, "ROR memory rotates carry in as clear");
}

void test_adc_sbc_binary_and_decimal_smoke() {
    RamBus bus;
    bus.set_reset_vector(0x0200);
    const uint8_t program[] = {
        0x18,       // CLC
        0xA9, 0x50, // LDA #$50
        0x69, 0x50, // ADC #$50 -> $A0, V, no C
        0x38,       // SEC
        0xE9, 0x10, // SBC #$10 -> $90, C
        0xF8,       // SED
        0x18,       // CLC
        0xA9, 0x45, // LDA #$45
        0x69, 0x55, // ADC #$55 -> $00, C in BCD
        0x38,       // SEC
        0xA9, 0x50, // LDA #$50
        0xE9, 0x01, // SBC #$01 -> $49 in BCD
        0xD8,       // CLD
    };
    bus.load(0x0200, program, sizeof(program));
    Cpu6510 cpu(bus);
    cpu.reset();

    require(cpu.step() == StepResult::Ok, "CLC before ADC executes");
    require(cpu.step() == StepResult::Ok, "ADC setup LDA executes");
    require(cpu.step() == StepResult::Ok, "ADC binary executes");
    require(cpu.state().a == 0xA0, "ADC binary stores binary result");
    require((cpu.state().p & FLAG_V) != 0, "ADC binary sets V on signed overflow");
    require((cpu.state().p & FLAG_C) == 0, "ADC binary leaves C clear without unsigned carry");
    require(cpu.step() == StepResult::Ok, "SEC before SBC executes");
    require(cpu.step() == StepResult::Ok, "SBC binary executes");
    require(cpu.state().a == 0x90, "SBC binary stores binary result");
    require((cpu.state().p & FLAG_C) != 0, "SBC binary keeps C when no borrow");
    require(cpu.step() == StepResult::Ok, "SED executes in decimal smoke");
    require(cpu.step() == StepResult::Ok, "CLC before BCD ADC executes");
    require(cpu.step() == StepResult::Ok, "BCD ADC setup LDA executes");
    require(cpu.step() == StepResult::Ok, "BCD ADC executes");
    require(cpu.state().a == 0x00, "BCD ADC stores decimal-adjusted result");
    require((cpu.state().p & FLAG_C) != 0, "BCD ADC sets C for 100 decimal");
    require(cpu.step() == StepResult::Ok, "SEC before BCD SBC executes");
    require(cpu.step() == StepResult::Ok, "BCD SBC setup LDA executes");
    require(cpu.step() == StepResult::Ok, "BCD SBC executes");
    require(cpu.state().a == 0x49, "BCD SBC stores decimal-adjusted result");
    require((cpu.state().p & FLAG_C) != 0, "BCD SBC keeps C when no decimal borrow");
    require(cpu.step() == StepResult::Ok, "CLD executes after decimal smoke");
    require((cpu.state().p & FLAG_D) == 0, "CLD clears decimal mode");
}

void test_e2e_program_image_with_vectors_subroutine_and_brk_rti() {
    RamBus bus;
    bus.set_reset_vector(0x0800);
    bus.set_irq_brk_vector(0x0900);

    const uint8_t main_program[] = {
        0xA9, 0x2A,       // $0800 LDA #$2A
        0x8D, 0x00, 0x40, // $0802 STA $4000
        0x20, 0x20, 0x08, // $0805 JSR $0820
        0xAD, 0x01, 0x40, // $0808 LDA $4001
        0x8D, 0x02, 0x40, // $080B STA $4002
        0x00,             // $080E BRK
        0xEA,             // $080F BRK padding byte
        0xA9, 0x55,       // $0810 LDA #$55
        0x8D, 0x03, 0x40, // $0812 STA $4003
        0x4C, 0x40, 0x08, // $0815 JMP $0840
    };
    const uint8_t subroutine[] = {
        0xA9, 0x7E,       // $0820 LDA #$7E
        0x8D, 0x01, 0x40, // $0822 STA $4001
        0x60,             // $0825 RTS
    };
    const uint8_t irq_brk_handler[] = {
        0xA9, 0xEE,       // $0900 LDA #$EE
        0x8D, 0x04, 0x40, // $0902 STA $4004
        0x40,             // $0905 RTI
    };
    bus.load(0x0800, main_program, sizeof(main_program));
    bus.load(0x0820, subroutine, sizeof(subroutine));
    bus.load(0x0900, irq_brk_handler, sizeof(irq_brk_handler));

    Cpu6510 cpu(bus);
    cpu.reset();

    run_steps(cpu, 15, "E2E RAM program");

    require(cpu.state().pc == 0x0840, "E2E program reaches final JMP target");
    require(cpu.state().sp == 0xFD, "E2E program balances JSR/RTS and BRK/RTI stack usage");
    require(bus.memory[0x4000] == 0x2A, "E2E program writes initial value to RAM");
    require(bus.memory[0x4001] == 0x7E, "E2E subroutine writes RAM value");
    require(bus.memory[0x4002] == 0x7E, "E2E main copies subroutine value through RAM");
    require(bus.memory[0x4003] == 0x55, "E2E main resumes after BRK/RTI");
    require(bus.memory[0x4004] == 0xEE, "E2E BRK handler writes RAM value");
}

void test_e2e_memory_driven_branch_program() {
    RamBus bus;
    bus.set_reset_vector(0x0A00);

    const uint8_t program[] = {
        0xAD, 0x00, 0x41, // $0A00 LDA $4100
        0xF0, 0x08,       // $0A03 BEQ $0A0D
        0xA9, 0x01,       // $0A05 LDA #$01
        0x8D, 0x01, 0x41, // $0A07 STA $4101
        0x4C, 0x12, 0x0A, // $0A0A JMP $0A12
        0xA9, 0x02,       // $0A0D LDA #$02
        0x8D, 0x01, 0x41, // $0A0F STA $4101
        0x02,             // $0A12 illegal sentinel
    };
    bus.load(0x0A00, program, sizeof(program));
    bus.memory[0x4100] = 0x00;

    Cpu6510 cpu(bus);
    cpu.reset();
    run_steps(cpu, 4, "E2E memory branch taken");

    require(bus.memory[0x4101] == 0x02, "E2E branch program chooses zero-memory path");
    require(cpu.step() == StepResult::IllegalOpcode, "E2E branch program reaches illegal sentinel");
    require(cpu.state().pc == 0x0A12, "illegal sentinel leaves PC at sentinel address");

    bus.memory.fill(0);
    bus.set_reset_vector(0x0A00);
    bus.load(0x0A00, program, sizeof(program));
    bus.memory[0x4100] = 0x80;
    cpu.reset();
    run_steps(cpu, 5, "E2E memory branch not taken");

    require(bus.memory[0x4101] == 0x01, "E2E branch program chooses non-zero-memory path");
    require(cpu.step() == StepResult::IllegalOpcode, "E2E not-taken path reaches illegal sentinel");
    require(cpu.state().pc == 0x0A12, "not-taken sentinel leaves PC at sentinel address");
}

void test_run_matches_step_for_e2e_program() {
    RamBus step_bus;
    RamBus run_bus;
    step_bus.set_reset_vector(0x0C00);
    run_bus.set_reset_vector(0x0C00);

    const uint8_t program[] = {
        0xA9, 0x10,       // LDA #$10
        0xAA,             // TAX
        0xE8,             // INX
        0x8E, 0x00, 0x43, // STX $4300
        0xA0, 0x02,       // LDY #$02
        0x88,             // DEY
        0xD0, 0xFD,       // BNE $0C09
        0xAD, 0x00, 0x43, // LDA $4300
        0x48,             // PHA
        0xA9, 0x00,       // LDA #$00
        0x68,             // PLA
        0x8D, 0x01, 0x43, // STA $4301
        0x02,             // illegal sentinel
    };
    step_bus.load(0x0C00, program, sizeof(program));
    run_bus.load(0x0C00, program, sizeof(program));

    Cpu6510 step_cpu(step_bus);
    Cpu6510 run_cpu(run_bus);
    step_cpu.reset();
    run_cpu.reset();

    for (int i = 0; i < 14; ++i) {
        require(step_cpu.step() == StepResult::Ok, "reference step executes before sentinel");
    }
    const RunResult run_result = run_cpu.run(14);

    require(run_result.result == StepResult::Ok, "run returns Ok before sentinel");
    require(run_result.instructions_executed == 14, "run reports executed instruction budget");
    require_same_state(step_cpu.state(), run_cpu.state(), "run vs step");
    require(step_bus.memory == run_bus.memory, "run and step produce identical memory");

    require(step_cpu.step() == StepResult::IllegalOpcode, "reference step reaches sentinel");
    const RunResult illegal_result = run_cpu.run(1);
    require(illegal_result.result == StepResult::IllegalOpcode, "run reports illegal opcode");
    require(illegal_result.instructions_executed == 1, "run counts failing instruction");
    require(illegal_result.stop_pc == run_cpu.state().pc, "run stop_pc matches CPU PC");
    require_same_state(step_cpu.state(), run_cpu.state(), "run vs step after sentinel");
}

void test_run_block_stops_on_budget_and_control_flow() {
    RamBus bus;
    bus.set_reset_vector(0x0D00);
    const uint8_t program[] = {
        0xA9, 0x11,       // LDA #$11
        0xAA,             // TAX
        0xE8,             // INX
        0xD0, 0x02,       // BNE $0D08
        0xA9, 0x00,       // skipped
        0x4C, 0x20, 0x0D, // JMP $0D20
    };
    bus.load(0x0D00, program, sizeof(program));
    Cpu6510 cpu(bus);
    cpu.reset();

    BlockRunResult budget = cpu.run_block(2);
    require(budget.result == StepResult::Ok, "run_block budget result is ok");
    require(budget.stop_reason == RunStopReason::BudgetExhausted, "run_block stops on budget");
    require(budget.instructions_executed == 2, "run_block budget counts instructions");
    require(cpu.state().pc == 0x0D03, "run_block budget leaves PC after executed instructions");

    BlockRunResult branch = cpu.run_block(10);
    require(branch.result == StepResult::Ok, "run_block branch result is ok");
    require(branch.stop_reason == RunStopReason::ControlFlow, "run_block stops on branch terminator");
    require(branch.instructions_executed == 2, "run_block branch executes through branch");
    require(cpu.state().pc == 0x0D08, "run_block branch leaves PC at branch target");

    BlockRunResult jump = cpu.run_block(10);
    require(jump.result == StepResult::Ok, "run_block jump result is ok");
    require(jump.stop_reason == RunStopReason::ControlFlow, "run_block stops on JMP terminator");
    require(jump.instructions_executed == 1, "run_block executes one JMP");
    require(cpu.state().pc == 0x0D20, "run_block jump leaves PC at target");
}

void test_run_block_stops_on_illegal_and_interrupt_pending() {
    RamBus bus;
    bus.set_reset_vector(0x0E00);
    bus.set_irq_brk_vector(0x0F00);
    bus.memory[0x0E00] = 0x02;
    Cpu6510 cpu(bus);
    cpu.reset();

    BlockRunResult illegal = cpu.run_block(4);
    require(illegal.result == StepResult::IllegalOpcode, "run_block reports illegal opcode");
    require(illegal.stop_reason == RunStopReason::IllegalOpcode, "run_block stop reason is illegal");
    require(illegal.instructions_executed == 1, "run_block counts illegal instruction attempt");
    require(illegal.stop_pc == 0x0E00, "run_block illegal stop PC is offending opcode");

    bus.memory[0x0E00] = 0xEA;
    cpu.reset();
    cpu.state().p = FLAG_U;
    cpu.set_irq_level(true);
    BlockRunResult irq = cpu.run_block(4);
    require(irq.result == StepResult::Ok, "run_block pending IRQ result is ok");
    require(irq.stop_reason == RunStopReason::InterruptPending, "run_block stops before pending interrupt work");
    require(irq.instructions_executed == 0, "run_block pending IRQ executes nothing");
    require(cpu.state().pc == 0x0E00, "run_block pending IRQ preserves PC");
}

void test_run_cached_matches_step_and_tracks_cache_stats() {
    RamBus step_bus;
    RamBus cached_bus;
    step_bus.set_reset_vector(0x1000);
    cached_bus.set_reset_vector(0x1000);
    const uint8_t program[] = {
        0xA9, 0x01,       // LDA #$01
        0xAA,             // TAX
        0xE8,             // INX
        0xD0, 0x02,       // BNE $1008
        0xA9, 0x00,       // skipped
        0xEA,             // NOP
        0x02,             // illegal sentinel
    };
    step_bus.load(0x1000, program, sizeof(program));
    cached_bus.load(0x1000, program, sizeof(program));
    Cpu6510 step_cpu(step_bus);
    Cpu6510 cached_cpu(cached_bus);
    step_cpu.reset();
    cached_cpu.reset();

    for (int i = 0; i < 5; ++i) {
        require(step_cpu.step() == StepResult::Ok, "reference step for cached test executes");
    }
    const RunResult cached = cached_cpu.run_cached(5);

    require(cached.result == StepResult::Ok, "run_cached returns Ok before sentinel");
    require(cached.instructions_executed == 5, "run_cached reports instruction budget");
    require_same_state(step_cpu.state(), cached_cpu.state(), "run_cached vs step");
#if J6510_ENABLE_CACHE_STATS
    require(cached_cpu.block_cache_stats().misses > 0, "run_cached records cache miss");
#endif

    const RunResult illegal = cached_cpu.run_cached(1);
    require(illegal.result == StepResult::IllegalOpcode, "run_cached reports illegal sentinel");
    require(illegal.instructions_executed == 1, "run_cached counts illegal instruction");
}

void test_run_cached_hits_and_invalidates_after_write() {
    RamBus bus;
    bus.set_reset_vector(0x1100);
    const uint8_t program[] = {
        0xEA,             // NOP
        0xEA,             // NOP
        0x4C, 0x00, 0x11, // JMP $1100
    };
    bus.load(0x1100, program, sizeof(program));
    Cpu6510 cpu(bus);
    cpu.reset();

    RunResult first = cpu.run_cached(3);
    require(first.result == StepResult::Ok, "first run_cached loop executes");
#if J6510_ENABLE_CACHE_STATS
    require(cpu.block_cache_stats().misses >= 1, "first run_cached records miss");
#endif

    RunResult second = cpu.run_cached(3);
    require(second.result == StepResult::Ok, "second run_cached loop executes");
#if J6510_ENABLE_CACHE_STATS
    require(cpu.block_cache_stats().hits >= 1, "second run_cached records hit");

    const uint64_t invalidations_before = cpu.block_cache_stats().invalidations;
#endif
    const uint8_t writer[] = {
        0xA9, 0x42,       // LDA #$42
        0x8D, 0x01, 0x11, // STA $1101
    };
    bus.set_reset_vector(0x1210);
    bus.load(0x1210, writer, sizeof(writer));
    cpu.reset();
    require(cpu.run_cached(2).result == StepResult::Ok, "run_cached writer executes");
    require(bus.memory[0x1101] == 0x42, "writer updates cached code page");
#if J6510_ENABLE_CACHE_STATS
    require(cpu.block_cache_stats().invalidations > invalidations_before, "write to cached code page invalidates cache");
#endif
}

void test_run_cached_direct_path_with_6510_port_matches_step() {
    RamBus step_bus;
    RamBus cached_bus;
    step_bus.set_reset_vector(0x1B00);
    cached_bus.set_reset_vector(0x1B00);
    const uint8_t program[] = {
        0xA2, 0x01,       // LDX #$01
        0xB5, 0xFF,       // LDA $FF,X -> 6510 DDR at $0000
        0x85, 0x10,       // STA $10
        0xB5, 0x00,       // LDA $00,X -> 6510 data port at $0001
        0x85, 0x11,       // STA $11
        0xA9, 0x00,       // LDA #$00
        0x95, 0xFF,       // STA $FF,X -> 6510 DDR at $0000
        0xA9, 0x55,       // LDA #$55
        0x95, 0x00,       // STA $00,X -> 6510 data port at $0001
        0xB5, 0x00,       // LDA $00,X -> data port with DDR cleared
        0x85, 0x12,       // STA $12
        0x4C, 0x00, 0x1B, // JMP $1B00
    };
    step_bus.load(0x1B00, program, sizeof(program));
    cached_bus.load(0x1B00, program, sizeof(program));

    Cpu6510 step_cpu(step_bus);
    Cpu6510 cached_cpu(cached_bus);
    step_cpu.reset();
    cached_cpu.reset();

    for (int i = 0; i < 12; ++i) {
        require(step_cpu.step() == StepResult::Ok, "reference step for cached direct 6510 port executes");
    }
    const RunResult cached = cached_cpu.run_cached(12);

    require(cached.result == StepResult::Ok, "run_cached direct 6510 port returns Ok");
    require(cached.instructions_executed == 12, "run_cached direct 6510 port reports instruction budget");
    require_same_state(step_cpu.state(), cached_cpu.state(), "run_cached direct 6510 port state");
    require(step_bus.memory == cached_bus.memory, "run_cached direct 6510 port memory");
    require(step_cpu.port().ddr == cached_cpu.port().ddr, "run_cached direct 6510 port DDR");
    require(step_cpu.port().data == cached_cpu.port().data, "run_cached direct 6510 port data");
    require(cached_bus.memory[0x0010] == 0x2F, "run_cached direct 6510 port stores DDR read");
    require(cached_bus.memory[0x0011] == 0xF7, "run_cached direct 6510 port stores data-port read");
    require(cached_bus.memory[0x0012] == 0xFF, "run_cached direct 6510 port stores data-port read after DDR clear");
#if J6510_ENABLE_CACHE_STATS
    require(cached_cpu.block_cache_stats().fallback_instructions == 0, "run_cached direct 6510 port stays in IR");
#endif
}

void test_run_cached_ir_mixed_loop_matches_step() {
    RamBus step_bus;
    RamBus cached_bus;
    step_bus.set_reset_vector(0x1300);
    cached_bus.set_reset_vector(0x1300);
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
        0xD0, 0xF6,       // BNE $130C
        0xCA,             // DEX
        0x8A,             // TXA
        0xAA,             // TAX
        0x4C, 0x00, 0x13, // JMP $1300
    };
    step_bus.load(0x1300, program, sizeof(program));
    cached_bus.load(0x1300, program, sizeof(program));
    for (uint16_t i = 0; i < 0x40; ++i) {
        step_bus.memory[static_cast<uint16_t>(0x3000 + i)] = static_cast<uint8_t>(i + 1);
        cached_bus.memory[static_cast<uint16_t>(0x3000 + i)] = static_cast<uint8_t>(i + 1);
    }

    Cpu6510 step_cpu(step_bus, Cpu6510Config{false});
    Cpu6510 cached_cpu(cached_bus, Cpu6510Config{false});
    step_cpu.reset();
    cached_cpu.reset();

    for (int i = 0; i < 90; ++i) {
        require(step_cpu.step() == StepResult::Ok, "reference step for cached IR mixed loop executes");
    }
    const RunResult cached = cached_cpu.run_cached(90);

    require(cached.result == StepResult::Ok, "run_cached IR mixed loop returns Ok");
    require(cached.instructions_executed == 90, "run_cached IR mixed loop reports instruction budget");
    require_same_state(step_cpu.state(), cached_cpu.state(), "run_cached IR mixed loop state");
    require(step_bus.memory == cached_bus.memory, "run_cached IR mixed loop memory");
#if J6510_ENABLE_CACHE_STATS
    require(cached_cpu.block_cache_stats().hits > 0, "run_cached IR mixed loop records hits");
#endif
}

void test_run_cached_reports_ir_and_fallback_coverage() {
#if J6510_ENABLE_CACHE_STATS
    RamBus ir_bus;
    ir_bus.set_reset_vector(0x1400);
    const uint8_t ir_program[] = {
        0xA9, 0x7F,       // LDA #$7F
        0xAA,             // TAX
        0xE8,             // INX
        0x4C, 0x00, 0x14, // JMP $1400
    };
    ir_bus.load(0x1400, ir_program, sizeof(ir_program));
    Cpu6510 ir_cpu(ir_bus, Cpu6510Config{false});
    ir_cpu.reset();
    require(ir_cpu.run_cached(4).result == StepResult::Ok, "run_cached IR coverage program executes");
    const BlockCacheStats& ir_stats = ir_cpu.block_cache_stats();
    require(ir_stats.ir_blocks > 0, "run_cached records IR block");
    require(ir_stats.ir_instructions == 4, "run_cached records IR instructions");
    require(ir_stats.fallback_blocks == 0, "run_cached IR program avoids fallback blocks");
    require(ir_stats.fallback_instructions == 0, "run_cached IR program avoids fallback instructions");

    RamBus fallback_bus;
    fallback_bus.set_reset_vector(0x1500);
    fallback_bus.set_irq_brk_vector(0x1500);
    const uint8_t fallback_program[] = {
        0xA9, 0x01,       // LDA #$01
        0x00,             // BRK, intentionally not in cached IR yet
        0x4C, 0x00, 0x15, // JMP $1500
    };
    fallback_bus.load(0x1500, fallback_program, sizeof(fallback_program));
    Cpu6510 fallback_cpu(fallback_bus, Cpu6510Config{false});
    fallback_cpu.reset();
    require(fallback_cpu.run_cached(3).result == StepResult::Ok, "run_cached fallback coverage program executes");
    const BlockCacheStats& fallback_stats = fallback_cpu.block_cache_stats();
    require(fallback_stats.ir_blocks == 0, "run_cached fallback program avoids IR blocks");
    require(fallback_stats.ir_instructions == 0, "run_cached fallback program avoids IR instructions");
    require(fallback_stats.fallback_blocks > 0, "run_cached records fallback block");
    require(fallback_stats.fallback_instructions == 3, "run_cached records fallback instructions");
    require(fallback_stats.fallback_opcodes[0x00] == 1, "run_cached records fallback opcode histogram");
    require(fallback_stats.unsupported_fallback_opcodes[0x00] == 1, "run_cached records unsupported fallback opcode histogram");
#endif
}

void test_run_cached_ir_flags_and_branches_match_step() {
    RamBus step_bus;
    RamBus cached_bus;
    step_bus.set_reset_vector(0x1600);
    cached_bus.set_reset_vector(0x1600);
    const uint8_t program[] = {
        0x38,             // SEC
        0xB0, 0x02,       // BCS $1605
        0xA9, 0x00,       // skipped
        0x18,             // CLC
        0x90, 0x02,       // BCC $160A
        0xA9, 0x01,       // skipped
        0x78,             // SEI
        0x58,             // CLI
        0xF8,             // SED
        0xD8,             // CLD
        0xA9, 0x00,       // LDA #$00, sets Z
        0xF0, 0x02,       // BEQ $1614
        0xA9, 0x02,       // skipped
        0xA9, 0x80,       // LDA #$80, sets N
        0x30, 0x02,       // BMI $161A
        0xA9, 0x03,       // skipped
        0xB8,             // CLV
        0x50, 0x02,       // BVC $161F
        0xA9, 0x04,       // skipped
        0x4C, 0x00, 0x16, // JMP $1600
    };
    step_bus.load(0x1600, program, sizeof(program));
    cached_bus.load(0x1600, program, sizeof(program));
    Cpu6510 step_cpu(step_bus, Cpu6510Config{false});
    Cpu6510 cached_cpu(cached_bus, Cpu6510Config{false});
    step_cpu.reset();
    cached_cpu.reset();

    for (int i = 0; i < 32; ++i) {
        require(step_cpu.step() == StepResult::Ok, "reference step for cached IR flags and branches executes");
    }
    const RunResult cached = cached_cpu.run_cached(32);

    require(cached.result == StepResult::Ok, "run_cached IR flags and branches returns Ok");
    require(cached.instructions_executed == 32, "run_cached IR flags and branches reports instruction budget");
    require_same_state(step_cpu.state(), cached_cpu.state(), "run_cached IR flags and branches state");
#if J6510_ENABLE_CACHE_STATS
    require(cached_cpu.block_cache_stats().fallback_instructions == 0, "run_cached flags and branches stay in IR");
#endif
}

void test_run_cached_ir_load_store_transfer_matches_step() {
    RamBus step_bus;
    RamBus cached_bus;
    step_bus.set_reset_vector(0x1700);
    cached_bus.set_reset_vector(0x1700);
    const uint8_t program[] = {
        0xA9, 0x12,       // LDA #$12
        0xA8,             // TAY
        0xC8,             // INY
        0x84, 0x40,       // STY $40
        0xA4, 0x40,       // LDY $40
        0x8C, 0x00, 0x31, // STY $3100
        0xAC, 0x00, 0x31, // LDY $3100
        0x98,             // TYA
        0xAA,             // TAX
        0x9A,             // TXS
        0xBA,             // TSX
        0x86, 0x41,       // STX $41
        0xA6, 0x41,       // LDX $41
        0x9D, 0x00, 0x31, // STA $3100,X
        0xA5, 0x40,       // LDA $40
        0x4C, 0x00, 0x17, // JMP $1700
    };
    step_bus.load(0x1700, program, sizeof(program));
    cached_bus.load(0x1700, program, sizeof(program));
    Cpu6510 step_cpu(step_bus, Cpu6510Config{false});
    Cpu6510 cached_cpu(cached_bus, Cpu6510Config{false});
    step_cpu.reset();
    cached_cpu.reset();

    for (int i = 0; i < 16; ++i) {
        require(step_cpu.step() == StepResult::Ok, "reference step for cached IR load/store/transfer executes");
    }
    const RunResult cached = cached_cpu.run_cached(16);

    require(cached.result == StepResult::Ok, "run_cached IR load/store/transfer returns Ok");
    require(cached.instructions_executed == 16, "run_cached IR load/store/transfer reports instruction budget");
    require_same_state(step_cpu.state(), cached_cpu.state(), "run_cached IR load/store/transfer state");
    require(step_bus.memory == cached_bus.memory, "run_cached IR load/store/transfer memory");
#if J6510_ENABLE_CACHE_STATS
    require(cached_cpu.block_cache_stats().fallback_instructions == 0, "run_cached load/store/transfer stays in IR");
#endif
}

void test_run_cached_ir_indexed_loads_match_step() {
    RamBus step_bus;
    RamBus cached_bus;
    step_bus.set_reset_vector(0x1800);
    cached_bus.set_reset_vector(0x1800);
    const uint8_t program[] = {
        0xA2, 0x03,       // LDX #$03
        0xA0, 0x05,       // LDY #$05
        0xB5, 0x20,       // LDA $20,X
        0xB6, 0x30,       // LDX $30,Y
        0xB4, 0x40,       // LDY $40,X
        0xBD, 0x00, 0x32, // LDA $3200,X
        0xBE, 0x10, 0x32, // LDX $3210,Y
        0xBC, 0x20, 0x32, // LDY $3220,X
        0x4C, 0x00, 0x18, // JMP $1800
    };
    step_bus.load(0x1800, program, sizeof(program));
    cached_bus.load(0x1800, program, sizeof(program));
    step_bus.memory[0x0023] = cached_bus.memory[0x0023] = 0x07;
    step_bus.memory[0x0035] = cached_bus.memory[0x0035] = 0x04;
    step_bus.memory[0x0044] = cached_bus.memory[0x0044] = 0x06;
    step_bus.memory[0x3204] = cached_bus.memory[0x3204] = 0x22;
    step_bus.memory[0x3216] = cached_bus.memory[0x3216] = 0x08;
    step_bus.memory[0x3228] = cached_bus.memory[0x3228] = 0x33;

    Cpu6510 step_cpu(step_bus, Cpu6510Config{false});
    Cpu6510 cached_cpu(cached_bus, Cpu6510Config{false});
    step_cpu.reset();
    cached_cpu.reset();

    for (int i = 0; i < 9; ++i) {
        require(step_cpu.step() == StepResult::Ok, "reference step for cached IR indexed loads executes");
    }
    const RunResult cached = cached_cpu.run_cached(9);

    require(cached.result == StepResult::Ok, "run_cached IR indexed loads returns Ok");
    require(cached.instructions_executed == 9, "run_cached IR indexed loads reports instruction budget");
    require_same_state(step_cpu.state(), cached_cpu.state(), "run_cached IR indexed loads state");
#if J6510_ENABLE_CACHE_STATS
    require(cached_cpu.block_cache_stats().fallback_instructions == 0, "run_cached indexed loads stay in IR");
    require(cached_cpu.block_cache_stats().ir_instructions == 9, "run_cached indexed loads count as IR");
#endif
}

void test_run_cached_ir_adc_cmp_inc_match_step() {
    RamBus step_bus;
    RamBus cached_bus;
    RamBus generic_bus;
    step_bus.set_reset_vector(0x1900);
    cached_bus.set_reset_vector(0x1900);
    generic_bus.set_reset_vector(0x1900);
    const uint8_t program[] = {
        0xF8,             // SED
        0xA9, 0x45,       // LDA #$45
        0x69, 0x55,       // ADC #$55, decimal result $00 with carry
        0x85, 0x20,       // STA $20
        0xD8,             // CLD
        0x18,             // CLC
        0xA9, 0x7F,       // LDA #$7F
        0x69, 0x01,       // ADC #$01, binary overflow to $80
        0xC9, 0x80,       // CMP #$80
        0xE6, 0x20,       // INC $20
        0x90, 0x02,       // BCC, not taken after CMP equality
        0xA9, 0xFE,       // LDA #$FE
        0x4C, 0x00, 0x19, // JMP $1900
    };
    step_bus.load(0x1900, program, sizeof(program));
    cached_bus.load(0x1900, program, sizeof(program));
    generic_bus.load(0x1900, program, sizeof(program));
    Cpu6510 step_cpu(step_bus, Cpu6510Config{false});
    Cpu6510 cached_cpu(cached_bus, Cpu6510Config{false});
    Cpu6510 generic_cpu(generic_bus);
    step_cpu.reset();
    cached_cpu.reset();
    generic_cpu.reset();

    for (int i = 0; i < 13; ++i) {
        require(step_cpu.step() == StepResult::Ok, "reference step for cached IR ADC/CMP/INC executes");
    }
    const RunResult cached = cached_cpu.run_cached(13);
    const RunResult generic = generic_cpu.run_cached(13);

    require(cached.result == StepResult::Ok, "run_cached IR ADC/CMP/INC returns Ok");
    require(cached.instructions_executed == 13, "run_cached IR ADC/CMP/INC reports instruction budget");
    require_same_state(step_cpu.state(), cached_cpu.state(), "run_cached IR ADC/CMP/INC state");
    require(step_bus.memory == cached_bus.memory, "run_cached IR ADC/CMP/INC memory");
#if J6510_ENABLE_CACHE_STATS
    require(cached_cpu.block_cache_stats().fallback_instructions == 0, "run_cached ADC/CMP/INC stays in IR");
#endif
    require(generic.result == StepResult::Ok, "generic cached IR ADC/CMP/INC returns Ok");
    require(generic.instructions_executed == 13, "generic cached IR ADC/CMP/INC reports instruction budget");
    require_same_state(step_cpu.state(), generic_cpu.state(), "generic cached IR ADC/CMP/INC state");
    require(step_bus.memory == generic_bus.memory, "generic cached IR ADC/CMP/INC memory");
#if J6510_ENABLE_CACHE_STATS
    require(generic_cpu.block_cache_stats().fallback_instructions == 0, "generic cached ADC/CMP/INC stays in IR");
#endif
}

void test_run_cached_fused_pairs_respect_budget_cuts() {
    const auto require_cached_matches_step = [](const uint8_t* program, uint16_t size, int first_budget,
                                                int second_budget, const std::string& context) {
        RamBus step_bus;
        RamBus cached_bus;
        step_bus.set_reset_vector(0x1C00);
        cached_bus.set_reset_vector(0x1C00);
        step_bus.load(0x1C00, program, size);
        cached_bus.load(0x1C00, program, size);
        Cpu6510 step_cpu(step_bus, Cpu6510Config{false});
        Cpu6510 cached_cpu(cached_bus, Cpu6510Config{false});
        step_cpu.reset();
        cached_cpu.reset();

        run_steps(step_cpu, first_budget, context + " first reference");
        const RunResult first = cached_cpu.run_cached(static_cast<uint32_t>(first_budget));
        require(first.result == StepResult::Ok, context + " first run_cached returns Ok");
        require(first.instructions_executed == static_cast<uint32_t>(first_budget),
                context + " first run_cached reports budget");
        require_same_state(step_cpu.state(), cached_cpu.state(), context + " first budget cut");

        run_steps(step_cpu, second_budget, context + " second reference");
        const RunResult second = cached_cpu.run_cached(static_cast<uint32_t>(second_budget));
        require(second.result == StepResult::Ok, context + " second run_cached returns Ok");
        require(second.instructions_executed == static_cast<uint32_t>(second_budget),
                context + " second run_cached reports budget");
        require_same_state(step_cpu.state(), cached_cpu.state(), context + " resumed branch");
        require(step_bus.memory == cached_bus.memory, context + " memory");
    };

    const uint8_t dex_bpl_program[] = {
        0xA2, 0x01,       // LDX #$01
        0xCA,             // DEX
        0x10, 0x02,       // BPL $1C07
        0xA9, 0xFF,       // skipped after resumed branch
        0xA9, 0x42,       // branch target
        0x4C, 0x00, 0x1C, // JMP $1C00
    };
    require_cached_matches_step(dex_bpl_program, sizeof(dex_bpl_program), 2, 1, "fused DEX/BPL");

    const uint8_t dey_bne_program[] = {
        0xA0, 0x02,       // LDY #$02
        0x88,             // DEY
        0xD0, 0x02,       // BNE $1C07
        0xA9, 0xFF,       // skipped after resumed branch
        0xA9, 0x24,       // branch target
        0x4C, 0x00, 0x1C, // JMP $1C00
    };
    require_cached_matches_step(dey_bne_program, sizeof(dey_bne_program), 2, 1, "fused DEY/BNE");

    const uint8_t cmp_bcc_program[] = {
        0xA9, 0x10,       // LDA #$10
        0xC9, 0x20,       // CMP #$20
        0x90, 0x02,       // BCC $1C08
        0xA9, 0xFF,       // skipped after resumed branch
        0xA9, 0x11,       // branch target
        0x4C, 0x00, 0x1C, // JMP $1C00
    };
    require_cached_matches_step(cmp_bcc_program, sizeof(cmp_bcc_program), 2, 1, "fused CMP/BCC");
}

void test_run_cached_realish_stress_matches_step() {
    RamBus step_bus;
    RamBus cached_bus;
    step_bus.set_reset_vector(0x0400);
    cached_bus.set_reset_vector(0x0400);
    load_realish_program(step_bus, 0x0400);
    load_realish_program(cached_bus, 0x0400);
    Cpu6510 step_cpu(step_bus, Cpu6510Config{false});
    Cpu6510 cached_cpu(cached_bus, Cpu6510Config{false});
    step_cpu.reset();
    cached_cpu.reset();

    constexpr int iterations = 100;
    constexpr int instructions_per_iteration = 292;
    constexpr int total_instructions = iterations * instructions_per_iteration;
    run_steps(step_cpu, total_instructions, "reference step for realish cached stress");
    const RunResult cached = cached_cpu.run_cached(total_instructions);

    require(cached.result == StepResult::Ok, "run_cached realish stress returns Ok");
    require(cached.instructions_executed == total_instructions, "run_cached realish stress reports instruction budget");
    require_same_state(step_cpu.state(), cached_cpu.state(), "run_cached realish stress state");
    require(step_bus.memory == cached_bus.memory, "run_cached realish stress memory");
#if J6510_ENABLE_CACHE_STATS
    require(cached_cpu.block_cache_stats().fallback_instructions == 0, "run_cached realish stress stays in IR");
    require(cached_cpu.block_cache_stats().hits > 0, "run_cached realish stress records cache hits");
#endif
}

void test_undocumented_run_paths_match_step() {
    RamBus step_bus;
    RamBus run_bus;
    RamBus cached_bus;
    step_bus.set_reset_vector(0x1A00);
    run_bus.set_reset_vector(0x1A00);
    cached_bus.set_reset_vector(0x1A00);
    const uint8_t program[] = {
        0xA9, 0x11, // LDA #$11
        0xA2, 0x0F, // LDX #$0F
        0x87, 0x20, // SAX $20 -> $01
        0xA9, 0x40, // LDA #$40
        0x07, 0x20, // SLO $20 -> mem $02, A $42
        0xA7, 0x20, // LAX $20 -> A/X $02
        0xC7, 0x20, // DCP $20 -> mem $01, compare A
        0xE7, 0x20, // ISC $20 -> mem $02, SBC
        0x80, 0x99, // NOP #$99
        0x02,       // illegal sentinel
    };
    step_bus.load(0x1A00, program, sizeof(program));
    run_bus.load(0x1A00, program, sizeof(program));
    cached_bus.load(0x1A00, program, sizeof(program));
    const Cpu6510Config config{false, ExecutionMode::InstructionFast, true};
    Cpu6510 step_cpu(step_bus, config);
    Cpu6510 run_cpu(run_bus, config);
    Cpu6510 cached_cpu(cached_bus, config);
    step_cpu.reset();
    run_cpu.reset();
    cached_cpu.reset();

    for (int i = 0; i < 9; ++i) {
        require(step_cpu.step() == StepResult::Ok, "reference step for undocumented run paths executes");
    }
    const RunResult run_result = run_cpu.run(9);
    const RunResult cached_result = cached_cpu.run_cached(9);

    require(run_result.result == StepResult::Ok, "run undocumented path returns Ok");
    require(cached_result.result == StepResult::Ok, "run_cached undocumented path returns Ok");
    require_same_state(step_cpu.state(), run_cpu.state(), "run undocumented vs step");
    require_same_state(step_cpu.state(), cached_cpu.state(), "run_cached undocumented vs step");
    require(step_bus.memory == run_bus.memory, "run undocumented memory");
    require(step_bus.memory == cached_bus.memory, "run_cached undocumented memory");
#if J6510_ENABLE_CACHE_STATS
    require(cached_cpu.block_cache_stats().fallback_instructions == 9, "run_cached undocumented falls back to interpreter");
#endif

    require(step_cpu.step() == StepResult::IllegalOpcode, "reference step reaches undocumented profile sentinel");
    require(run_cpu.run(1).result == StepResult::IllegalOpcode, "run reaches undocumented profile sentinel");
    require(cached_cpu.run_cached(1).result == StepResult::IllegalOpcode, "run_cached reaches undocumented profile sentinel");
}

void test_cycle_exact_undocumented_representatives() {
    {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t program[] = {0x07, 0x10}; // SLO $10
        bus.load(0x0200, program, sizeof(program));
        bus.memory[0x0010] = 0x41;
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact, true});
        cpu.reset();
        cpu.state().a = 0x01;
        require(cpu.step() == StepResult::Ok, "cycle exact SLO zero page executes");
        require(cpu.cycle() == 5, "cycle exact SLO zero page cycle count");
        require(cpu.state().a == 0x83, "cycle exact SLO updates A");
        require(bus.memory[0x0010] == 0x82, "cycle exact SLO writes shifted memory");
    }
    {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t program[] = {0xA7, 0x10}; // LAX $10
        bus.load(0x0200, program, sizeof(program));
        bus.memory[0x0010] = 0x80;
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact, true});
        cpu.reset();
        require(cpu.step() == StepResult::Ok, "cycle exact LAX zero page executes");
        require(cpu.cycle() == 3, "cycle exact LAX zero page cycle count");
        require(cpu.state().a == 0x80, "cycle exact LAX updates A");
        require(cpu.state().x == 0x80, "cycle exact LAX updates X");
        require((cpu.state().p & FLAG_N) != 0, "cycle exact LAX sets N");
    }
    {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t program[] = {0x80, 0x55}; // NOP #$55
        bus.load(0x0200, program, sizeof(program));
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact, true});
        cpu.reset();
        require(cpu.step() == StepResult::Ok, "cycle exact immediate NOP executes");
        require(cpu.cycle() == 2, "cycle exact immediate NOP cycle count");
        require(cpu.state().pc == 0x0202, "cycle exact immediate NOP consumes operand");
    }
}

void test_cycle_exact_fixed_instruction_cycles() {
    const auto run_single = [](const uint8_t* program, uint16_t size, uint16_t pc, int expected_cycles, const std::string& context) {
        RamBus bus;
        bus.set_reset_vector(pc);
        bus.load(pc, program, size);
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        const uint64_t before = cpu.cycle();
        require(cpu.step() == StepResult::Ok, context + " executes");
        require(cpu.cycle() - before == static_cast<uint64_t>(expected_cycles), context + " cycle count");
        return bus;
    };

    {
        const uint8_t program[] = {0xA9, 0x44}; // LDA #$44
        run_single(program, sizeof(program), 0x0200, 2, "cycle exact LDA immediate");
    }
    {
        const uint8_t program[] = {0x8D, 0x00, 0x30}; // STA $3000
        RamBus bus;
        bus.set_reset_vector(0x0200);
        bus.load(0x0200, program, sizeof(program));
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        cpu.state().a = 0x5A;
        require(cpu.step() == StepResult::Ok, "cycle exact STA absolute executes");
        require(cpu.cycle() == 4, "cycle exact STA absolute cycle count");
        require(bus.memory[0x3000] == 0x5A, "cycle exact STA absolute writes memory");
    }
    {
        const uint8_t program[] = {0xE6, 0x10}; // INC $10
        RamBus bus;
        bus.set_reset_vector(0x0200);
        bus.load(0x0200, program, sizeof(program));
        bus.memory[0x0010] = 0x7F;
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        require(cpu.step() == StepResult::Ok, "cycle exact INC zero page executes");
        require(cpu.cycle() == 5, "cycle exact INC zero page cycle count");
        require(bus.memory[0x0010] == 0x80, "cycle exact INC zero page writes incremented value");
    }
    {
        const uint8_t program[] = {0x0E, 0x00, 0x30}; // ASL $3000
        RamBus bus;
        bus.set_reset_vector(0x0200);
        bus.load(0x0200, program, sizeof(program));
        bus.memory[0x3000] = 0x80;
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        require(cpu.step() == StepResult::Ok, "cycle exact ASL absolute executes");
        require(cpu.cycle() == 6, "cycle exact ASL absolute cycle count");
        require(bus.memory[0x3000] == 0x00, "cycle exact ASL absolute writes shifted value");
        require((cpu.state().p & FLAG_C) != 0, "cycle exact ASL absolute sets carry");
    }
    {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t main_program[] = {0x20, 0x10, 0x02}; // JSR $0210
        bus.load(0x0200, main_program, sizeof(main_program));
        bus.memory[0x0210] = 0x60; // RTS
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        require(cpu.step() == StepResult::Ok, "cycle exact JSR executes");
        require(cpu.cycle() == 6, "cycle exact JSR cycle count");
        require(cpu.state().pc == 0x0210, "cycle exact JSR jumps");
        require(cpu.step() == StepResult::Ok, "cycle exact RTS executes");
        require(cpu.cycle() == 12, "cycle exact RTS cycle count");
        require(cpu.state().pc == 0x0203, "cycle exact RTS returns");
    }
    {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        bus.set_irq_brk_vector(0x0300);
        bus.memory[0x0200] = 0x00; // BRK
        bus.memory[0x0201] = 0xEA;
        bus.memory[0x0300] = 0x40; // RTI
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        cpu.state().p = FLAG_U;
        require(cpu.step() == StepResult::Ok, "cycle exact BRK executes");
        require(cpu.cycle() == 7, "cycle exact BRK cycle count");
        require(cpu.state().pc == 0x0300, "cycle exact BRK vectors");
        require(cpu.step() == StepResult::Ok, "cycle exact RTI executes");
        require(cpu.cycle() == 13, "cycle exact RTI cycle count");
        require(cpu.state().pc == 0x0202, "cycle exact RTI restores PC");
    }
    {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t program[] = {0x6C, 0xFF, 0x30}; // JMP ($30FF)
        bus.load(0x0200, program, sizeof(program));
        bus.memory[0x30FF] = 0x34;
        bus.memory[0x3000] = 0x12;
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        require(cpu.step() == StepResult::Ok, "cycle exact JMP indirect executes");
        require(cpu.cycle() == 5, "cycle exact JMP indirect cycle count");
        require(cpu.state().pc == 0x1234, "cycle exact JMP indirect keeps NMOS page wrap bug");
    }
}

void test_cycle_exact_variable_cycles() {
    {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t program[] = {0xD0, 0x02}; // BNE
        bus.load(0x0200, program, sizeof(program));
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        cpu.state().p = FLAG_U | FLAG_Z;
        require(cpu.step() == StepResult::Ok, "cycle exact branch not taken executes");
        require(cpu.cycle() == 2, "cycle exact branch not taken cycles");
        require(cpu.state().pc == 0x0202, "cycle exact branch not taken PC");
    }
    {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t program[] = {0xD0, 0x02}; // BNE $0204
        bus.load(0x0200, program, sizeof(program));
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        cpu.state().p = FLAG_U;
        require(cpu.step() == StepResult::Ok, "cycle exact branch same-page executes");
        require(cpu.cycle() == 3, "cycle exact branch same-page cycles");
        require(cpu.state().pc == 0x0204, "cycle exact branch same-page PC");
    }
    {
        RamBus bus;
        bus.set_reset_vector(0x02FD);
        const uint8_t program[] = {0xD0, 0x01}; // after operand PC is $02FF, target $0300
        bus.load(0x02FD, program, sizeof(program));
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        cpu.state().p = FLAG_U;
        require(cpu.step() == StepResult::Ok, "cycle exact branch page-cross executes");
        require(cpu.cycle() == 4, "cycle exact branch page-cross cycles");
        require(cpu.state().pc == 0x0300, "cycle exact branch page-cross PC");
    }
    {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t program[] = {0xBD, 0x00, 0x30, 0xBD, 0xFF, 0x30}; // LDA abs,X twice
        bus.load(0x0200, program, sizeof(program));
        bus.memory[0x3001] = 0x11;
        bus.memory[0x3100] = 0x22;
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        cpu.state().x = 0x01;
        require(cpu.step() == StepResult::Ok, "cycle exact LDA abs,X same-page executes");
        require(cpu.cycle() == 4, "cycle exact LDA abs,X same-page cycles");
        require(cpu.state().a == 0x11, "cycle exact LDA abs,X same-page value");
        require(cpu.step() == StepResult::Ok, "cycle exact LDA abs,X page-cross executes");
        require(cpu.cycle() == 9, "cycle exact LDA abs,X page-cross cycles");
        require(cpu.state().a == 0x22, "cycle exact LDA abs,X page-cross value");
    }
    {
        RamBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t program[] = {0xB1, 0x20, 0xB1, 0x22}; // LDA ($20),Y twice
        bus.load(0x0200, program, sizeof(program));
        bus.memory[0x0020] = 0x00;
        bus.memory[0x0021] = 0x40;
        bus.memory[0x0022] = 0xFF;
        bus.memory[0x0023] = 0x40;
        bus.memory[0x4001] = 0x33;
        bus.memory[0x4100] = 0x44;
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        cpu.state().y = 0x01;
        require(cpu.step() == StepResult::Ok, "cycle exact LDA (zp),Y same-page executes");
        require(cpu.cycle() == 5, "cycle exact LDA (zp),Y same-page cycles");
        require(cpu.state().a == 0x33, "cycle exact LDA (zp),Y same-page value");
        require(cpu.step() == StepResult::Ok, "cycle exact LDA (zp),Y page-cross executes");
        require(cpu.cycle() == 11, "cycle exact LDA (zp),Y page-cross cycles");
        require(cpu.state().a == 0x44, "cycle exact LDA (zp),Y page-cross value");
    }
}

void test_cycle_exact_bus_sequence_for_rmw_and_branch() {
    {
        SpyBus bus;
        bus.set_reset_vector(0x0200);
        const uint8_t program[] = {0xE6, 0x10}; // INC $10
        bus.load(0x0200, program, sizeof(program));
        bus.memory[0x0010] = 0x7F;
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        bus.events.clear();
        require(cpu.step() == StepResult::Ok, "cycle exact spy INC executes");
        require(bus.events.size() == 5, "cycle exact spy INC event count");
        require_event(bus.events, 0, 'R', 0x0200, 0xE6, "cycle exact spy INC");
        require_event(bus.events, 1, 'R', 0x0201, 0x10, "cycle exact spy INC");
        require_event(bus.events, 2, 'R', 0x0010, 0x7F, "cycle exact spy INC");
        require_event(bus.events, 3, 'W', 0x0010, 0x7F, "cycle exact spy INC");
        require_event(bus.events, 4, 'W', 0x0010, 0x80, "cycle exact spy INC");
    }
    {
        SpyBus bus;
        bus.set_reset_vector(0x02FD);
        const uint8_t program[] = {0xD0, 0x01}; // BNE to $0300
        bus.load(0x02FD, program, sizeof(program));
        Cpu6510 cpu(bus, Cpu6510Config{false, ExecutionMode::CycleExact});
        cpu.reset();
        cpu.state().p = FLAG_U;
        bus.events.clear();
        require(cpu.step() == StepResult::Ok, "cycle exact spy branch executes");
        require(bus.events.size() == 4, "cycle exact spy branch event count");
        require_event(bus.events, 0, 'R', 0x02FD, 0xD0, "cycle exact spy branch");
        require_event(bus.events, 1, 'R', 0x02FE, 0x01, "cycle exact spy branch");
        require_event(bus.events, 2, 'R', 0x02FF, 0x00, "cycle exact spy branch");
        require_event(bus.events, 3, 'R', 0x0200, 0x00, "cycle exact spy branch");
    }
}

} // namespace

#if J6510_ENABLE_BLOCK_CACHE
namespace {

void test_run_cached_differential_fuzz() {
    // Straight-line random programs built from the cached-IR opcode pool,
    // executed K instructions via step() (reference) and run_cached(). Final
    // registers and the full 64 KB of RAM must match exactly.
    static const struct {
        uint8_t op;
        uint8_t len;
        uint8_t kind; // 0=imm 1=zp 2=zp indexed 3=abs 4=abs indexed 5=implied 6=indirect zp
    } pool[] = {
        {0xA9,2,0},{0xA2,2,0},{0xA0,2,0},{0x69,2,0},{0xE9,2,0},{0xC9,2,0},{0xE0,2,0},{0xC0,2,0},
        {0x09,2,0},{0x29,2,0},{0x49,2,0},
        {0xA5,2,1},{0xA6,2,1},{0xA4,2,1},{0x85,2,1},{0x86,2,1},{0x84,2,1},{0x65,2,1},{0xE5,2,1},
        {0xC5,2,1},{0xE4,2,1},{0xC4,2,1},{0x05,2,1},{0x25,2,1},{0x45,2,1},{0x24,2,1},{0xE6,2,1},
        {0xC6,2,1},{0x06,2,1},{0x46,2,1},{0x26,2,1},{0x66,2,1},
        {0xB5,2,2},{0x95,2,2},{0xF6,2,2},{0xD6,2,2},{0x94,2,2},{0x96,2,2},{0xB4,2,2},{0xB6,2,2},
        {0x15,2,2},{0x35,2,2},{0x55,2,2},{0x75,2,2},{0xF5,2,2},{0xD5,2,2},
        {0x16,2,2},{0x56,2,2},{0x36,2,2},{0x76,2,2},
        {0xAD,3,3},{0xAE,3,3},{0xAC,3,3},{0x8D,3,3},{0x8E,3,3},{0x8C,3,3},{0x6D,3,3},{0xED,3,3},
        {0xCD,3,3},{0xEC,3,3},{0xCC,3,3},{0x0D,3,3},{0x2D,3,3},{0x4D,3,3},{0x2C,3,3},{0xEE,3,3},
        {0xCE,3,3},{0x0E,3,3},{0x4E,3,3},{0x2E,3,3},{0x6E,3,3},
        {0xBD,3,4},{0xB9,3,4},{0x9D,3,4},{0x99,3,4},{0xBE,3,4},{0xBC,3,4},
        {0x1D,3,4},{0x3D,3,4},{0x5D,3,4},{0x7D,3,4},{0xFD,3,4},{0xDD,3,4},{0xFE,3,4},{0xDE,3,4},
        {0x19,3,4},{0x39,3,4},{0x59,3,4},{0x79,3,4},{0xF9,3,4},{0xD9,3,4},
        {0xAA,1,5},{0xA8,1,5},{0x8A,1,5},{0x98,1,5},{0xBA,1,5},{0x9A,1,5},{0xE8,1,5},{0xC8,1,5},
        {0xCA,1,5},{0x88,1,5},{0x0A,1,5},{0x4A,1,5},{0x2A,1,5},{0x6A,1,5},{0x48,1,5},{0x08,1,5},
        {0x68,1,5},{0x28,1,5},{0x18,1,5},{0x38,1,5},{0x58,1,5},{0x78,1,5},{0xB8,1,5},{0xD8,1,5},
        {0xF8,1,5},{0xEA,1,5},
        {0xA1,2,6},{0xB1,2,6},{0x81,2,6},{0x91,2,6},
        {0x01,2,6},{0x21,2,6},{0x41,2,6},{0x61,2,6},{0xE1,2,6},{0xC1,2,6},
        {0x11,2,6},{0x31,2,6},{0x51,2,6},{0x71,2,6},{0xF1,2,6},{0xD1,2,6},
    };

    constexpr int kInstructions = 200;
    constexpr uint32_t kSeeds = 400;
    for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
        std::mt19937 rng(seed);
        std::vector<uint8_t> program;
        program.reserve(kInstructions * 3);
        int generated = 0;
        while (generated < kInstructions) {
            const auto& entry = pool[rng() % (sizeof(pool) / sizeof(pool[0]))];
            program.push_back(entry.op);
            if (entry.len >= 2) {
                program.push_back(static_cast<uint8_t>(rng()));
            }
            if (entry.len == 3) {
                program.push_back(static_cast<uint8_t>(0x30 + (rng() % 2))); // $30xx/$31xx
            }
            ++generated;
        }

        RamBus step_bus;
        RamBus cached_bus;
        step_bus.set_reset_vector(0x0200);
        cached_bus.set_reset_vector(0x0200);
        step_bus.load(0x0200, program.data(), static_cast<uint16_t>(program.size()));
        cached_bus.load(0x0200, program.data(), static_cast<uint16_t>(program.size()));

        Cpu6510 reference(step_bus);
        reference.reset();
        bool reference_ok = true;
        for (int i = 0; i < kInstructions; ++i) {
            if (reference.step() != StepResult::Ok) {
                reference_ok = false;
                break;
            }
        }
        if (!reference_ok) {
            continue;
        }

        Cpu6510 cached(cached_bus);
        cached.reset();
        const RunResult result = cached.run_cached(kInstructions);
        const std::string context = " (fuzz seed " + std::to_string(seed) + ")";
        require(result.result == StepResult::Ok &&
                    result.instructions_executed == static_cast<uint32_t>(kInstructions),
                "differential fuzz cached run completes" + context);
        const Cpu6510State& a = reference.state();
        const Cpu6510State& b = cached.state();
        require(a.a == b.a && a.x == b.x && a.y == b.y && a.sp == b.sp && a.p == b.p && a.pc == b.pc,
                "differential fuzz registers match" + context);
        require(step_bus.memory == cached_bus.memory, "differential fuzz memory matches" + context);
    }
}

} // namespace
#endif

int main() {
    test_reset_loads_pc_from_vector();
    test_stack_wraps_on_page_one();
    test_opcode_metadata_and_illegal_opcode();
    test_opcode_table_has_all_legal_opcodes();
    test_undocumented_opcode_profile_metadata();
    test_undocumented_opcode_families();
    test_basic_instructions_end_to_end();
    test_jsr_and_rts();
    test_brk_and_rti();
    test_interrupt_placeholder_state_and_service();
    test_interrupt_poll_callback_nmi_edge_and_irq_level();
    test_indexed_addressing_modes();
    test_indirect_addressing_modes();
    test_zero_page_indirect_pointer_wraps();
    test_relative_branches_forward_and_backward();
    test_branch_conditions();
    test_jmp_indirect_uses_nmos_page_wrap_bug();
    test_ldx_ldy_and_store_modes();
    test_transfer_instructions_and_flags();
    test_stack_instructions_and_status_normalization();
    test_e2e_stack_transfer_program();
    test_port6510_read_write_masking_and_callback();
    test_flag_instructions();
    test_logic_bit_compare_inc_dec();
    test_shift_and_rotate_accumulator_and_memory();
    test_adc_sbc_binary_and_decimal_smoke();
    test_e2e_program_image_with_vectors_subroutine_and_brk_rti();
    test_e2e_memory_driven_branch_program();
    test_run_matches_step_for_e2e_program();
    test_run_block_stops_on_budget_and_control_flow();
    test_run_block_stops_on_illegal_and_interrupt_pending();
    test_cycle_exact_fixed_instruction_cycles();
    test_cycle_exact_variable_cycles();
    test_cycle_exact_bus_sequence_for_rmw_and_branch();
    test_cycle_exact_undocumented_representatives();
#if J6510_ENABLE_BLOCK_CACHE
    test_run_cached_matches_step_and_tracks_cache_stats();
    test_run_cached_hits_and_invalidates_after_write();
    test_run_cached_direct_path_with_6510_port_matches_step();
    test_run_cached_ir_mixed_loop_matches_step();
    test_run_cached_reports_ir_and_fallback_coverage();
    test_run_cached_ir_flags_and_branches_match_step();
    test_run_cached_ir_load_store_transfer_matches_step();
    test_run_cached_ir_indexed_loads_match_step();
    test_run_cached_ir_adc_cmp_inc_match_step();
    test_run_cached_fused_pairs_respect_budget_cuts();
    test_run_cached_realish_stress_matches_step();
    test_undocumented_run_paths_match_step();
    test_run_cached_differential_fuzz();
#endif

    std::cout << "All j6510 tests passed\n";
    return 0;
}
