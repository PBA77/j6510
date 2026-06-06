#include "cpu6510_core.h"

#include <utility>

namespace j6510 {

Cpu6510::Cpu6510(Bus& bus) : Cpu6510(bus, Cpu6510Config{}) {}

Cpu6510::Cpu6510(Bus& bus, Cpu6510Config config) : bus_(bus), direct_memory_(bus.direct_memory()), config_(config) {}

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
    if (interrupt_poll_callback_ || interrupts_.reset_pending || interrupts_.nmi_pending || interrupts_.irq_level) {
        poll_target_interrupts();
        service_pending_interrupt_if_needed();
    }

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

RunResult Cpu6510::run(uint32_t max_instructions) {
    RunResult result{};
    const bool fast_path = can_use_fast_run_path();
    for (uint32_t i = 0; i < max_instructions; ++i) {
        StepResult step_result = StepResult::Ok;
        if (!fast_path || !run_fast_instruction(step_result)) {
            step_result = step();
        }
        if (step_result != StepResult::Ok) {
            result.result = step_result;
            result.instructions_executed = static_cast<uint32_t>(i + 1);
            result.stop_pc = state_.pc;
            return result;
        }
    }
    result.instructions_executed = max_instructions;
    result.stop_pc = state_.pc;
    return result;
}

RunResult Cpu6510::run_cached(uint32_t max_instructions) {
    RunResult result{};
    while (result.instructions_executed < max_instructions) {
        if (has_pending_interrupt_work()) {
            result.stop_pc = state_.pc;
            return result;
        }

        CachedBlock& slot = block_cache_slot(state_.pc);
        if (!slot.valid || slot.start_pc != state_.pc) {
            if (slot.valid) {
                remove_block_from_page_counts(slot);
                --valid_cached_blocks_;
            }
            slot = decode_block(state_.pc);
            add_block_to_page_counts(slot);
            ++valid_cached_blocks_;
            ++block_cache_stats_.misses;
        } else {
            ++block_cache_stats_.hits;
        }

        if (!execute_cached_block(slot, max_instructions - result.instructions_executed, result)) {
            return result;
        }
    }
    result.stop_pc = state_.pc;
    return result;
}

const BlockCacheStats& Cpu6510::block_cache_stats() const {
    return block_cache_stats_;
}

void Cpu6510::clear_block_cache() {
    for (auto& block : block_cache_) {
        block.valid = false;
    }
    cached_page_use_count_.fill(0);
    valid_cached_blocks_ = 0;
}

BlockRunResult Cpu6510::run_block(uint32_t max_instructions) {
    BlockRunResult result{};
    result.start_pc = state_.pc;

    for (uint32_t i = 0; i < max_instructions; ++i) {
        if (has_pending_interrupt_work()) {
            result.stop_reason = RunStopReason::InterruptPending;
            result.instructions_executed = i;
            result.stop_pc = state_.pc;
            return result;
        }

        const uint16_t pc_before = state_.pc;
        const uint8_t opcode = read(pc_before);
        StepResult step_result = StepResult::Ok;
        if (!can_use_fast_run_path() || !run_fast_instruction(step_result)) {
            step_result = step();
        }

        result.instructions_executed = static_cast<uint32_t>(i + 1);
        result.stop_pc = state_.pc;

        if (step_result != StepResult::Ok) {
            result.result = step_result;
            result.stop_reason = RunStopReason::IllegalOpcode;
            return result;
        }

        if (is_block_terminator(opcode)) {
            result.stop_reason = RunStopReason::ControlFlow;
            return result;
        }
    }

    result.stop_reason = RunStopReason::BudgetExhausted;
    result.stop_pc = state_.pc;
    return result;
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
    if (direct_memory_ && !config_.port_enabled) {
        return direct_memory_[address];
    }
    if (is_port_address(address)) {
        return read_port(address);
    }
    if (direct_memory_) {
        return direct_memory_[address];
    }
    return bus_.read(address);
}

void Cpu6510::write(uint16_t address, uint8_t value) {
    if (direct_memory_ && !config_.port_enabled) {
        direct_write(address, value);
        return;
    }
    if (is_port_address(address)) {
        write_port(address, value);
        return;
    }
    if (direct_memory_) {
        direct_write(address, value);
        return;
    }
    bus_.write(address, value);
    invalidate_block_cache_for_write(address);
}

void Cpu6510::invalidate_block_cache_for_write(uint16_t address) {
    if (valid_cached_blocks_ == 0) {
        return;
    }

    const uint8_t page = static_cast<uint8_t>(address >> 8);
    if (cached_page_use_count_[page] == 0) {
        return;
    }

    bool invalidated = false;
    for (auto& block : block_cache_) {
        if (block.valid && block_uses_page(block, page)) {
            remove_block_from_page_counts(block);
            block.valid = false;
            --valid_cached_blocks_;
            invalidated = true;
        }
    }
    if (invalidated) {
        ++block_cache_stats_.invalidations;
    }
}

void Cpu6510::add_block_to_page_counts(const CachedBlock& block) {
    if (!block.valid) {
        return;
    }
    uint8_t page = block.page_start;
    while (true) {
        ++cached_page_use_count_[page];
        if (page == block.page_end) {
            break;
        }
        ++page;
    }
}

void Cpu6510::remove_block_from_page_counts(const CachedBlock& block) {
    if (!block.valid) {
        return;
    }
    uint8_t page = block.page_start;
    while (true) {
        if (cached_page_use_count_[page] > 0) {
            --cached_page_use_count_[page];
        }
        if (page == block.page_end) {
            break;
        }
        ++page;
    }
}

bool Cpu6510::block_uses_page(const CachedBlock& block, uint8_t page) const {
    if (block.page_start <= block.page_end) {
        return page >= block.page_start && page <= block.page_end;
    }
    return page >= block.page_start || page <= block.page_end;
}

Cpu6510::CachedBlock& Cpu6510::block_cache_slot(uint16_t pc) {
    return block_cache_[pc & 0x00FF];
}

Cpu6510::CachedBlock Cpu6510::decode_block(uint16_t pc) {
    CachedBlock block{};
    block.valid = true;
    block.executable = true;
    block.start_pc = pc;
    block.page_start = static_cast<uint8_t>(pc >> 8);
    const auto disable_ir_for_unsafe_writes = [this, &block]() {
        for (uint8_t i = 0; i < block.count; ++i) {
            const CachedOpKind kind = block.ops[i].kind;
            if ((kind == CachedOpKind::StaAbs || kind == CachedOpKind::StxAbs ||
                 kind == CachedOpKind::StaZp || kind == CachedOpKind::StaZpX ||
                 kind == CachedOpKind::StaAbsY) &&
                !cached_write_is_safe_for_block(block, block.ops[i])) {
                block.executable = false;
                return;
            }
        }
    };

    uint16_t cursor = pc;
    while (block.count < 32) {
        const uint8_t opcode = read(cursor);
        const OpcodeInfo& info = opcode_info(opcode);
        uint16_t operand = 0;
        if (info.bytes == 2) {
            operand = read(static_cast<uint16_t>(cursor + 1));
        } else if (info.bytes == 3) {
            const uint8_t low = read(static_cast<uint16_t>(cursor + 1));
            const uint8_t high = read(static_cast<uint16_t>(cursor + 2));
            operand = static_cast<uint16_t>((high << 8) | low);
        }

        block.opcodes[block.count] = opcode;
        block.lengths[block.count] = info.bytes;
        CachedOp& op = block.ops[block.count];
        if (!decode_cached_op(opcode, operand, op)) {
            block.executable = false;
        }

        if (info.operation == Operation::Illegal) {
            block.page_end = static_cast<uint8_t>(cursor >> 8);
            block.terminator = RunStopReason::IllegalOpcode;
            block.executable = false;
            ++block.count;
            return block;
        }
        if (is_block_terminator(opcode)) {
            const uint16_t last_byte = static_cast<uint16_t>(cursor + info.bytes - 1);
            block.page_end = static_cast<uint8_t>(last_byte >> 8);
            block.terminator = RunStopReason::ControlFlow;
            ++block.count;
            disable_ir_for_unsafe_writes();
            return block;
        }

        cursor = static_cast<uint16_t>(cursor + info.bytes);
        ++block.count;
    }

    block.page_end = static_cast<uint8_t>(static_cast<uint16_t>(cursor - 1) >> 8);
    block.terminator = RunStopReason::BudgetExhausted;
    disable_ir_for_unsafe_writes();
    return block;
}

bool Cpu6510::execute_cached_block(const CachedBlock& block, uint32_t remaining_budget, RunResult& result) {
    const uint32_t to_execute = block.count < remaining_budget ? block.count : remaining_budget;
    if (block.executable && can_use_fast_run_path()) {
        if (can_use_direct_memory_path()) {
            execute_cached_block_direct(block, to_execute, result);
        } else {
            for (uint32_t i = 0; i < to_execute; ++i) {
                execute_cached_op(block.ops[i]);
                ++result.instructions_executed;
                result.stop_pc = state_.pc;
            }
        }
        return true;
    }

    for (uint32_t i = 0; i < to_execute; ++i) {
        StepResult step_result = StepResult::Ok;
        const uint16_t pc_before = state_.pc;
        if (!can_use_fast_run_path() || !run_fast_instruction(step_result)) {
            step_result = step();
        }
        ++result.instructions_executed;
        result.stop_pc = state_.pc;

        if (step_result != StepResult::Ok) {
            result.result = step_result;
            return false;
        }

        if (block.lengths[i] != 0 && !is_block_terminator(block.opcodes[i])) {
            const uint16_t expected_pc = static_cast<uint16_t>(pc_before + block.lengths[i]);
            if (state_.pc != expected_pc) {
                return true;
            }
        }
    }
    return true;
}

bool Cpu6510::decode_cached_op(uint8_t opcode, uint16_t operand, CachedOp& op) const {
    switch (opcode) {
    case 0x4C:
        op = CachedOp{CachedOpKind::JmpAbs, operand};
        return true;
    case 0x8D:
        op = CachedOp{CachedOpKind::StaAbs, operand};
        return true;
    case 0x8E:
        op = CachedOp{CachedOpKind::StxAbs, operand};
        return true;
    case 0x95:
        op = CachedOp{CachedOpKind::StaZpX, operand};
        return true;
    case 0x99:
        op = CachedOp{CachedOpKind::StaAbsY, operand};
        return true;
    case 0xA0:
        op = CachedOp{CachedOpKind::LdyImm, operand};
        return true;
    case 0xA2:
        op = CachedOp{CachedOpKind::LdxImm, operand};
        return true;
    case 0x85:
        op = CachedOp{CachedOpKind::StaZp, operand};
        return true;
    case 0x88:
        op = CachedOp{CachedOpKind::Dey, 0};
        return true;
    case 0x8A:
        op = CachedOp{CachedOpKind::Txa, 0};
        return true;
    case 0xA9:
        op = CachedOp{CachedOpKind::LdaImm, operand};
        return true;
    case 0xAA:
        op = CachedOp{CachedOpKind::Tax, 0};
        return true;
    case 0xAD:
        op = CachedOp{CachedOpKind::LdaAbs, operand};
        return true;
    case 0xAE:
        op = CachedOp{CachedOpKind::LdxAbs, operand};
        return true;
    case 0xB5:
        op = CachedOp{CachedOpKind::LdaZpX, operand};
        return true;
    case 0xB9:
        op = CachedOp{CachedOpKind::LdaAbsY, operand};
        return true;
    case 0xCA:
        op = CachedOp{CachedOpKind::Dex, 0};
        return true;
    case 0xD0:
        op = CachedOp{CachedOpKind::Bne, operand};
        return true;
    case 0xE8:
        op = CachedOp{CachedOpKind::Inx, 0};
        return true;
    case 0xEA:
        op = CachedOp{CachedOpKind::Nop, 0};
        return true;
    default:
        op = CachedOp{CachedOpKind::Fallback, 0};
        return false;
    }
}

bool Cpu6510::cached_write_is_safe_for_block(const CachedBlock& block, const CachedOp& op) const {
    switch (op.kind) {
    case CachedOpKind::StaAbs:
    case CachedOpKind::StxAbs:
        return !block_uses_page(block, static_cast<uint8_t>(op.operand >> 8));
    case CachedOpKind::StaZp:
    case CachedOpKind::StaZpX:
        return !block_uses_page(block, 0x00);
    case CachedOpKind::StaAbsY: {
        const uint8_t first_page = static_cast<uint8_t>(op.operand >> 8);
        const uint8_t second_page = static_cast<uint8_t>((op.operand + 0xFF) >> 8);
        return !block_uses_page(block, first_page) && !block_uses_page(block, second_page);
    }
    default:
        return true;
    }
}

bool Cpu6510::can_use_direct_memory_path() const {
    return direct_memory_ && !config_.port_enabled;
}

void Cpu6510::direct_write(uint16_t address, uint8_t value) {
    direct_memory_[address] = value;
    const uint8_t page = static_cast<uint8_t>(address >> 8);
    if (valid_cached_blocks_ != 0 && cached_page_use_count_[page] != 0) {
        invalidate_block_cache_for_write(address);
    }
}

void Cpu6510::execute_cached_op(const CachedOp& op) {
    switch (op.kind) {
    case CachedOpKind::LdaImm:
        state_.a = static_cast<uint8_t>(op.operand);
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        set_zn(state_.a);
        return;
    case CachedOpKind::LdxImm:
        state_.x = static_cast<uint8_t>(op.operand);
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        set_zn(state_.x);
        return;
    case CachedOpKind::LdyImm:
        state_.y = static_cast<uint8_t>(op.operand);
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        set_zn(state_.y);
        return;
    case CachedOpKind::LdaAbs:
        state_.a = read(op.operand);
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        set_zn(state_.a);
        return;
    case CachedOpKind::LdxAbs:
        state_.x = read(op.operand);
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        set_zn(state_.x);
        return;
    case CachedOpKind::LdaZpX:
        state_.a = read(static_cast<uint8_t>(op.operand + state_.x));
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        set_zn(state_.a);
        return;
    case CachedOpKind::LdaAbsY:
        state_.a = read(static_cast<uint16_t>(op.operand + state_.y));
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        set_zn(state_.a);
        return;
    case CachedOpKind::StaAbs:
        write(op.operand, state_.a);
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        return;
    case CachedOpKind::StxAbs:
        write(op.operand, state_.x);
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        return;
    case CachedOpKind::StaZp:
        write(static_cast<uint8_t>(op.operand), state_.a);
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        return;
    case CachedOpKind::StaZpX:
        write(static_cast<uint8_t>(op.operand + state_.x), state_.a);
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        return;
    case CachedOpKind::StaAbsY:
        write(static_cast<uint16_t>(op.operand + state_.y), state_.a);
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        return;
    case CachedOpKind::Tax:
        state_.x = state_.a;
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.x);
        return;
    case CachedOpKind::Txa:
        state_.a = state_.x;
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.a);
        return;
    case CachedOpKind::Inx:
        state_.x = static_cast<uint8_t>(state_.x + 1);
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.x);
        return;
    case CachedOpKind::Dex:
        state_.x = static_cast<uint8_t>(state_.x - 1);
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.x);
        return;
    case CachedOpKind::Dey:
        state_.y = static_cast<uint8_t>(state_.y - 1);
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.y);
        return;
    case CachedOpKind::Nop:
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        return;
    case CachedOpKind::Bne:
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        if ((state_.p & FLAG_Z) == 0) {
            state_.pc = static_cast<uint16_t>(state_.pc + static_cast<int8_t>(op.operand));
        }
        return;
    case CachedOpKind::JmpAbs:
        state_.pc = op.operand;
        return;
    case CachedOpKind::Fallback:
        return;
    }
}

