#include "cpu6510_opcode_table.h"

namespace j6510 {

namespace {

constexpr OpcodeInfo illegal_opcode() {
    return {};
}

std::array<OpcodeInfo, 256> make_opcode_table() {
    std::array<OpcodeInfo, 256> table{};
    table.fill(illegal_opcode());

    auto op = [&table](uint8_t opcode, Operation operation, AddressingMode mode, const char* mnemonic, uint8_t bytes) {
        table[opcode] = {operation, mode, mnemonic, bytes};
    };

    op(0x00, Operation::BRK, AddressingMode::Implied, "BRK", 1);
    op(0x01, Operation::ORA, AddressingMode::IndexedIndirect, "ORA", 2);
    op(0x05, Operation::ORA, AddressingMode::ZeroPage, "ORA", 2);
    op(0x06, Operation::ASL, AddressingMode::ZeroPage, "ASL", 2);
    op(0x08, Operation::PHP, AddressingMode::Implied, "PHP", 1);
    op(0x09, Operation::ORA, AddressingMode::Immediate, "ORA", 2);
    op(0x0A, Operation::ASL, AddressingMode::Accumulator, "ASL", 1);
    op(0x0D, Operation::ORA, AddressingMode::Absolute, "ORA", 3);
    op(0x0E, Operation::ASL, AddressingMode::Absolute, "ASL", 3);

    op(0x10, Operation::BPL, AddressingMode::Relative, "BPL", 2);
    op(0x11, Operation::ORA, AddressingMode::IndirectIndexed, "ORA", 2);
    op(0x15, Operation::ORA, AddressingMode::ZeroPageX, "ORA", 2);
    op(0x16, Operation::ASL, AddressingMode::ZeroPageX, "ASL", 2);
    op(0x18, Operation::CLC, AddressingMode::Implied, "CLC", 1);
    op(0x19, Operation::ORA, AddressingMode::AbsoluteY, "ORA", 3);
    op(0x1D, Operation::ORA, AddressingMode::AbsoluteX, "ORA", 3);
    op(0x1E, Operation::ASL, AddressingMode::AbsoluteX, "ASL", 3);

    op(0x20, Operation::JSR, AddressingMode::Absolute, "JSR", 3);
    op(0x21, Operation::AND, AddressingMode::IndexedIndirect, "AND", 2);
    op(0x24, Operation::BIT, AddressingMode::ZeroPage, "BIT", 2);
    op(0x25, Operation::AND, AddressingMode::ZeroPage, "AND", 2);
    op(0x26, Operation::ROL, AddressingMode::ZeroPage, "ROL", 2);
    op(0x28, Operation::PLP, AddressingMode::Implied, "PLP", 1);
    op(0x29, Operation::AND, AddressingMode::Immediate, "AND", 2);
    op(0x2A, Operation::ROL, AddressingMode::Accumulator, "ROL", 1);
    op(0x2C, Operation::BIT, AddressingMode::Absolute, "BIT", 3);
    op(0x2D, Operation::AND, AddressingMode::Absolute, "AND", 3);
    op(0x2E, Operation::ROL, AddressingMode::Absolute, "ROL", 3);

    op(0x30, Operation::BMI, AddressingMode::Relative, "BMI", 2);
    op(0x31, Operation::AND, AddressingMode::IndirectIndexed, "AND", 2);
    op(0x35, Operation::AND, AddressingMode::ZeroPageX, "AND", 2);
    op(0x36, Operation::ROL, AddressingMode::ZeroPageX, "ROL", 2);
    op(0x38, Operation::SEC, AddressingMode::Implied, "SEC", 1);
    op(0x39, Operation::AND, AddressingMode::AbsoluteY, "AND", 3);
    op(0x3D, Operation::AND, AddressingMode::AbsoluteX, "AND", 3);
    op(0x3E, Operation::ROL, AddressingMode::AbsoluteX, "ROL", 3);

    op(0x40, Operation::RTI, AddressingMode::Implied, "RTI", 1);
    op(0x41, Operation::EOR, AddressingMode::IndexedIndirect, "EOR", 2);
    op(0x45, Operation::EOR, AddressingMode::ZeroPage, "EOR", 2);
    op(0x46, Operation::LSR, AddressingMode::ZeroPage, "LSR", 2);
    op(0x48, Operation::PHA, AddressingMode::Implied, "PHA", 1);
    op(0x49, Operation::EOR, AddressingMode::Immediate, "EOR", 2);
    op(0x4A, Operation::LSR, AddressingMode::Accumulator, "LSR", 1);
    op(0x4C, Operation::JMP, AddressingMode::Absolute, "JMP", 3);
    op(0x4D, Operation::EOR, AddressingMode::Absolute, "EOR", 3);
    op(0x4E, Operation::LSR, AddressingMode::Absolute, "LSR", 3);

    op(0x50, Operation::BVC, AddressingMode::Relative, "BVC", 2);
    op(0x51, Operation::EOR, AddressingMode::IndirectIndexed, "EOR", 2);
    op(0x55, Operation::EOR, AddressingMode::ZeroPageX, "EOR", 2);
    op(0x56, Operation::LSR, AddressingMode::ZeroPageX, "LSR", 2);
    op(0x58, Operation::CLI, AddressingMode::Implied, "CLI", 1);
    op(0x59, Operation::EOR, AddressingMode::AbsoluteY, "EOR", 3);
    op(0x5D, Operation::EOR, AddressingMode::AbsoluteX, "EOR", 3);
    op(0x5E, Operation::LSR, AddressingMode::AbsoluteX, "LSR", 3);

    op(0x60, Operation::RTS, AddressingMode::Implied, "RTS", 1);
    op(0x61, Operation::ADC, AddressingMode::IndexedIndirect, "ADC", 2);
    op(0x65, Operation::ADC, AddressingMode::ZeroPage, "ADC", 2);
    op(0x66, Operation::ROR, AddressingMode::ZeroPage, "ROR", 2);
    op(0x68, Operation::PLA, AddressingMode::Implied, "PLA", 1);
    op(0x69, Operation::ADC, AddressingMode::Immediate, "ADC", 2);
    op(0x6A, Operation::ROR, AddressingMode::Accumulator, "ROR", 1);
    op(0x6C, Operation::JMP, AddressingMode::Indirect, "JMP", 3);
    op(0x6D, Operation::ADC, AddressingMode::Absolute, "ADC", 3);
    op(0x6E, Operation::ROR, AddressingMode::Absolute, "ROR", 3);

    op(0x70, Operation::BVS, AddressingMode::Relative, "BVS", 2);
    op(0x71, Operation::ADC, AddressingMode::IndirectIndexed, "ADC", 2);
    op(0x75, Operation::ADC, AddressingMode::ZeroPageX, "ADC", 2);
    op(0x76, Operation::ROR, AddressingMode::ZeroPageX, "ROR", 2);
    op(0x78, Operation::SEI, AddressingMode::Implied, "SEI", 1);
    op(0x79, Operation::ADC, AddressingMode::AbsoluteY, "ADC", 3);
    op(0x7D, Operation::ADC, AddressingMode::AbsoluteX, "ADC", 3);
    op(0x7E, Operation::ROR, AddressingMode::AbsoluteX, "ROR", 3);

    op(0x81, Operation::STA, AddressingMode::IndexedIndirect, "STA", 2);
    op(0x84, Operation::STY, AddressingMode::ZeroPage, "STY", 2);
    op(0x85, Operation::STA, AddressingMode::ZeroPage, "STA", 2);
    op(0x86, Operation::STX, AddressingMode::ZeroPage, "STX", 2);
    op(0x88, Operation::DEY, AddressingMode::Implied, "DEY", 1);
    op(0x8A, Operation::TXA, AddressingMode::Implied, "TXA", 1);
    op(0x8C, Operation::STY, AddressingMode::Absolute, "STY", 3);
    op(0x8D, Operation::STA, AddressingMode::Absolute, "STA", 3);
    op(0x8E, Operation::STX, AddressingMode::Absolute, "STX", 3);

    op(0x90, Operation::BCC, AddressingMode::Relative, "BCC", 2);
    op(0x91, Operation::STA, AddressingMode::IndirectIndexed, "STA", 2);
    op(0x94, Operation::STY, AddressingMode::ZeroPageX, "STY", 2);
    op(0x95, Operation::STA, AddressingMode::ZeroPageX, "STA", 2);
    op(0x96, Operation::STX, AddressingMode::ZeroPageY, "STX", 2);
    op(0x98, Operation::TYA, AddressingMode::Implied, "TYA", 1);
    op(0x99, Operation::STA, AddressingMode::AbsoluteY, "STA", 3);
    op(0x9A, Operation::TXS, AddressingMode::Implied, "TXS", 1);
    op(0x9D, Operation::STA, AddressingMode::AbsoluteX, "STA", 3);

    op(0xA0, Operation::LDY, AddressingMode::Immediate, "LDY", 2);
    op(0xA1, Operation::LDA, AddressingMode::IndexedIndirect, "LDA", 2);
    op(0xA2, Operation::LDX, AddressingMode::Immediate, "LDX", 2);
    op(0xA4, Operation::LDY, AddressingMode::ZeroPage, "LDY", 2);
    op(0xA5, Operation::LDA, AddressingMode::ZeroPage, "LDA", 2);
    op(0xA6, Operation::LDX, AddressingMode::ZeroPage, "LDX", 2);
    op(0xA8, Operation::TAY, AddressingMode::Implied, "TAY", 1);
    op(0xA9, Operation::LDA, AddressingMode::Immediate, "LDA", 2);
    op(0xAA, Operation::TAX, AddressingMode::Implied, "TAX", 1);
    op(0xAC, Operation::LDY, AddressingMode::Absolute, "LDY", 3);
    op(0xAD, Operation::LDA, AddressingMode::Absolute, "LDA", 3);
    op(0xAE, Operation::LDX, AddressingMode::Absolute, "LDX", 3);

    op(0xB0, Operation::BCS, AddressingMode::Relative, "BCS", 2);
    op(0xB1, Operation::LDA, AddressingMode::IndirectIndexed, "LDA", 2);
    op(0xB4, Operation::LDY, AddressingMode::ZeroPageX, "LDY", 2);
    op(0xB5, Operation::LDA, AddressingMode::ZeroPageX, "LDA", 2);
    op(0xB6, Operation::LDX, AddressingMode::ZeroPageY, "LDX", 2);
    op(0xB8, Operation::CLV, AddressingMode::Implied, "CLV", 1);
    op(0xB9, Operation::LDA, AddressingMode::AbsoluteY, "LDA", 3);
    op(0xBA, Operation::TSX, AddressingMode::Implied, "TSX", 1);
    op(0xBC, Operation::LDY, AddressingMode::AbsoluteX, "LDY", 3);
    op(0xBD, Operation::LDA, AddressingMode::AbsoluteX, "LDA", 3);
    op(0xBE, Operation::LDX, AddressingMode::AbsoluteY, "LDX", 3);

    op(0xC0, Operation::CPY, AddressingMode::Immediate, "CPY", 2);
    op(0xC1, Operation::CMP, AddressingMode::IndexedIndirect, "CMP", 2);
    op(0xC4, Operation::CPY, AddressingMode::ZeroPage, "CPY", 2);
    op(0xC5, Operation::CMP, AddressingMode::ZeroPage, "CMP", 2);
    op(0xC6, Operation::DEC, AddressingMode::ZeroPage, "DEC", 2);
    op(0xC8, Operation::INY, AddressingMode::Implied, "INY", 1);
    op(0xC9, Operation::CMP, AddressingMode::Immediate, "CMP", 2);
    op(0xCA, Operation::DEX, AddressingMode::Implied, "DEX", 1);
    op(0xCC, Operation::CPY, AddressingMode::Absolute, "CPY", 3);
    op(0xCD, Operation::CMP, AddressingMode::Absolute, "CMP", 3);
    op(0xCE, Operation::DEC, AddressingMode::Absolute, "DEC", 3);

    op(0xD0, Operation::BNE, AddressingMode::Relative, "BNE", 2);
    op(0xD1, Operation::CMP, AddressingMode::IndirectIndexed, "CMP", 2);
    op(0xD5, Operation::CMP, AddressingMode::ZeroPageX, "CMP", 2);
    op(0xD6, Operation::DEC, AddressingMode::ZeroPageX, "DEC", 2);
    op(0xD8, Operation::CLD, AddressingMode::Implied, "CLD", 1);
    op(0xD9, Operation::CMP, AddressingMode::AbsoluteY, "CMP", 3);
    op(0xDD, Operation::CMP, AddressingMode::AbsoluteX, "CMP", 3);
    op(0xDE, Operation::DEC, AddressingMode::AbsoluteX, "DEC", 3);

    op(0xE0, Operation::CPX, AddressingMode::Immediate, "CPX", 2);
    op(0xE1, Operation::SBC, AddressingMode::IndexedIndirect, "SBC", 2);
    op(0xE4, Operation::CPX, AddressingMode::ZeroPage, "CPX", 2);
    op(0xE5, Operation::SBC, AddressingMode::ZeroPage, "SBC", 2);
    op(0xE6, Operation::INC, AddressingMode::ZeroPage, "INC", 2);
    op(0xE8, Operation::INX, AddressingMode::Implied, "INX", 1);
    op(0xE9, Operation::SBC, AddressingMode::Immediate, "SBC", 2);
    op(0xEA, Operation::NOP, AddressingMode::Implied, "NOP", 1);
    op(0xEC, Operation::CPX, AddressingMode::Absolute, "CPX", 3);
    op(0xED, Operation::SBC, AddressingMode::Absolute, "SBC", 3);
    op(0xEE, Operation::INC, AddressingMode::Absolute, "INC", 3);

    op(0xF0, Operation::BEQ, AddressingMode::Relative, "BEQ", 2);
    op(0xF1, Operation::SBC, AddressingMode::IndirectIndexed, "SBC", 2);
    op(0xF5, Operation::SBC, AddressingMode::ZeroPageX, "SBC", 2);
    op(0xF6, Operation::INC, AddressingMode::ZeroPageX, "INC", 2);
    op(0xF8, Operation::SED, AddressingMode::Implied, "SED", 1);
    op(0xF9, Operation::SBC, AddressingMode::AbsoluteY, "SBC", 3);
    op(0xFD, Operation::SBC, AddressingMode::AbsoluteX, "SBC", 3);
    op(0xFE, Operation::INC, AddressingMode::AbsoluteX, "INC", 3);

    return table;
}

} // namespace

const std::array<OpcodeInfo, 256>& opcode_table() {
    static const auto table = make_opcode_table();
    return table;
}

const OpcodeInfo& opcode_info(uint8_t opcode) {
    return opcode_table()[opcode];
}

} // namespace j6510
