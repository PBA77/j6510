#include "cpu6510_bus.h"
#include "cpu6510_core.h"
#include "cpu6510_opcode_table.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

} // namespace

int main() {
    test_reset_loads_pc_from_vector();
    test_stack_wraps_on_page_one();
    test_opcode_metadata_and_illegal_opcode();
    test_opcode_table_has_all_legal_opcodes();
    test_basic_instructions_end_to_end();
    test_jsr_and_rts();
    test_brk_and_rti();
    test_interrupt_placeholder_state_and_service();
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

    std::cout << "All j6510 tests passed\n";
    return 0;
}