void Cpu6510::execute_cached_block_direct(const CachedBlock& block, uint32_t to_execute, RunResult& result) {
    uint8_t* memory = direct_memory_;
    uint8_t a = state_.a;
    uint8_t x = state_.x;
    uint8_t y = state_.y;
    uint8_t p = state_.p;
    uint16_t pc = state_.pc;

    const auto set_zn_local = [&p](uint8_t value) {
        p = static_cast<uint8_t>((p & ~(FLAG_Z | FLAG_N)) | FLAG_U);
        if (value == 0) {
            p |= FLAG_Z;
        }
        p |= static_cast<uint8_t>(value & FLAG_N);
    };
    const auto write_direct = [this, memory](uint16_t address, uint8_t value) {
        memory[address] = value;
        const uint8_t page = static_cast<uint8_t>(address >> 8);
        if (valid_cached_blocks_ != 0 && cached_page_use_count_[page] != 0) {
            invalidate_block_cache_for_write(address);
        }
    };

    for (uint32_t i = 0; i < to_execute; ++i) {
        const CachedOp& op = block.ops[i];
        switch (op.kind) {
        case CachedOpKind::LdaImm:
            a = static_cast<uint8_t>(op.operand);
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(a);
            break;
        case CachedOpKind::LdxImm:
            x = static_cast<uint8_t>(op.operand);
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(x);
            break;
        case CachedOpKind::LdyImm:
            y = static_cast<uint8_t>(op.operand);
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(y);
            break;
        case CachedOpKind::LdaAbs:
            a = memory[op.operand];
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(a);
            break;
        case CachedOpKind::LdxAbs:
            x = memory[op.operand];
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(x);
            break;
        case CachedOpKind::LdaZpX:
            a = memory[static_cast<uint8_t>(op.operand + x)];
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(a);
            break;
        case CachedOpKind::LdaAbsY:
            a = memory[static_cast<uint16_t>(op.operand + y)];
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(a);
            break;
        case CachedOpKind::StaAbs:
            write_direct(op.operand, a);
            pc = static_cast<uint16_t>(pc + 3);
            break;
        case CachedOpKind::StxAbs:
            write_direct(op.operand, x);
            pc = static_cast<uint16_t>(pc + 3);
            break;
        case CachedOpKind::StaZp:
            write_direct(static_cast<uint8_t>(op.operand), a);
            pc = static_cast<uint16_t>(pc + 2);
            break;
        case CachedOpKind::StaZpX:
            write_direct(static_cast<uint8_t>(op.operand + x), a);
            pc = static_cast<uint16_t>(pc + 2);
            break;
        case CachedOpKind::StaAbsY:
            write_direct(static_cast<uint16_t>(op.operand + y), a);
            pc = static_cast<uint16_t>(pc + 3);
            break;
        case CachedOpKind::Tax:
            x = a;
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(x);
            break;
        case CachedOpKind::Txa:
            a = x;
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(a);
            break;
        case CachedOpKind::Inx:
            x = static_cast<uint8_t>(x + 1);
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(x);
            break;
        case CachedOpKind::Dex:
            x = static_cast<uint8_t>(x - 1);
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(x);
            break;
        case CachedOpKind::Dey:
            y = static_cast<uint8_t>(y - 1);
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(y);
            break;
        case CachedOpKind::Nop:
            pc = static_cast<uint16_t>(pc + 1);
            break;
        case CachedOpKind::Bne:
            pc = static_cast<uint16_t>(pc + 2);
            if ((p & FLAG_Z) == 0) {
                pc = static_cast<uint16_t>(pc + static_cast<int8_t>(op.operand));
            }
            break;
        case CachedOpKind::JmpAbs:
            pc = op.operand;
            break;
        case CachedOpKind::Fallback:
            break;
        }
    }

    state_.a = a;
    state_.x = x;
    state_.y = y;
    state_.p = static_cast<uint8_t>(p | FLAG_U);
    state_.pc = pc;
    result.instructions_executed += to_execute;
    result.stop_pc = pc;
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
    state_.p = static_cast<uint8_t>((state_.p & ~(FLAG_Z | FLAG_N)) | FLAG_U);
    if (value == 0) {
        state_.p |= FLAG_Z;
    }
    state_.p |= static_cast<uint8_t>(value & FLAG_N);
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

bool Cpu6510::can_use_fast_run_path() const {
    return !has_pending_interrupt_work();
}

bool Cpu6510::run_fast_instruction(StepResult& result) {
    const uint16_t instruction_pc = state_.pc;
    const uint8_t opcode = fast_fetch8();

    switch (opcode) {
    case 0x10:
        fast_branch_if((state_.p & FLAG_N) == 0);
        return true;

    case 0x18:
        state_.p = static_cast<uint8_t>((state_.p & ~FLAG_C) | FLAG_U);
        return true;

    case 0x30:
        fast_branch_if((state_.p & FLAG_N) != 0);
        return true;

    case 0x38:
        state_.p = static_cast<uint8_t>(state_.p | FLAG_C | FLAG_U);
        return true;

    case 0x4C:
        state_.pc = fast_fetch16();
        return true;

    case 0x50:
        fast_branch_if((state_.p & FLAG_V) == 0);
        return true;

    case 0x58:
        state_.p = static_cast<uint8_t>((state_.p & ~FLAG_I) | FLAG_U);
        return true;

    case 0x70:
        fast_branch_if((state_.p & FLAG_V) != 0);
        return true;

    case 0x78:
        state_.p = static_cast<uint8_t>(state_.p | FLAG_I | FLAG_U);
        return true;

    case 0x84:
        write(fast_fetch8(), state_.y);
        return true;

    case 0x85:
        write(fast_fetch8(), state_.a);
        return true;

    case 0x86:
        write(fast_fetch8(), state_.x);
        return true;

    case 0x88:
        state_.y = static_cast<uint8_t>(state_.y - 1);
        set_zn(state_.y);
        return true;

    case 0x8A:
        state_.a = state_.x;
        set_zn(state_.a);
        return true;

    case 0x8D:
        write(fast_fetch16(), state_.a);
        return true;

    case 0x8E:
        write(fast_fetch16(), state_.x);
        return true;

    case 0x90:
        fast_branch_if((state_.p & FLAG_C) == 0);
        return true;

    case 0x94:
        write(fast_zp_x(), state_.y);
        return true;

    case 0x95:
        write(fast_zp_x(), state_.a);
        return true;

    case 0x96:
        write(fast_zp_y(), state_.x);
        return true;

    case 0x98:
        state_.a = state_.y;
        set_zn(state_.a);
        return true;

    case 0x99:
        write(fast_abs_y(), state_.a);
        return true;

    case 0x9A:
        state_.sp = state_.x;
        return true;

    case 0x9D:
        write(fast_abs_x(), state_.a);
        return true;

    case 0xA0:
        state_.y = fast_fetch8();
        set_zn(state_.y);
        return true;

    case 0xA1:
        state_.a = read(fast_indexed_indirect());
        set_zn(state_.a);
        return true;

    case 0xA2:
        state_.x = fast_fetch8();
        set_zn(state_.x);
        return true;

    case 0xA4:
        state_.y = read(fast_fetch8());
        set_zn(state_.y);
        return true;

    case 0xA5:
        state_.a = read(fast_fetch8());
        set_zn(state_.a);
        return true;

    case 0xA6:
        state_.x = read(fast_fetch8());
        set_zn(state_.x);
        return true;

    case 0xA8:
        state_.y = state_.a;
        set_zn(state_.y);
        return true;

    case 0xA9:
        state_.a = fast_fetch8();
        set_zn(state_.a);
        return true;

    case 0xAA:
        state_.x = state_.a;
        set_zn(state_.x);
        return true;

    case 0xAD:
        state_.a = read(fast_fetch16());
        set_zn(state_.a);
        return true;

    case 0xAE:
        state_.x = read(fast_fetch16());
        set_zn(state_.x);
        return true;

    case 0xB0:
        fast_branch_if((state_.p & FLAG_C) != 0);
        return true;

    case 0xB1:
        state_.a = read(fast_indirect_indexed());
        set_zn(state_.a);
        return true;

    case 0xB4:
        state_.y = read(fast_zp_x());
        set_zn(state_.y);
        return true;

    case 0xB5:
        state_.a = read(fast_zp_x());
        set_zn(state_.a);
        return true;

    case 0xB6:
        state_.x = read(fast_zp_y());
        set_zn(state_.x);
        return true;

    case 0xB8:
        state_.p = static_cast<uint8_t>((state_.p & ~FLAG_V) | FLAG_U);
        return true;

    case 0xB9:
        state_.a = read(fast_abs_y());
        set_zn(state_.a);
        return true;

    case 0xBA:
        state_.x = state_.sp;
        set_zn(state_.x);
        return true;

    case 0xBC:
        state_.y = read(fast_abs_x());
        set_zn(state_.y);
        return true;

    case 0xBD:
        state_.a = read(fast_abs_x());
        set_zn(state_.a);
        return true;

    case 0xBE:
        state_.x = read(fast_abs_y());
        set_zn(state_.x);
        return true;

    case 0xCA:
        state_.x = static_cast<uint8_t>(state_.x - 1);
        set_zn(state_.x);
        return true;

    case 0xC8:
        state_.y = static_cast<uint8_t>(state_.y + 1);
        set_zn(state_.y);
        return true;

    case 0xD0:
        fast_branch_if((state_.p & FLAG_Z) == 0);
        return true;

    case 0xD8:
        state_.p = static_cast<uint8_t>((state_.p & ~FLAG_D) | FLAG_U);
        return true;

    case 0xE8:
        state_.x = static_cast<uint8_t>(state_.x + 1);
        set_zn(state_.x);
        return true;

    case 0xF0:
        fast_branch_if((state_.p & FLAG_Z) != 0);
        return true;

    case 0xF8:
        state_.p = static_cast<uint8_t>(state_.p | FLAG_D | FLAG_U);
        return true;

    default:
        state_.pc = instruction_pc;
        result = StepResult::Ok;
        return false;
    }
}

bool Cpu6510::is_block_terminator(uint8_t opcode) const {
    switch (opcode) {
    case 0x00: // BRK
    case 0x10: // BPL
    case 0x20: // JSR
    case 0x30: // BMI
    case 0x40: // RTI
    case 0x4C: // JMP abs
    case 0x50: // BVC
    case 0x60: // RTS
    case 0x6C: // JMP indirect
    case 0x70: // BVS
    case 0x90: // BCC
    case 0xB0: // BCS
    case 0xD0: // BNE
    case 0xF0: // BEQ
        return true;
    default:
        return false;
    }
}

bool Cpu6510::has_pending_interrupt_work() const {
    return interrupt_poll_callback_ || interrupts_.reset_pending || interrupts_.nmi_pending || interrupts_.irq_level;
}

uint8_t Cpu6510::fast_fetch8() {
    return read(state_.pc++);
}

uint16_t Cpu6510::fast_fetch16() {
    const uint8_t low = fast_fetch8();
    const uint8_t high = fast_fetch8();
    return static_cast<uint16_t>((high << 8) | low);
}

uint16_t Cpu6510::fast_zp_x() {
    return static_cast<uint8_t>(fast_fetch8() + state_.x);
}

uint16_t Cpu6510::fast_zp_y() {
    return static_cast<uint8_t>(fast_fetch8() + state_.y);
}

uint16_t Cpu6510::fast_abs_x() {
    return static_cast<uint16_t>(fast_fetch16() + state_.x);
}

uint16_t Cpu6510::fast_abs_y() {
    return static_cast<uint16_t>(fast_fetch16() + state_.y);
}

uint16_t Cpu6510::fast_indexed_indirect() {
    return read16_zp(static_cast<uint8_t>(fast_fetch8() + state_.x));
}

uint16_t Cpu6510::fast_indirect_indexed() {
    return static_cast<uint16_t>(read16_zp(fast_fetch8()) + state_.y);
}

void Cpu6510::fast_branch_if(bool condition) {
    const int8_t offset = static_cast<int8_t>(fast_fetch8());
    if (condition) {
        state_.pc = static_cast<uint16_t>(state_.pc + offset);
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
