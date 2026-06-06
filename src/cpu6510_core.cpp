#include "cpu6510_core.h"

#include <utility>

namespace j6510 {

Cpu6510::Cpu6510(Bus& bus) : Cpu6510(bus, Cpu6510Config{}) {}

Cpu6510::Cpu6510(Bus& bus, Cpu6510Config config) : bus_(bus), config_(config) {}

Cpu6510State& Cpu6510::state() {
    return state_;
}

const Cpu6510State& Cpu6510::state() const {
    return state_;
}

const InterruptState& Cpu6510::interrupts() const {
    return interrupts_;
}

const Port6510State& Cpu6510::port() const {
    return port_;
}

void Cpu6510::request_reset() {
    interrupts_.reset_pending = true;
}

void Cpu6510::pulse_nmi() {
    interrupts_.nmi_pending = true;
}

void Cpu6510::set_irq_level(bool active) {
    interrupts_.irq_level = active;
}

void Cpu6510::clear_irq_level() {
    interrupts_.irq_level = false;
}

void Cpu6510::poll_target_interrupts() {
    if (interrupt_poll_callback_) {
        interrupt_poll_callback_(*this);
    }
}

void Cpu6510::set_interrupt_poll_callback(InterruptPollCallback callback) {
    interrupt_poll_callback_ = std::move(callback);
}

void Cpu6510::service_pending_interrupt_if_needed() {
    if (interrupts_.reset_pending) {
        reset();
        interrupts_.reset_pending = false;
        return;
    }

    if (interrupts_.nmi_pending) {
        interrupts_.nmi_pending = false;
        interrupt(0xFFFA, false, state_.pc);
        return;
    }

    if (interrupts_.irq_level && !flag(FLAG_I)) {
        interrupt(0xFFFE, false, state_.pc);
    }
}

void Cpu6510::reset() {
    state_.sp = 0xFD;
    state_.p = FLAG_U | FLAG_I;
    state_.pc = read16(0xFFFC);
}

StepResult Cpu6510::step() {
    poll_target_interrupts();
    service_pending_interrupt_if_needed();

    const uint16_t instruction_pc = state_.pc;
    const uint8_t opcode = fetch8();
    const OpcodeInfo& info = opcode_info(opcode);

    switch (info.operation) {
    case Operation::Illegal:
        state_.pc = instruction_pc;
        return StepResult::IllegalOpcode;

    case Operation::LDA:
        state_.a = read_operand(info.mode);
        set_zn(state_.a);
        break;

    case Operation::LDX:
        state_.x = read_operand(info.mode);
        set_zn(state_.x);
        break;

    case Operation::LDY:
        state_.y = read_operand(info.mode);
        set_zn(state_.y);
        break;

    case Operation::STA:
        write(operand_address(info.mode), state_.a);
        break;

    case Operation::STX:
        write(operand_address(info.mode), state_.x);
        break;

    case Operation::STY:
        write(operand_address(info.mode), state_.y);
        break;

    case Operation::AND:
        state_.a = static_cast<uint8_t>(state_.a & read_operand(info.mode));
        set_zn(state_.a);
        break;

    case Operation::ORA:
        state_.a = static_cast<uint8_t>(state_.a | read_operand(info.mode));
        set_zn(state_.a);
        break;

    case Operation::EOR:
        state_.a = static_cast<uint8_t>(state_.a ^ read_operand(info.mode));
        set_zn(state_.a);
        break;

    case Operation::BIT: {
        const uint8_t value = read_operand(info.mode);
        set_flag(FLAG_Z, (state_.a & value) == 0);
        set_flag(FLAG_N, (value & FLAG_N) != 0);
        set_flag(FLAG_V, (value & FLAG_V) != 0);
        break;
    }

    case Operation::ADC:
        adc(read_operand(info.mode));
        break;

    case Operation::SBC:
        sbc(read_operand(info.mode));
        break;

    case Operation::CMP:
        compare(state_.a, read_operand(info.mode));
        break;

    case Operation::CPX:
        compare(state_.x, read_operand(info.mode));
        break;

    case Operation::CPY:
        compare(state_.y, read_operand(info.mode));
        break;

    case Operation::INC: {
        const uint16_t address = operand_address(info.mode);
        const uint8_t value = static_cast<uint8_t>(read(address) + 1);
        write(address, value);
        set_zn(value);
        break;
    }

    case Operation::DEC: {
        const uint16_t address = operand_address(info.mode);
        const uint8_t value = static_cast<uint8_t>(read(address) - 1);
        write(address, value);
        set_zn(value);
        break;
    }

    case Operation::ASL:
        asl(info.mode);
        break;

    case Operation::LSR:
        lsr(info.mode);
        break;

    case Operation::ROL:
        rol(info.mode);
        break;

    case Operation::ROR:
        ror(info.mode);
        break;

    case Operation::TAX:
        state_.x = state_.a;
        set_zn(state_.x);
        break;

    case Operation::TAY:
        state_.y = state_.a;
        set_zn(state_.y);
        break;

    case Operation::TXA:
        state_.a = state_.x;
        set_zn(state_.a);
        break;

    case Operation::TYA:
        state_.a = state_.y;
        set_zn(state_.a);
        break;

    case Operation::TSX:
        state_.x = state_.sp;
        set_zn(state_.x);
        break;

    case Operation::TXS:
        state_.sp = state_.x;
        break;

    case Operation::INX:
        state_.x = static_cast<uint8_t>(state_.x + 1);
        set_zn(state_.x);
        break;

    case Operation::INY:
        state_.y = static_cast<uint8_t>(state_.y + 1);
        set_zn(state_.y);
        break;

    case Operation::DEX:
        state_.x = static_cast<uint8_t>(state_.x - 1);
        set_zn(state_.x);
        break;

    case Operation::DEY:
        state_.y = static_cast<uint8_t>(state_.y - 1);
        set_zn(state_.y);
        break;

    case Operation::PHA:
        push(state_.a);
        break;

    case Operation::PHP:
        push(static_cast<uint8_t>(state_.p | FLAG_B | FLAG_U));
        break;

    case Operation::PLA:
        state_.a = pull();
        set_zn(state_.a);
        break;

    case Operation::PLP:
        state_.p = normalized_p(pull());
        break;

    case Operation::CLC:
        set_flag(FLAG_C, false);
        break;

    case Operation::SEC:
        set_flag(FLAG_C, true);
        break;

    case Operation::CLI:
        set_flag(FLAG_I, false);
        break;

    case Operation::SEI:
        set_flag(FLAG_I, true);
        break;

    case Operation::CLV:
        set_flag(FLAG_V, false);
        break;

    case Operation::CLD:
        set_flag(FLAG_D, false);
        break;

    case Operation::SED:
        set_flag(FLAG_D, true);
        break;

    case Operation::JMP:
        state_.pc = operand_address(info.mode);
        break;

    case Operation::JSR: {
        const uint16_t target = fetch16();
        const uint16_t return_address = static_cast<uint16_t>(state_.pc - 1);
        push(static_cast<uint8_t>(return_address >> 8));
        push(static_cast<uint8_t>(return_address & 0x00FF));
        state_.pc = target;
        break;
    }

    case Operation::RTS: {
        const uint8_t low = pull();
        const uint8_t high = pull();
        state_.pc = static_cast<uint16_t>(((high << 8) | low) + 1);
        break;
    }

    case Operation::BRK:
        ++state_.pc;
        interrupt(0xFFFE, true, state_.pc);
        break;

    case Operation::RTI: {
        state_.p = normalized_p(pull());
        const uint8_t low = pull();
        const uint8_t high = pull();
        state_.pc = static_cast<uint16_t>((high << 8) | low);
        break;
    }

    case Operation::NOP:
        break;

    case Operation::BPL:
        branch_if(!flag(FLAG_N));
        break;

    case Operation::BMI:
        branch_if(flag(FLAG_N));
        break;

    case Operation::BVC:
        branch_if(!flag(FLAG_V));
        break;

    case Operation::BVS:
        branch_if(flag(FLAG_V));
        break;

    case Operation::BCC:
        branch_if(!flag(FLAG_C));
        break;

    case Operation::BCS:
        branch_if(flag(FLAG_C));
        break;

    case Operation::BNE:
        branch_if(!flag(FLAG_Z));
        break;

    case Operation::BEQ:
        branch_if(flag(FLAG_Z));
        break;
    }

    return StepResult::Ok;
}

void Cpu6510::set_port_external_inputs(uint8_t value) {
    port_.external_inputs = value;
}

void Cpu6510::set_port_active_mask(uint8_t mask) {
    const uint8_t old_output = port_output();
    port_.active_mask = mask;
    notify_port_if_changed(old_output);
}

uint8_t Cpu6510::port_output() const {
    return static_cast<uint8_t>(port_.data & port_.ddr & port_.active_mask);
}

void Cpu6510::set_port_changed_callback(std::function<void(uint8_t)> callback) {
    port_changed_callback_ = std::move(callback);
}

void Cpu6510::push(uint8_t value) {
    write(static_cast<uint16_t>(0x0100 | state_.sp), value);
    state_.sp = static_cast<uint8_t>(state_.sp - 1);
}

uint8_t Cpu6510::pull() {
    state_.sp = static_cast<uint8_t>(state_.sp + 1);
    return read(static_cast<uint16_t>(0x0100 | state_.sp));
}

uint8_t Cpu6510::read(uint16_t address) {
    if (is_port_address(address)) {
        return read_port(address);
    }
    return bus_.read(address);
}

void Cpu6510::write(uint16_t address, uint8_t value) {
    if (is_port_address(address)) {
        write_port(address, value);
        return;
    }
    bus_.write(address, value);
}

uint8_t Cpu6510::fetch8() {
    return read(state_.pc++);
}

uint16_t Cpu6510::fetch16() {
    const uint8_t low = fetch8();
    const uint8_t high = fetch8();
    return static_cast<uint16_t>((high << 8) | low);
}

uint16_t Cpu6510::read16(uint16_t address) {
    const uint8_t low = read(address);
    const uint8_t high = read(static_cast<uint16_t>(address + 1));
    return static_cast<uint16_t>((high << 8) | low);
}

uint16_t Cpu6510::read16_zp(uint8_t address) {
    const uint8_t low = read(address);
    const uint8_t high = read(static_cast<uint8_t>(address + 1));
    return static_cast<uint16_t>((high << 8) | low);
}

uint16_t Cpu6510::read16_nmos_indirect(uint16_t address) {
    const uint8_t low = read(address);
    const uint16_t high_address = static_cast<uint16_t>((address & 0xFF00) | static_cast<uint8_t>(address + 1));
    const uint8_t high = read(high_address);
    return static_cast<uint16_t>((high << 8) | low);
}

void Cpu6510::set_flag(uint8_t flag_value, bool enabled) {
    if (enabled) {
        state_.p |= flag_value;
    } else {
        state_.p &= static_cast<uint8_t>(~flag_value);
    }
    state_.p |= FLAG_U;
}

bool Cpu6510::flag(uint8_t flag_value) const {
    return (state_.p & flag_value) != 0;
}

void Cpu6510::set_zn(uint8_t value) {
    set_flag(FLAG_Z, value == 0);
    set_flag(FLAG_N, (value & 0x80) != 0);
}

uint8_t Cpu6510::normalized_p(uint8_t value) const {
    return static_cast<uint8_t>((value & ~FLAG_B) | FLAG_U);
}

bool Cpu6510::is_port_address(uint16_t address) const {
    return config_.port_enabled && (address == 0x0000 || address == 0x0001);
}

uint8_t Cpu6510::read_port(uint16_t address) const {
    if (address == 0x0000) {
        return port_.ddr;
    }
    return static_cast<uint8_t>((port_.data & port_.ddr) | (port_.external_inputs & ~port_.ddr));
}

void Cpu6510::write_port(uint16_t address, uint8_t value) {
    const uint8_t old_output = port_output();
    if (address == 0x0000) {
        port_.ddr = value;
    } else {
        port_.data = value;
    }
    notify_port_if_changed(old_output);
}

void Cpu6510::notify_port_if_changed(uint8_t old_output) {
    const uint8_t new_output = port_output();
    if (port_changed_callback_ && new_output != old_output) {
        port_changed_callback_(new_output);
    }
}

uint8_t Cpu6510::read_operand(AddressingMode mode) {
    if (mode == AddressingMode::Accumulator) {
        return state_.a;
    }
    return read(operand_address(mode));
}

void Cpu6510::write_operand(AddressingMode mode, uint8_t value) {
    if (mode == AddressingMode::Accumulator) {
        state_.a = value;
        return;
    }
    write(operand_address(mode), value);
}

void Cpu6510::compare(uint8_t lhs, uint8_t rhs) {
    const uint8_t result = static_cast<uint8_t>(lhs - rhs);
    set_flag(FLAG_C, lhs >= rhs);
    set_zn(result);
}

void Cpu6510::adc(uint8_t value) {
    const uint8_t carry = flag(FLAG_C) ? 1 : 0;
    const uint16_t binary_sum = static_cast<uint16_t>(state_.a + value + carry);
    const uint8_t binary_result = static_cast<uint8_t>(binary_sum);
    set_flag(FLAG_V, ((~(state_.a ^ value) & (state_.a ^ binary_result)) & 0x80) != 0);

    if (flag(FLAG_D)) {
        uint16_t low = static_cast<uint16_t>((state_.a & 0x0F) + (value & 0x0F) + carry);
        uint16_t high = static_cast<uint16_t>((state_.a & 0xF0) + (value & 0xF0));
        if (low > 0x09) {
            high += 0x10;
            low += 0x06;
        }
        if (high > 0x90) {
            high += 0x60;
        }
        set_flag(FLAG_C, high > 0xF0);
        state_.a = static_cast<uint8_t>((high & 0xF0) | (low & 0x0F));
    } else {
        set_flag(FLAG_C, binary_sum > 0xFF);
        state_.a = binary_result;
    }
    set_zn(state_.a);
}

void Cpu6510::sbc(uint8_t value) {
    const uint8_t carry = flag(FLAG_C) ? 1 : 0;
    const uint16_t diff = static_cast<uint16_t>(state_.a - value - (1 - carry));
    const uint8_t binary_result = static_cast<uint8_t>(diff);
    set_flag(FLAG_V, (((state_.a ^ value) & (state_.a ^ binary_result)) & 0x80) != 0);

    if (flag(FLAG_D)) {
        int low = (state_.a & 0x0F) - (value & 0x0F) - (1 - carry);
        int high = (state_.a >> 4) - (value >> 4);
        if (low < 0) {
            low -= 6;
            --high;
        }
        if (high < 0) {
            high -= 6;
        }
        state_.a = static_cast<uint8_t>(((high << 4) & 0xF0) | (low & 0x0F));
        set_flag(FLAG_C, diff < 0x100);
    } else {
        state_.a = binary_result;
        set_flag(FLAG_C, diff < 0x100);
    }
    set_zn(state_.a);
}

void Cpu6510::asl(AddressingMode mode) {
    const bool accumulator = mode == AddressingMode::Accumulator;
    const uint16_t address = accumulator ? 0 : operand_address(mode);
    const uint8_t value = accumulator ? state_.a : read(address);
    const uint8_t result = static_cast<uint8_t>(value << 1);
    set_flag(FLAG_C, (value & 0x80) != 0);
    if (accumulator) {
        state_.a = result;
    } else {
        write(address, result);
    }
    set_zn(result);
}

void Cpu6510::lsr(AddressingMode mode) {
    const bool accumulator = mode == AddressingMode::Accumulator;
    const uint16_t address = accumulator ? 0 : operand_address(mode);
    const uint8_t value = accumulator ? state_.a : read(address);
    const uint8_t result = static_cast<uint8_t>(value >> 1);
    set_flag(FLAG_C, (value & 0x01) != 0);
    if (accumulator) {
        state_.a = result;
    } else {
        write(address, result);
    }
    set_zn(result);
}

void Cpu6510::rol(AddressingMode mode) {
    const bool accumulator = mode == AddressingMode::Accumulator;
    const uint16_t address = accumulator ? 0 : operand_address(mode);
    const uint8_t value = accumulator ? state_.a : read(address);
    const uint8_t result = static_cast<uint8_t>((value << 1) | (flag(FLAG_C) ? 1 : 0));
    set_flag(FLAG_C, (value & 0x80) != 0);
    if (accumulator) {
        state_.a = result;
    } else {
        write(address, result);
    }
    set_zn(result);
}

void Cpu6510::ror(AddressingMode mode) {
    const bool accumulator = mode == AddressingMode::Accumulator;
    const uint16_t address = accumulator ? 0 : operand_address(mode);
    const uint8_t value = accumulator ? state_.a : read(address);
    const uint8_t result = static_cast<uint8_t>((value >> 1) | (flag(FLAG_C) ? 0x80 : 0));
    set_flag(FLAG_C, (value & 0x01) != 0);
    if (accumulator) {
        state_.a = result;
    } else {
        write(address, result);
    }
    set_zn(result);
}

void Cpu6510::branch_if(bool condition) {
    const int8_t offset = static_cast<int8_t>(fetch8());
    if (condition) {
        state_.pc = static_cast<uint16_t>(state_.pc + offset);
    }
}

void Cpu6510::interrupt(uint16_t vector, bool break_flag, uint16_t return_pc) {
    push(static_cast<uint8_t>(return_pc >> 8));
    push(static_cast<uint8_t>(return_pc & 0x00FF));
    push(static_cast<uint8_t>((state_.p | FLAG_U) | (break_flag ? FLAG_B : 0)));
    set_flag(FLAG_I, true);
    state_.pc = read16(vector);
}

uint16_t Cpu6510::operand_address(AddressingMode mode) {
    switch (mode) {
    case AddressingMode::Immediate:
        return state_.pc++;
    case AddressingMode::ZeroPage:
        return fetch8();
    case AddressingMode::ZeroPageX:
        return static_cast<uint8_t>(fetch8() + state_.x);
    case AddressingMode::ZeroPageY:
        return static_cast<uint8_t>(fetch8() + state_.y);
    case AddressingMode::Absolute:
        return fetch16();
    case AddressingMode::AbsoluteX:
        return static_cast<uint16_t>(fetch16() + state_.x);
    case AddressingMode::AbsoluteY:
        return static_cast<uint16_t>(fetch16() + state_.y);
    case AddressingMode::Indirect:
        return read16_nmos_indirect(fetch16());
    case AddressingMode::IndexedIndirect:
        return read16_zp(static_cast<uint8_t>(fetch8() + state_.x));
    case AddressingMode::IndirectIndexed:
        return static_cast<uint16_t>(read16_zp(fetch8()) + state_.y);
    case AddressingMode::Relative:
        return state_.pc++;
    case AddressingMode::Implied:
    case AddressingMode::Accumulator:
        return 0;
    }
    return 0;
}

} // namespace j6510
