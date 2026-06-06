#pragma once

#include <array>
#include <cstdint>

namespace j6510 {

enum class AddressingMode : uint8_t {
    Implied,
    Accumulator,
    Immediate,
    ZeroPage,
    ZeroPageX,
    ZeroPageY,
    Absolute,
    AbsoluteX,
    AbsoluteY,
    Relative,
    Indirect,
    IndexedIndirect,
    IndirectIndexed,
};

enum class Operation : uint8_t {
    Illegal,
    LDA,
    LDX,
    LDY,
    STA,
    STX,
    STY,
    AND,
    ORA,
    EOR,
    BIT,
    ADC,
    SBC,
    CMP,
    CPX,
    CPY,
    INC,
    DEC,
    ASL,
    LSR,
    ROL,
    ROR,
    TAX,
    TAY,
    TXA,
    TYA,
    TSX,
    TXS,
    INX,
    INY,
    DEX,
    DEY,
    PHA,
    PHP,
    PLA,
    PLP,
    CLC,
    SEC,
    CLI,
    SEI,
    CLV,
    CLD,
    SED,
    JMP,
    JSR,
    RTS,
    BRK,
    RTI,
    NOP,
    BPL,
    BMI,
    BVC,
    BVS,
    BCC,
    BCS,
    BNE,
    BEQ,
};

struct OpcodeInfo {
    Operation operation = Operation::Illegal;
    AddressingMode mode = AddressingMode::Implied;
    const char* mnemonic = "???";
    uint8_t bytes = 1;
};

const std::array<OpcodeInfo, 256>& opcode_table();
const OpcodeInfo& opcode_info(uint8_t opcode);

} // namespace j6510
