#include "cpu6510_opcode_table.h"

namespace j6510 {

namespace {

constexpr OpcodeInfo illegal_opcode() {
    return {};
}

constexpr bool is_branch(Operation operation) {
    return operation == Operation::BPL || operation == Operation::BMI ||
           operation == Operation::BVC || operation == Operation::BVS ||
           operation == Operation::BCC || operation == Operation::BCS ||
           operation == Operation::BNE || operation == Operation::BEQ;
}

constexpr bool is_read_page_cross_candidate(Operation operation, AddressingMode mode) {
    const bool read_operation =
        operation == Operation::LDA || operation == Operation::LDX ||
        operation == Operation::LDY || operation == Operation::AND ||
        operation == Operation::ORA || operation == Operation::EOR ||
        operation == Operation::ADC || operation == Operation::SBC ||
        operation == Operation::CMP || operation == Operation::LAX ||
        operation == Operation::NOP;
    return read_operation &&
           (mode == AddressingMode::AbsoluteX || mode == AddressingMode::AbsoluteY ||
            mode == AddressingMode::IndirectIndexed);
}

constexpr uint8_t nominal_cycles(Operation operation, AddressingMode mode) {
    if (is_branch(operation)) {
        return 2;
    }

    switch (operation) {
    case Operation::BRK:
        return 7;
    case Operation::JSR:
        return 6;
    case Operation::RTS:
        return 6;
    case Operation::RTI:
        return 6;
    case Operation::PHA:
    case Operation::PHP:
        return 3;
    case Operation::PLA:
    case Operation::PLP:
        return 4;
    case Operation::JMP:
        return mode == AddressingMode::Indirect ? 5 : 3;
    case Operation::ASL:
    case Operation::LSR:
    case Operation::ROL:
    case Operation::ROR:
    case Operation::SLO:
    case Operation::RLA:
    case Operation::SRE:
    case Operation::RRA:
    case Operation::DCP:
    case Operation::ISC:
        if (mode == AddressingMode::Accumulator) {
            return 2;
        }
        if (mode == AddressingMode::ZeroPage) {
            return 5;
        }
        if (mode == AddressingMode::ZeroPageX) {
            return 6;
        }
        if (mode == AddressingMode::Absolute) {
            return 6;
        }
        if (mode == AddressingMode::IndexedIndirect || mode == AddressingMode::IndirectIndexed) {
            return 8;
        }
        return 7;
    case Operation::INC:
    case Operation::DEC:
        if (mode == AddressingMode::ZeroPage) {
            return 5;
        }
        if (mode == AddressingMode::ZeroPageX) {
            return 6;
        }
        if (mode == AddressingMode::Absolute) {
            return 6;
        }
        return 7;
    case Operation::STA:
        if (mode == AddressingMode::IndexedIndirect) {
            return 6;
        }
        if (mode == AddressingMode::IndirectIndexed) {
            return 6;
        }
        if (mode == AddressingMode::ZeroPage) {
            return 3;
        }
        if (mode == AddressingMode::ZeroPageX || mode == AddressingMode::ZeroPageY) {
            return 4;
        }
        if (mode == AddressingMode::Absolute) {
            return 4;
        }
        return 5;
    case Operation::STX:
    case Operation::STY:
    case Operation::SAX:
        if (mode == AddressingMode::IndexedIndirect) {
            return 6;
        }
        if (mode == AddressingMode::ZeroPage) {
            return 3;
        }
        if (mode == AddressingMode::ZeroPageX || mode == AddressingMode::ZeroPageY) {
            return 4;
        }
        return 4;
    case Operation::NOP:
        if (mode == AddressingMode::Implied) {
            return 2;
        }
        if (mode == AddressingMode::Immediate) {
            return 2;
        }
        if (mode == AddressingMode::ZeroPage) {
            return 3;
        }
        if (mode == AddressingMode::ZeroPageX) {
            return 4;
        }
        if (mode == AddressingMode::Absolute) {
            return 4;
        }
        if (mode == AddressingMode::AbsoluteX) {
            return 4;
        }
        return 2;
    default:
        break;
    }

    switch (mode) {
    case AddressingMode::Implied:
    case AddressingMode::Accumulator:
        return 2;
    case AddressingMode::Immediate:
        return 2;
    case AddressingMode::ZeroPage:
        return 3;
    case AddressingMode::ZeroPageX:
    case AddressingMode::ZeroPageY:
        return 4;
    case AddressingMode::Absolute:
        return 4;
    case AddressingMode::AbsoluteX:
    case AddressingMode::AbsoluteY:
        return 4;
    case AddressingMode::IndexedIndirect:
        return 6;
    case AddressingMode::IndirectIndexed:
        return 5;
    case AddressingMode::Indirect:
        return 5;
    case AddressingMode::Relative:
        return 2;
    }
    return 1;
}

std::array<OpcodeInfo, 256> make_opcode_table() {
    std::array<OpcodeInfo, 256> table{};
    table.fill(illegal_opcode());

    auto op = [&table](uint8_t opcode, Operation operation, AddressingMode mode, const char* mnemonic, uint8_t bytes) {
        table[opcode] = {operation,
                         mode,
                         mnemonic,
                         bytes,
                         nominal_cycles(operation, mode),
                         is_branch(operation),
                         is_read_page_cross_candidate(operation, mode)};
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

std::array<OpcodeInfo, 256> make_undocumented_opcode_table() {
    std::array<OpcodeInfo, 256> table = make_opcode_table();

    auto op = [&table](uint8_t opcode, Operation operation, AddressingMode mode, const char* mnemonic, uint8_t bytes) {
        table[opcode] = {operation,
                         mode,
                         mnemonic,
                         bytes,
                         nominal_cycles(operation, mode),
                         is_branch(operation),
                         is_read_page_cross_candidate(operation, mode)};
    };

    op(0x03, Operation::SLO, AddressingMode::IndexedIndirect, "SLO", 2);
    op(0x07, Operation::SLO, AddressingMode::ZeroPage, "SLO", 2);
    op(0x0F, Operation::SLO, AddressingMode::Absolute, "SLO", 3);
    op(0x13, Operation::SLO, AddressingMode::IndirectIndexed, "SLO", 2);
    op(0x17, Operation::SLO, AddressingMode::ZeroPageX, "SLO", 2);
    op(0x1B, Operation::SLO, AddressingMode::AbsoluteY, "SLO", 3);
    op(0x1F, Operation::SLO, AddressingMode::AbsoluteX, "SLO", 3);

    op(0x23, Operation::RLA, AddressingMode::IndexedIndirect, "RLA", 2);
    op(0x27, Operation::RLA, AddressingMode::ZeroPage, "RLA", 2);
    op(0x2F, Operation::RLA, AddressingMode::Absolute, "RLA", 3);
    op(0x33, Operation::RLA, AddressingMode::IndirectIndexed, "RLA", 2);
    op(0x37, Operation::RLA, AddressingMode::ZeroPageX, "RLA", 2);
    op(0x3B, Operation::RLA, AddressingMode::AbsoluteY, "RLA", 3);
    op(0x3F, Operation::RLA, AddressingMode::AbsoluteX, "RLA", 3);

    op(0x43, Operation::SRE, AddressingMode::IndexedIndirect, "SRE", 2);
    op(0x47, Operation::SRE, AddressingMode::ZeroPage, "SRE", 2);
    op(0x4F, Operation::SRE, AddressingMode::Absolute, "SRE", 3);
    op(0x53, Operation::SRE, AddressingMode::IndirectIndexed, "SRE", 2);
    op(0x57, Operation::SRE, AddressingMode::ZeroPageX, "SRE", 2);
    op(0x5B, Operation::SRE, AddressingMode::AbsoluteY, "SRE", 3);
    op(0x5F, Operation::SRE, AddressingMode::AbsoluteX, "SRE", 3);

    op(0x63, Operation::RRA, AddressingMode::IndexedIndirect, "RRA", 2);
    op(0x67, Operation::RRA, AddressingMode::ZeroPage, "RRA", 2);
    op(0x6F, Operation::RRA, AddressingMode::Absolute, "RRA", 3);
    op(0x73, Operation::RRA, AddressingMode::IndirectIndexed, "RRA", 2);
    op(0x77, Operation::RRA, AddressingMode::ZeroPageX, "RRA", 2);
    op(0x7B, Operation::RRA, AddressingMode::AbsoluteY, "RRA", 3);
    op(0x7F, Operation::RRA, AddressingMode::AbsoluteX, "RRA", 3);

    op(0x83, Operation::SAX, AddressingMode::IndexedIndirect, "SAX", 2);
    op(0x87, Operation::SAX, AddressingMode::ZeroPage, "SAX", 2);
    op(0x8F, Operation::SAX, AddressingMode::Absolute, "SAX", 3);
    op(0x97, Operation::SAX, AddressingMode::ZeroPageY, "SAX", 2);

    op(0xA3, Operation::LAX, AddressingMode::IndexedIndirect, "LAX", 2);
    op(0xA7, Operation::LAX, AddressingMode::ZeroPage, "LAX", 2);
    op(0xAF, Operation::LAX, AddressingMode::Absolute, "LAX", 3);
    op(0xB3, Operation::LAX, AddressingMode::IndirectIndexed, "LAX", 2);
    op(0xB7, Operation::LAX, AddressingMode::ZeroPageY, "LAX", 2);
    op(0xBF, Operation::LAX, AddressingMode::AbsoluteY, "LAX", 3);

    op(0xC3, Operation::DCP, AddressingMode::IndexedIndirect, "DCP", 2);
    op(0xC7, Operation::DCP, AddressingMode::ZeroPage, "DCP", 2);
    op(0xCF, Operation::DCP, AddressingMode::Absolute, "DCP", 3);
    op(0xD3, Operation::DCP, AddressingMode::IndirectIndexed, "DCP", 2);
    op(0xD7, Operation::DCP, AddressingMode::ZeroPageX, "DCP", 2);
    op(0xDB, Operation::DCP, AddressingMode::AbsoluteY, "DCP", 3);
    op(0xDF, Operation::DCP, AddressingMode::AbsoluteX, "DCP", 3);

    op(0xE3, Operation::ISC, AddressingMode::IndexedIndirect, "ISC", 2);
    op(0xE7, Operation::ISC, AddressingMode::ZeroPage, "ISC", 2);
    op(0xEB, Operation::SBC, AddressingMode::Immediate, "SBC", 2);
    op(0xEF, Operation::ISC, AddressingMode::Absolute, "ISC", 3);
    op(0xF3, Operation::ISC, AddressingMode::IndirectIndexed, "ISC", 2);
    op(0xF7, Operation::ISC, AddressingMode::ZeroPageX, "ISC", 2);
    op(0xFB, Operation::ISC, AddressingMode::AbsoluteY, "ISC", 3);
    op(0xFF, Operation::ISC, AddressingMode::AbsoluteX, "ISC", 3);

    op(0x04, Operation::NOP, AddressingMode::ZeroPage, "NOP", 2);
    op(0x0C, Operation::NOP, AddressingMode::Absolute, "NOP", 3);
    op(0x14, Operation::NOP, AddressingMode::ZeroPageX, "NOP", 2);
    op(0x1A, Operation::NOP, AddressingMode::Implied, "NOP", 1);
    op(0x1C, Operation::NOP, AddressingMode::AbsoluteX, "NOP", 3);
    op(0x34, Operation::NOP, AddressingMode::ZeroPageX, "NOP", 2);
    op(0x3A, Operation::NOP, AddressingMode::Implied, "NOP", 1);
    op(0x3C, Operation::NOP, AddressingMode::AbsoluteX, "NOP", 3);
    op(0x44, Operation::NOP, AddressingMode::ZeroPage, "NOP", 2);
    op(0x54, Operation::NOP, AddressingMode::ZeroPageX, "NOP", 2);
    op(0x5A, Operation::NOP, AddressingMode::Implied, "NOP", 1);
    op(0x5C, Operation::NOP, AddressingMode::AbsoluteX, "NOP", 3);
    op(0x64, Operation::NOP, AddressingMode::ZeroPage, "NOP", 2);
    op(0x74, Operation::NOP, AddressingMode::ZeroPageX, "NOP", 2);
    op(0x7A, Operation::NOP, AddressingMode::Implied, "NOP", 1);
    op(0x7C, Operation::NOP, AddressingMode::AbsoluteX, "NOP", 3);
    op(0x80, Operation::NOP, AddressingMode::Immediate, "NOP", 2);
    op(0x82, Operation::NOP, AddressingMode::Immediate, "NOP", 2);
    op(0x89, Operation::NOP, AddressingMode::Immediate, "NOP", 2);
    op(0xC2, Operation::NOP, AddressingMode::Immediate, "NOP", 2);
    op(0xD4, Operation::NOP, AddressingMode::ZeroPageX, "NOP", 2);
    op(0xDA, Operation::NOP, AddressingMode::Implied, "NOP", 1);
    op(0xDC, Operation::NOP, AddressingMode::AbsoluteX, "NOP", 3);
    op(0xE2, Operation::NOP, AddressingMode::Immediate, "NOP", 2);
    op(0xF4, Operation::NOP, AddressingMode::ZeroPageX, "NOP", 2);
    op(0xFA, Operation::NOP, AddressingMode::Implied, "NOP", 1);
    op(0xFC, Operation::NOP, AddressingMode::AbsoluteX, "NOP", 3);

    return table;
}

} // namespace

const std::array<OpcodeInfo, 256>& opcode_table() {
    static const auto table = make_opcode_table();
    return table;
}

const std::array<OpcodeInfo, 256>& undocumented_opcode_table() {
    static const auto table = make_undocumented_opcode_table();
    return table;
}

const OpcodeInfo& opcode_info(uint8_t opcode) {
    return opcode_table()[opcode];
}

const OpcodeInfo& undocumented_opcode_info(uint8_t opcode) {
    return undocumented_opcode_table()[opcode];
}

} // namespace j6510
