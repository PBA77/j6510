#include "cpu6510_core.h"

#include <utility>

namespace j6510 {

namespace {

#if J6510_ENABLE_BLOCK_CACHE

#if J6510_ENABLE_CACHE_STATS
#define J6510_CACHE_STATS(statement)                                                                                   \
    do {                                                                                                               \
        statement;                                                                                                     \
    } while (false)
#else
#define J6510_CACHE_STATS(statement)                                                                                   \
    do {                                                                                                               \
    } while (false)
#endif

uint16_t make_branch_operand(uint16_t operand, uint8_t flag) {
    return static_cast<uint16_t>((operand << 8) | flag);
}

uint8_t branch_flag(uint16_t operand) {
    return static_cast<uint8_t>(operand);
}

int8_t branch_offset(uint16_t operand) {
    return static_cast<int8_t>(operand >> 8);
}

uint8_t with_flag(uint8_t p, uint8_t flag, bool enabled) {
    if (enabled) {
        return static_cast<uint8_t>(p | flag | FLAG_U);
    }
    return static_cast<uint8_t>((p & ~flag) | FLAG_U);
}

uint8_t with_zn(uint8_t p, uint8_t value) {
    p = static_cast<uint8_t>((p & ~(FLAG_Z | FLAG_N)) | FLAG_U);
    if (value == 0) {
        p |= FLAG_Z;
    }
    return static_cast<uint8_t>(p | (value & FLAG_N));
}

uint8_t adc_result(uint8_t a, uint8_t value, uint8_t& p) {
    const uint8_t carry = (p & FLAG_C) != 0 ? 1 : 0;
    const uint16_t binary_sum = static_cast<uint16_t>(a + value + carry);
    const uint8_t binary_result = static_cast<uint8_t>(binary_sum);
    p = with_flag(p, FLAG_V, ((~(a ^ value) & (a ^ binary_result)) & 0x80) != 0);

    uint8_t result = binary_result;
    if ((p & FLAG_D) != 0) {
        uint16_t low = static_cast<uint16_t>((a & 0x0F) + (value & 0x0F) + carry);
        uint16_t high = static_cast<uint16_t>((a & 0xF0) + (value & 0xF0));
        if (low > 0x09) {
            high += 0x10;
            low += 0x06;
        }
        if (high > 0x90) {
            high += 0x60;
        }
        p = with_flag(p, FLAG_C, high > 0xF0);
        result = static_cast<uint8_t>((high & 0xF0) | (low & 0x0F));
    } else {
        p = with_flag(p, FLAG_C, binary_sum > 0xFF);
    }
    p = with_zn(p, result);
    return result;
}

uint8_t compare_flags(uint8_t p, uint8_t lhs, uint8_t rhs) {
    const uint8_t result = static_cast<uint8_t>(lhs - rhs);
    p = with_flag(p, FLAG_C, lhs >= rhs);
    return with_zn(p, result);
}

#endif

#ifndef J6510_CACHE_STATS
#define J6510_CACHE_STATS(statement)                                                                                   \
    do {                                                                                                               \
    } while (false)
#endif

} // namespace

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
    pending_exact_cycles_ = 0;
    exact_instruction_completed_ = false;
    staged_exact_state_valid_ = false;
    staged_exact_result_ = StepResult::Ok;
    state_.sp = 0xFD;
    state_.p = FLAG_U | FLAG_I;
    state_.pc = read16(0xFFFC);
}

StepResult Cpu6510::step() {
    if (J6510_UNLIKELY(config_.execution_mode == ExecutionMode::CycleExact)) {
        StepResult result = StepResult::Ok;
        do {
            result = tick();
        } while (result == StepResult::Ok && !exact_instruction_completed_);
        return result;
    }

    if (interrupt_poll_callback_ || interrupts_.reset_pending || interrupts_.nmi_pending || interrupts_.irq_level) {
        poll_target_interrupts();
        service_pending_interrupt_if_needed();
    }

    const uint16_t instruction_pc = state_.pc;
    const uint8_t opcode = fetch8();
    const OpcodeInfo& info = active_opcode_info(opcode);

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
        if (info.mode != AddressingMode::Implied) {
            (void)read_operand(info.mode);
        }
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

    case Operation::SLO:
        slo(info.mode);
        break;

    case Operation::RLA:
        rla(info.mode);
        break;

    case Operation::SRE:
        sre(info.mode);
        break;

    case Operation::RRA:
        rra(info.mode);
        break;

    case Operation::SAX:
        sax(info.mode);
        break;

    case Operation::LAX:
        lax(info.mode);
        break;

    case Operation::DCP:
        dcp(info.mode);
        break;

    case Operation::ISC:
        isc(info.mode);
        break;
    }

    return StepResult::Ok;
}

StepResult Cpu6510::tick() {
    exact_instruction_completed_ = false;
    if (pending_exact_cycles_ > 0) {
        --pending_exact_cycles_;
        ++cycle_count_;
        exact_instruction_completed_ = pending_exact_cycles_ == 0;
        if (exact_instruction_completed_ && staged_exact_state_valid_) {
            state_ = staged_exact_state_;
            staged_exact_state_valid_ = false;
            return staged_exact_result_;
        }
        return StepResult::Ok;
    }

    if (config_.execution_mode != ExecutionMode::CycleExact) {
        const StepResult result = step();
        ++cycle_count_;
        exact_instruction_completed_ = true;
        return result;
    }

    uint8_t cycles = 0;
    const Cpu6510State state_before = state_;
    const StepResult result = execute_cycle_exact_instruction(cycles);
    if (cycles == 0) {
        cycles = 1;
    }
    if (cycles > 1 && result == StepResult::Ok) {
        staged_exact_state_ = state_;
        staged_exact_state_valid_ = true;
        staged_exact_result_ = result;
        state_ = state_before;
    }
    pending_exact_cycles_ = static_cast<uint8_t>(cycles - 1);
    ++cycle_count_;
    exact_instruction_completed_ = pending_exact_cycles_ == 0;
    return result;
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

CycleRunResult Cpu6510::run_cycles(uint32_t max_cycles) {
    CycleRunResult result{};
    for (uint32_t i = 0; i < max_cycles; ++i) {
        const StepResult tick_result = tick();
        result.cycles_executed = static_cast<uint32_t>(i + 1);
        result.stop_pc = state_.pc;
        if (exact_instruction_completed_) {
            ++result.instructions_completed;
        }
        if (tick_result != StepResult::Ok) {
            result.result = tick_result;
            return result;
        }
    }
    result.stop_pc = state_.pc;
    return result;
}

J6510_FAST_CODE_ATTR RunResult Cpu6510::run_cached(uint32_t max_instructions) {
#if J6510_ENABLE_BLOCK_CACHE
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
            J6510_CACHE_STATS(++block_cache_stats_.misses);
        } else {
            J6510_CACHE_STATS(++block_cache_stats_.hits);
        }

        if (!execute_cached_block(slot, max_instructions - result.instructions_executed, result)) {
            return result;
        }
    }
    result.stop_pc = state_.pc;
    return result;
#else
    return run(max_instructions);
#endif
}

const BlockCacheStats& Cpu6510::block_cache_stats() const {
#if J6510_ENABLE_BLOCK_CACHE && J6510_ENABLE_CACHE_STATS
    return block_cache_stats_;
#else
    static const BlockCacheStats empty_stats{};
    return empty_stats;
#endif
}

void Cpu6510::clear_block_cache() {
#if J6510_ENABLE_BLOCK_CACHE
    for (auto& block : block_cache_) {
        block.valid = false;
    }
    cached_page_use_count_.fill(0);
    valid_cached_blocks_ = 0;
#endif
}

uint64_t Cpu6510::cycle() const {
    return cycle_count_;
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
#if J6510_ENABLE_BLOCK_CACHE
    invalidate_block_cache_for_write(address);
#endif
}

#if J6510_ENABLE_BLOCK_CACHE
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
        J6510_CACHE_STATS(++block_cache_stats_.invalidations);
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
    return block_cache_[pc % J6510_BLOCK_CACHE_SLOTS];
}

Cpu6510::CachedBlock Cpu6510::decode_block(uint16_t pc) {
    CachedBlock block{};
    block.valid = true;
    block.executable = true;
    block.hot_executable = true;
    block.start_pc = pc;
    block.page_start = static_cast<uint8_t>(pc >> 8);
    const auto disable_ir_for_unsafe_writes = [this, &block]() {
        for (uint8_t i = 0; i < block.count; ++i) {
            const CachedOpKind kind = block.ops[i].kind;
            if ((kind == CachedOpKind::StaAbs || kind == CachedOpKind::StxAbs ||
                 kind == CachedOpKind::StaZp || kind == CachedOpKind::StaZpX ||
                 kind == CachedOpKind::StaAbsX || kind == CachedOpKind::StaAbsY ||
                 kind == CachedOpKind::StyZp || kind == CachedOpKind::StyAbs ||
                 kind == CachedOpKind::StxZp || kind == CachedOpKind::IncZp) &&
                !cached_write_is_safe_for_block(block, block.ops[i])) {
                block.executable = false;
                return;
            }
        }
    };

    uint16_t cursor = pc;
    while (block.count < J6510_CACHED_BLOCK_MAX_OPS) {
        const uint8_t opcode = read(cursor);
        const OpcodeInfo& info = active_opcode_info(opcode);
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
            block.hot_executable = false;
        } else if (!is_hot_cached_op(op.kind)) {
            block.hot_executable = false;
        }

        if (info.operation == Operation::Illegal) {
            block.page_end = static_cast<uint8_t>(cursor >> 8);
            block.terminator = RunStopReason::IllegalOpcode;
            block.executable = false;
            block.hot_executable = false;
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

J6510_FAST_CODE_ATTR bool Cpu6510::execute_cached_block(const CachedBlock& block, uint32_t remaining_budget, RunResult& result) {
    const uint32_t to_execute = block.count < remaining_budget ? block.count : remaining_budget;
    if (block.executable && can_use_fast_run_path()) {
        J6510_CACHE_STATS(++block_cache_stats_.ir_blocks);
        J6510_CACHE_STATS(block_cache_stats_.ir_instructions += to_execute);
        if (can_use_direct_memory_path()) {
            if (block.hot_executable) {
                execute_cached_block_direct_hot(block, to_execute, result);
            } else {
                execute_cached_block_direct(block, to_execute, result);
            }
        } else {
            for (uint32_t i = 0; i < to_execute; ++i) {
                execute_cached_op(block.ops[i]);
                ++result.instructions_executed;
                result.stop_pc = state_.pc;
            }
        }
        return true;
    }

    J6510_CACHE_STATS(++block_cache_stats_.fallback_blocks);
    J6510_CACHE_STATS(block_cache_stats_.fallback_instructions += to_execute);
#if J6510_ENABLE_CACHE_STATS
    for (uint32_t i = 0; i < to_execute; ++i) {
        ++block_cache_stats_.fallback_opcodes[block.opcodes[i]];
        if (block.ops[i].kind == CachedOpKind::Fallback) {
            ++block_cache_stats_.unsupported_fallback_opcodes[block.opcodes[i]];
        }
    }
#endif
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
    case 0x10:
        op = CachedOp{CachedOpKind::BranchClear, make_branch_operand(operand, FLAG_N)};
        return true;
    case 0x18:
        op = CachedOp{CachedOpKind::FlagClear, FLAG_C};
        return true;
    case 0x30:
        op = CachedOp{CachedOpKind::BranchSet, make_branch_operand(operand, FLAG_N)};
        return true;
    case 0x38:
        op = CachedOp{CachedOpKind::FlagSet, FLAG_C};
        return true;
    case 0x4C:
        op = CachedOp{CachedOpKind::JmpAbs, operand};
        return true;
    case 0x50:
        op = CachedOp{CachedOpKind::BranchClear, make_branch_operand(operand, FLAG_V)};
        return true;
    case 0x58:
        op = CachedOp{CachedOpKind::FlagClear, FLAG_I};
        return true;
    case 0x69:
        op = CachedOp{CachedOpKind::AdcImm, operand};
        return true;
    case 0x70:
        op = CachedOp{CachedOpKind::BranchSet, make_branch_operand(operand, FLAG_V)};
        return true;
    case 0x78:
        op = CachedOp{CachedOpKind::FlagSet, FLAG_I};
        return true;
    case 0x84:
        op = CachedOp{CachedOpKind::StyZp, operand};
        return true;
    case 0x8D:
        op = CachedOp{CachedOpKind::StaAbs, operand};
        return true;
    case 0x8E:
        op = CachedOp{CachedOpKind::StxAbs, operand};
        return true;
    case 0x86:
        op = CachedOp{CachedOpKind::StxZp, operand};
        return true;
    case 0x8C:
        op = CachedOp{CachedOpKind::StyAbs, operand};
        return true;
    case 0x98:
        op = CachedOp{CachedOpKind::Tya, 0};
        return true;
    case 0x9A:
        op = CachedOp{CachedOpKind::Txs, 0};
        return true;
    case 0x9D:
        op = CachedOp{CachedOpKind::StaAbsX, operand};
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
    case 0xA4:
        op = CachedOp{CachedOpKind::LdyZp, operand};
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
    case 0xA5:
        op = CachedOp{CachedOpKind::LdaZp, operand};
        return true;
    case 0xA6:
        op = CachedOp{CachedOpKind::LdxZp, operand};
        return true;
    case 0xA8:
        op = CachedOp{CachedOpKind::Tay, 0};
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
    case 0xAC:
        op = CachedOp{CachedOpKind::LdyAbs, operand};
        return true;
    case 0xB5:
        op = CachedOp{CachedOpKind::LdaZpX, operand};
        return true;
    case 0xB6:
        op = CachedOp{CachedOpKind::LdxZpY, operand};
        return true;
    case 0xB9:
        op = CachedOp{CachedOpKind::LdaAbsY, operand};
        return true;
    case 0x90:
        op = CachedOp{CachedOpKind::BranchClear, make_branch_operand(operand, FLAG_C)};
        return true;
    case 0xB0:
        op = CachedOp{CachedOpKind::BranchSet, make_branch_operand(operand, FLAG_C)};
        return true;
    case 0xB8:
        op = CachedOp{CachedOpKind::FlagClear, FLAG_V};
        return true;
    case 0xBA:
        op = CachedOp{CachedOpKind::Tsx, 0};
        return true;
    case 0xB4:
        op = CachedOp{CachedOpKind::LdyZpX, operand};
        return true;
    case 0xBC:
        op = CachedOp{CachedOpKind::LdyAbsX, operand};
        return true;
    case 0xBD:
        op = CachedOp{CachedOpKind::LdaAbsX, operand};
        return true;
    case 0xBE:
        op = CachedOp{CachedOpKind::LdxAbsY, operand};
        return true;
    case 0xCA:
        op = CachedOp{CachedOpKind::Dex, 0};
        return true;
    case 0xC9:
        op = CachedOp{CachedOpKind::CmpImm, operand};
        return true;
    case 0xC8:
        op = CachedOp{CachedOpKind::Iny, 0};
        return true;
    case 0xD0:
        op = CachedOp{CachedOpKind::BranchClear, make_branch_operand(operand, FLAG_Z)};
        return true;
    case 0xE8:
        op = CachedOp{CachedOpKind::Inx, 0};
        return true;
    case 0xE6:
        op = CachedOp{CachedOpKind::IncZp, operand};
        return true;
    case 0xD8:
        op = CachedOp{CachedOpKind::FlagClear, FLAG_D};
        return true;
    case 0xEA:
        op = CachedOp{CachedOpKind::Nop, 0};
        return true;
    case 0xF0:
        op = CachedOp{CachedOpKind::BranchSet, make_branch_operand(operand, FLAG_Z)};
        return true;
    case 0xF8:
        op = CachedOp{CachedOpKind::FlagSet, FLAG_D};
        return true;
    default:
        op = CachedOp{CachedOpKind::Fallback, 0};
        return false;
    }
}

bool Cpu6510::is_hot_cached_op(CachedOpKind kind) const {
    switch (kind) {
    case CachedOpKind::LdaImm:
    case CachedOpKind::LdxImm:
    case CachedOpKind::LdyImm:
    case CachedOpKind::LdaAbs:
    case CachedOpKind::LdxAbs:
    case CachedOpKind::LdaZpX:
    case CachedOpKind::LdaAbsY:
    case CachedOpKind::StaAbs:
    case CachedOpKind::StxAbs:
    case CachedOpKind::StaZp:
    case CachedOpKind::StaZpX:
    case CachedOpKind::StaAbsY:
    case CachedOpKind::Tax:
    case CachedOpKind::Txa:
    case CachedOpKind::Inx:
    case CachedOpKind::Dex:
    case CachedOpKind::Dey:
    case CachedOpKind::Nop:
    case CachedOpKind::FlagSet:
    case CachedOpKind::FlagClear:
    case CachedOpKind::BranchSet:
    case CachedOpKind::BranchClear:
    case CachedOpKind::JmpAbs:
    case CachedOpKind::AdcImm:
    case CachedOpKind::CmpImm:
    case CachedOpKind::IncZp:
        return true;
    default:
        return false;
    }
}

bool Cpu6510::cached_write_is_safe_for_block(const CachedBlock& block, const CachedOp& op) const {
    switch (op.kind) {
    case CachedOpKind::StaAbs:
    case CachedOpKind::StxAbs:
    case CachedOpKind::StyAbs:
        return !block_uses_page(block, static_cast<uint8_t>(op.operand >> 8));
    case CachedOpKind::StaZp:
    case CachedOpKind::StaZpX:
    case CachedOpKind::StyZp:
    case CachedOpKind::StxZp:
    case CachedOpKind::IncZp:
        return !block_uses_page(block, 0x00);
    case CachedOpKind::StaAbsX:
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
    return direct_memory_ != nullptr;
}

#endif

void Cpu6510::direct_write(uint16_t address, uint8_t value) {
    direct_memory_[address] = value;
#if J6510_ENABLE_BLOCK_CACHE
    const uint8_t page = static_cast<uint8_t>(address >> 8);
    if (valid_cached_blocks_ != 0 && cached_page_use_count_[page] != 0) {
        invalidate_block_cache_for_write(address);
    }
#else
    (void)address;
#endif
}

#if J6510_ENABLE_BLOCK_CACHE

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
    case CachedOpKind::LdaZp:
        state_.a = read(static_cast<uint8_t>(op.operand));
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        set_zn(state_.a);
        return;
    case CachedOpKind::LdxZp:
        state_.x = read(static_cast<uint8_t>(op.operand));
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        set_zn(state_.x);
        return;
    case CachedOpKind::LdyZp:
        state_.y = read(static_cast<uint8_t>(op.operand));
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        set_zn(state_.y);
        return;
    case CachedOpKind::LdyAbs:
        state_.y = read(op.operand);
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        set_zn(state_.y);
        return;
    case CachedOpKind::LdxZpY:
        state_.x = read(static_cast<uint8_t>(op.operand + state_.y));
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        set_zn(state_.x);
        return;
    case CachedOpKind::LdyZpX:
        state_.y = read(static_cast<uint8_t>(op.operand + state_.x));
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        set_zn(state_.y);
        return;
    case CachedOpKind::LdaAbsX:
        state_.a = read(static_cast<uint16_t>(op.operand + state_.x));
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        set_zn(state_.a);
        return;
    case CachedOpKind::LdxAbsY:
        state_.x = read(static_cast<uint16_t>(op.operand + state_.y));
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        set_zn(state_.x);
        return;
    case CachedOpKind::LdyAbsX:
        state_.y = read(static_cast<uint16_t>(op.operand + state_.x));
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        set_zn(state_.y);
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
    case CachedOpKind::StaAbsX:
        write(static_cast<uint16_t>(op.operand + state_.x), state_.a);
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        return;
    case CachedOpKind::StaAbsY:
        write(static_cast<uint16_t>(op.operand + state_.y), state_.a);
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        return;
    case CachedOpKind::StyZp:
        write(static_cast<uint8_t>(op.operand), state_.y);
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        return;
    case CachedOpKind::StyAbs:
        write(op.operand, state_.y);
        state_.pc = static_cast<uint16_t>(state_.pc + 3);
        return;
    case CachedOpKind::StxZp:
        write(static_cast<uint8_t>(op.operand), state_.x);
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        return;
    case CachedOpKind::Tax:
        state_.x = state_.a;
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.x);
        return;
    case CachedOpKind::Tay:
        state_.y = state_.a;
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.y);
        return;
    case CachedOpKind::Txa:
        state_.a = state_.x;
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.a);
        return;
    case CachedOpKind::Tya:
        state_.a = state_.y;
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.a);
        return;
    case CachedOpKind::Tsx:
        state_.x = state_.sp;
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.x);
        return;
    case CachedOpKind::Txs:
        state_.sp = state_.x;
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        return;
    case CachedOpKind::Inx:
        state_.x = static_cast<uint8_t>(state_.x + 1);
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.x);
        return;
    case CachedOpKind::Iny:
        state_.y = static_cast<uint8_t>(state_.y + 1);
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        set_zn(state_.y);
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
    case CachedOpKind::FlagSet:
        state_.p = static_cast<uint8_t>(state_.p | static_cast<uint8_t>(op.operand) | FLAG_U);
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        return;
    case CachedOpKind::FlagClear:
        state_.p = static_cast<uint8_t>((state_.p & ~static_cast<uint8_t>(op.operand)) | FLAG_U);
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        return;
    case CachedOpKind::BranchSet:
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        if ((state_.p & branch_flag(op.operand)) != 0) {
            state_.pc = static_cast<uint16_t>(state_.pc + branch_offset(op.operand));
        }
        return;
    case CachedOpKind::BranchClear:
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        if ((state_.p & branch_flag(op.operand)) == 0) {
            state_.pc = static_cast<uint16_t>(state_.pc + branch_offset(op.operand));
        }
        return;
    case CachedOpKind::JmpAbs:
        state_.pc = op.operand;
        return;
    case CachedOpKind::AdcImm:
        adc(static_cast<uint8_t>(op.operand));
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        return;
    case CachedOpKind::CmpImm:
        compare(state_.a, static_cast<uint8_t>(op.operand));
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        return;
    case CachedOpKind::IncZp: {
        const uint16_t address = static_cast<uint8_t>(op.operand);
        const uint8_t value = static_cast<uint8_t>(read(address) + 1);
        write(address, value);
        state_.pc = static_cast<uint16_t>(state_.pc + 2);
        set_zn(value);
        return;
    }
    case CachedOpKind::Fallback:
        return;
    }
}

J6510_FAST_CODE_ATTR void Cpu6510::execute_cached_block_direct_hot(const CachedBlock& block,
                                                                    uint32_t to_execute,
                                                                    RunResult& result) {
    uint8_t* memory = direct_memory_;
    const bool port_enabled = config_.port_enabled;
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
    const auto read_direct = [this, memory, port_enabled](uint16_t address) -> uint8_t {
        if (J6510_UNLIKELY(port_enabled && (address == 0x0000 || address == 0x0001))) {
            return read_port(address);
        }
        return memory[address];
    };
    const auto write_direct = [this, memory, port_enabled](uint16_t address, uint8_t value) {
        if (J6510_UNLIKELY(port_enabled && (address == 0x0000 || address == 0x0001))) {
            write_port(address, value);
            return;
        }
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
            a = read_direct(op.operand);
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(a);
            break;
        case CachedOpKind::LdxAbs:
            x = read_direct(op.operand);
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(x);
            break;
        case CachedOpKind::LdaZpX:
            a = read_direct(static_cast<uint8_t>(op.operand + x));
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(a);
            break;
        case CachedOpKind::LdaAbsY:
            a = read_direct(static_cast<uint16_t>(op.operand + y));
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
        case CachedOpKind::FlagSet:
            p = static_cast<uint8_t>(p | static_cast<uint8_t>(op.operand) | FLAG_U);
            pc = static_cast<uint16_t>(pc + 1);
            break;
        case CachedOpKind::FlagClear:
            p = static_cast<uint8_t>((p & ~static_cast<uint8_t>(op.operand)) | FLAG_U);
            pc = static_cast<uint16_t>(pc + 1);
            break;
        case CachedOpKind::BranchSet:
            pc = static_cast<uint16_t>(pc + 2);
            if ((p & branch_flag(op.operand)) != 0) {
                pc = static_cast<uint16_t>(pc + branch_offset(op.operand));
            }
            break;
        case CachedOpKind::BranchClear:
            pc = static_cast<uint16_t>(pc + 2);
            if ((p & branch_flag(op.operand)) == 0) {
                pc = static_cast<uint16_t>(pc + branch_offset(op.operand));
            }
            break;
        case CachedOpKind::JmpAbs:
            pc = op.operand;
            break;
        case CachedOpKind::AdcImm:
            a = adc_result(a, static_cast<uint8_t>(op.operand), p);
            pc = static_cast<uint16_t>(pc + 2);
            break;
        case CachedOpKind::CmpImm:
            p = compare_flags(p, a, static_cast<uint8_t>(op.operand));
            pc = static_cast<uint16_t>(pc + 2);
            break;
        case CachedOpKind::IncZp: {
            const uint16_t address = static_cast<uint8_t>(op.operand);
            const uint8_t value = static_cast<uint8_t>(read_direct(address) + 1);
            write_direct(address, value);
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(value);
            break;
        }
        default:
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

J6510_FAST_CODE_ATTR void Cpu6510::execute_cached_block_direct(const CachedBlock& block,
                                                                uint32_t to_execute,
                                                                RunResult& result) {
    uint8_t* memory = direct_memory_;
    const bool port_enabled = config_.port_enabled;
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
    const auto read_direct = [this, memory, port_enabled](uint16_t address) -> uint8_t {
        if (J6510_UNLIKELY(port_enabled && (address == 0x0000 || address == 0x0001))) {
            return read_port(address);
        }
        return memory[address];
    };
    const auto write_direct = [this, memory, port_enabled](uint16_t address, uint8_t value) {
        if (J6510_UNLIKELY(port_enabled && (address == 0x0000 || address == 0x0001))) {
            write_port(address, value);
            return;
        }
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
            a = read_direct(op.operand);
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(a);
            break;
        case CachedOpKind::LdxAbs:
            x = read_direct(op.operand);
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(x);
            break;
        case CachedOpKind::LdaZpX:
            a = read_direct(static_cast<uint8_t>(op.operand + x));
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(a);
            break;
        case CachedOpKind::LdaAbsY:
            a = read_direct(static_cast<uint16_t>(op.operand + y));
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(a);
            break;
        case CachedOpKind::LdaZp:
            a = read_direct(static_cast<uint8_t>(op.operand));
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(a);
            break;
        case CachedOpKind::LdxZp:
            x = read_direct(static_cast<uint8_t>(op.operand));
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(x);
            break;
        case CachedOpKind::LdyZp:
            y = read_direct(static_cast<uint8_t>(op.operand));
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(y);
            break;
        case CachedOpKind::LdyAbs:
            y = read_direct(op.operand);
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(y);
            break;
        case CachedOpKind::LdxZpY:
            x = read_direct(static_cast<uint8_t>(op.operand + y));
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(x);
            break;
        case CachedOpKind::LdyZpX:
            y = read_direct(static_cast<uint8_t>(op.operand + x));
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(y);
            break;
        case CachedOpKind::LdaAbsX:
            a = read_direct(static_cast<uint16_t>(op.operand + x));
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(a);
            break;
        case CachedOpKind::LdxAbsY:
            x = read_direct(static_cast<uint16_t>(op.operand + y));
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(x);
            break;
        case CachedOpKind::LdyAbsX:
            y = read_direct(static_cast<uint16_t>(op.operand + x));
            pc = static_cast<uint16_t>(pc + 3);
            set_zn_local(y);
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
        case CachedOpKind::StaAbsX:
            write_direct(static_cast<uint16_t>(op.operand + x), a);
            pc = static_cast<uint16_t>(pc + 3);
            break;
        case CachedOpKind::StaAbsY:
            write_direct(static_cast<uint16_t>(op.operand + y), a);
            pc = static_cast<uint16_t>(pc + 3);
            break;
        case CachedOpKind::StyZp:
            write_direct(static_cast<uint8_t>(op.operand), y);
            pc = static_cast<uint16_t>(pc + 2);
            break;
        case CachedOpKind::StyAbs:
            write_direct(op.operand, y);
            pc = static_cast<uint16_t>(pc + 3);
            break;
        case CachedOpKind::StxZp:
            write_direct(static_cast<uint8_t>(op.operand), x);
            pc = static_cast<uint16_t>(pc + 2);
            break;
        case CachedOpKind::Tax:
            x = a;
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(x);
            break;
        case CachedOpKind::Tay:
            y = a;
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(y);
            break;
        case CachedOpKind::Txa:
            a = x;
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(a);
            break;
        case CachedOpKind::Tya:
            a = y;
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(a);
            break;
        case CachedOpKind::Tsx:
            x = state_.sp;
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(x);
            break;
        case CachedOpKind::Txs:
            state_.sp = x;
            pc = static_cast<uint16_t>(pc + 1);
            break;
        case CachedOpKind::Inx:
            x = static_cast<uint8_t>(x + 1);
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(x);
            break;
        case CachedOpKind::Iny:
            y = static_cast<uint8_t>(y + 1);
            pc = static_cast<uint16_t>(pc + 1);
            set_zn_local(y);
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
        case CachedOpKind::FlagSet:
            p = static_cast<uint8_t>(p | static_cast<uint8_t>(op.operand) | FLAG_U);
            pc = static_cast<uint16_t>(pc + 1);
            break;
        case CachedOpKind::FlagClear:
            p = static_cast<uint8_t>((p & ~static_cast<uint8_t>(op.operand)) | FLAG_U);
            pc = static_cast<uint16_t>(pc + 1);
            break;
        case CachedOpKind::BranchSet:
            pc = static_cast<uint16_t>(pc + 2);
            if ((p & branch_flag(op.operand)) != 0) {
                pc = static_cast<uint16_t>(pc + branch_offset(op.operand));
            }
            break;
        case CachedOpKind::BranchClear:
            pc = static_cast<uint16_t>(pc + 2);
            if ((p & branch_flag(op.operand)) == 0) {
                pc = static_cast<uint16_t>(pc + branch_offset(op.operand));
            }
            break;
        case CachedOpKind::JmpAbs:
            pc = op.operand;
            break;
        case CachedOpKind::AdcImm:
            a = adc_result(a, static_cast<uint8_t>(op.operand), p);
            pc = static_cast<uint16_t>(pc + 2);
            break;
        case CachedOpKind::CmpImm:
            p = compare_flags(p, a, static_cast<uint8_t>(op.operand));
            pc = static_cast<uint16_t>(pc + 2);
            break;
        case CachedOpKind::IncZp: {
            const uint16_t address = static_cast<uint8_t>(op.operand);
            const uint8_t value = static_cast<uint8_t>(read_direct(address) + 1);
            write_direct(address, value);
            pc = static_cast<uint16_t>(pc + 2);
            set_zn_local(value);
            break;
        }
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
#endif

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

void Cpu6510::slo(AddressingMode mode) {
    const uint16_t address = operand_address(mode);
    const uint8_t value = read(address);
    const uint8_t result = static_cast<uint8_t>(value << 1);
    set_flag(FLAG_C, (value & 0x80) != 0);
    write(address, result);
    state_.a = static_cast<uint8_t>(state_.a | result);
    set_zn(state_.a);
}

void Cpu6510::rla(AddressingMode mode) {
    const uint16_t address = operand_address(mode);
    const uint8_t value = read(address);
    const uint8_t result = static_cast<uint8_t>((value << 1) | (flag(FLAG_C) ? 1 : 0));
    set_flag(FLAG_C, (value & 0x80) != 0);
    write(address, result);
    state_.a = static_cast<uint8_t>(state_.a & result);
    set_zn(state_.a);
}

void Cpu6510::sre(AddressingMode mode) {
    const uint16_t address = operand_address(mode);
    const uint8_t value = read(address);
    const uint8_t result = static_cast<uint8_t>(value >> 1);
    set_flag(FLAG_C, (value & 0x01) != 0);
    write(address, result);
    state_.a = static_cast<uint8_t>(state_.a ^ result);
    set_zn(state_.a);
}

void Cpu6510::rra(AddressingMode mode) {
    const uint16_t address = operand_address(mode);
    const uint8_t value = read(address);
    const uint8_t result = static_cast<uint8_t>((value >> 1) | (flag(FLAG_C) ? 0x80 : 0));
    set_flag(FLAG_C, (value & 0x01) != 0);
    write(address, result);
    adc(result);
}

void Cpu6510::sax(AddressingMode mode) {
    write(operand_address(mode), static_cast<uint8_t>(state_.a & state_.x));
}

void Cpu6510::lax(AddressingMode mode) {
    const uint8_t value = read_operand(mode);
    state_.a = value;
    state_.x = value;
    set_zn(value);
}

void Cpu6510::dcp(AddressingMode mode) {
    const uint16_t address = operand_address(mode);
    const uint8_t value = static_cast<uint8_t>(read(address) - 1);
    write(address, value);
    compare(state_.a, value);
}

void Cpu6510::isc(AddressingMode mode) {
    const uint16_t address = operand_address(mode);
    const uint8_t value = static_cast<uint8_t>(read(address) + 1);
    write(address, value);
    sbc(value);
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

const OpcodeInfo& Cpu6510::active_opcode_info(uint8_t opcode) const {
    return config_.undocumented_opcodes_enabled ? undocumented_opcode_info(opcode) : opcode_info(opcode);
}

StepResult Cpu6510::execute_cycle_exact_instruction(uint8_t& cycles) {
    cycles = 0;
    const auto bus_read = [this, &cycles](uint16_t address) {
        ++cycles;
        return read(address);
    };
    const auto bus_write = [this, &cycles](uint16_t address, uint8_t value) {
        ++cycles;
        write(address, value);
    };
    const auto stack_read = [&bus_read](uint8_t sp) {
        return bus_read(static_cast<uint16_t>(0x0100 | sp));
    };
    const auto stack_write = [this, &bus_write](uint8_t value) {
        bus_write(static_cast<uint16_t>(0x0100 | state_.sp), value);
        state_.sp = static_cast<uint8_t>(state_.sp - 1);
    };
    const auto stack_pull = [this, &bus_read]() {
        state_.sp = static_cast<uint8_t>(state_.sp + 1);
        return bus_read(static_cast<uint16_t>(0x0100 | state_.sp));
    };
    const auto read16_at = [&bus_read](uint16_t address) {
        const uint8_t low = bus_read(address);
        const uint8_t high = bus_read(static_cast<uint16_t>(address + 1));
        return static_cast<uint16_t>((high << 8) | low);
    };
    const auto read16_zp_at = [&bus_read](uint8_t address) {
        const uint8_t low = bus_read(address);
        const uint8_t high = bus_read(static_cast<uint8_t>(address + 1));
        return static_cast<uint16_t>((high << 8) | low);
    };
    const auto read16_nmos_at = [&bus_read](uint16_t address) {
        const uint8_t low = bus_read(address);
        const uint16_t high_address = static_cast<uint16_t>((address & 0xFF00) | static_cast<uint8_t>(address + 1));
        const uint8_t high = bus_read(high_address);
        return static_cast<uint16_t>((high << 8) | low);
    };
    const auto dummy_to = [&bus_read](uint16_t address) {
        (void)bus_read(address);
    };
    const auto same_page = [](uint16_t lhs, uint16_t rhs) {
        return (lhs & 0xFF00) == (rhs & 0xFF00);
    };
    const auto wrong_indexed_address = [](uint16_t base, uint8_t index) {
        return static_cast<uint16_t>((base & 0xFF00) | static_cast<uint8_t>(base + index));
    };

    if (interrupt_poll_callback_ || interrupts_.reset_pending || interrupts_.nmi_pending || interrupts_.irq_level) {
        poll_target_interrupts();
        if (interrupts_.reset_pending) {
            interrupts_.reset_pending = false;
            state_.sp = 0xFD;
            state_.p = FLAG_U | FLAG_I;
            dummy_to(state_.pc);
            dummy_to(state_.pc);
            dummy_to(static_cast<uint16_t>(0x0100 | state_.sp));
            dummy_to(static_cast<uint16_t>(0x0100 | state_.sp));
            dummy_to(static_cast<uint16_t>(0x0100 | state_.sp));
            state_.pc = read16_at(0xFFFC);
            return StepResult::Ok;
        }
        if (interrupts_.nmi_pending) {
            interrupts_.nmi_pending = false;
            dummy_to(state_.pc);
            dummy_to(state_.pc);
            stack_write(static_cast<uint8_t>(state_.pc >> 8));
            stack_write(static_cast<uint8_t>(state_.pc & 0x00FF));
            stack_write(static_cast<uint8_t>(state_.p | FLAG_U));
            set_flag(FLAG_I, true);
            state_.pc = read16_at(0xFFFA);
            return StepResult::Ok;
        }
        if (interrupts_.irq_level && !flag(FLAG_I)) {
            dummy_to(state_.pc);
            dummy_to(state_.pc);
            stack_write(static_cast<uint8_t>(state_.pc >> 8));
            stack_write(static_cast<uint8_t>(state_.pc & 0x00FF));
            stack_write(static_cast<uint8_t>(state_.p | FLAG_U));
            set_flag(FLAG_I, true);
            state_.pc = read16_at(0xFFFE);
            return StepResult::Ok;
        }
    }

    const uint16_t instruction_pc = state_.pc;
    const uint8_t opcode = bus_read(state_.pc++);
    const OpcodeInfo& info = active_opcode_info(opcode);
    if (info.operation == Operation::Illegal) {
        state_.pc = instruction_pc;
        return StepResult::IllegalOpcode;
    }

    const auto read_operand_exact = [&](AddressingMode mode, uint16_t& address) {
        address = 0;
        switch (mode) {
        case AddressingMode::Accumulator:
            dummy_to(state_.pc);
            return state_.a;
        case AddressingMode::Immediate:
            address = state_.pc++;
            return bus_read(address);
        case AddressingMode::ZeroPage:
            address = bus_read(state_.pc++);
            return bus_read(address);
        case AddressingMode::ZeroPageX: {
            const uint8_t base = bus_read(state_.pc++);
            dummy_to(base);
            address = static_cast<uint8_t>(base + state_.x);
            return bus_read(address);
        }
        case AddressingMode::ZeroPageY: {
            const uint8_t base = bus_read(state_.pc++);
            dummy_to(base);
            address = static_cast<uint8_t>(base + state_.y);
            return bus_read(address);
        }
        case AddressingMode::Absolute:
            address = read16_at(state_.pc);
            state_.pc = static_cast<uint16_t>(state_.pc + 2);
            return bus_read(address);
        case AddressingMode::AbsoluteX: {
            const uint16_t base = read16_at(state_.pc);
            state_.pc = static_cast<uint16_t>(state_.pc + 2);
            address = static_cast<uint16_t>(base + state_.x);
            if (!same_page(base, address)) {
                dummy_to(wrong_indexed_address(base, state_.x));
            }
            return bus_read(address);
        }
        case AddressingMode::AbsoluteY: {
            const uint16_t base = read16_at(state_.pc);
            state_.pc = static_cast<uint16_t>(state_.pc + 2);
            address = static_cast<uint16_t>(base + state_.y);
            if (!same_page(base, address)) {
                dummy_to(wrong_indexed_address(base, state_.y));
            }
            return bus_read(address);
        }
        case AddressingMode::IndexedIndirect: {
            const uint8_t operand = bus_read(state_.pc++);
            dummy_to(operand);
            address = read16_zp_at(static_cast<uint8_t>(operand + state_.x));
            return bus_read(address);
        }
        case AddressingMode::IndirectIndexed: {
            const uint8_t operand = bus_read(state_.pc++);
            const uint16_t base = read16_zp_at(operand);
            address = static_cast<uint16_t>(base + state_.y);
            if (!same_page(base, address)) {
                dummy_to(wrong_indexed_address(base, state_.y));
            }
            return bus_read(address);
        }
        case AddressingMode::Implied:
        case AddressingMode::Relative:
        case AddressingMode::Indirect:
            dummy_to(state_.pc);
            return uint8_t{0};
        }
        dummy_to(state_.pc);
        return uint8_t{0};
    };

    const auto write_operand_exact = [&](AddressingMode mode, uint8_t value) {
        switch (mode) {
        case AddressingMode::ZeroPage: {
            const uint8_t address = bus_read(state_.pc++);
            bus_write(address, value);
            return;
        }
        case AddressingMode::ZeroPageX: {
            const uint8_t base = bus_read(state_.pc++);
            dummy_to(base);
            bus_write(static_cast<uint8_t>(base + state_.x), value);
            return;
        }
        case AddressingMode::ZeroPageY: {
            const uint8_t base = bus_read(state_.pc++);
            dummy_to(base);
            bus_write(static_cast<uint8_t>(base + state_.y), value);
            return;
        }
        case AddressingMode::Absolute: {
            const uint16_t address = read16_at(state_.pc);
            state_.pc = static_cast<uint16_t>(state_.pc + 2);
            bus_write(address, value);
            return;
        }
        case AddressingMode::AbsoluteX: {
            const uint16_t base = read16_at(state_.pc);
            state_.pc = static_cast<uint16_t>(state_.pc + 2);
            dummy_to(wrong_indexed_address(base, state_.x));
            bus_write(static_cast<uint16_t>(base + state_.x), value);
            return;
        }
        case AddressingMode::AbsoluteY: {
            const uint16_t base = read16_at(state_.pc);
            state_.pc = static_cast<uint16_t>(state_.pc + 2);
            dummy_to(wrong_indexed_address(base, state_.y));
            bus_write(static_cast<uint16_t>(base + state_.y), value);
            return;
        }
        case AddressingMode::IndexedIndirect: {
            const uint8_t operand = bus_read(state_.pc++);
            dummy_to(operand);
            const uint16_t address = read16_zp_at(static_cast<uint8_t>(operand + state_.x));
            bus_write(address, value);
            return;
        }
        case AddressingMode::IndirectIndexed: {
            const uint8_t operand = bus_read(state_.pc++);
            const uint16_t base = read16_zp_at(operand);
            dummy_to(wrong_indexed_address(base, state_.y));
            bus_write(static_cast<uint16_t>(base + state_.y), value);
            return;
        }
        case AddressingMode::Accumulator:
            dummy_to(state_.pc);
            state_.a = value;
            return;
        case AddressingMode::Immediate:
        case AddressingMode::Implied:
        case AddressingMode::Relative:
        case AddressingMode::Indirect:
            dummy_to(state_.pc);
            return;
        }
    };

    const auto rmw_operand_exact = [&](AddressingMode mode, auto transform) {
        if (mode == AddressingMode::Accumulator) {
            dummy_to(state_.pc);
            state_.a = transform(state_.a);
            set_zn(state_.a);
            return state_.a;
        }

        uint16_t address = 0;
        switch (mode) {
        case AddressingMode::ZeroPage:
            address = bus_read(state_.pc++);
            break;
        case AddressingMode::ZeroPageX: {
            const uint8_t base = bus_read(state_.pc++);
            dummy_to(base);
            address = static_cast<uint8_t>(base + state_.x);
            break;
        }
        case AddressingMode::Absolute:
            address = read16_at(state_.pc);
            state_.pc = static_cast<uint16_t>(state_.pc + 2);
            break;
        case AddressingMode::AbsoluteX: {
            const uint16_t base = read16_at(state_.pc);
            state_.pc = static_cast<uint16_t>(state_.pc + 2);
            dummy_to(wrong_indexed_address(base, state_.x));
            address = static_cast<uint16_t>(base + state_.x);
            break;
        }
        case AddressingMode::AbsoluteY: {
            const uint16_t base = read16_at(state_.pc);
            state_.pc = static_cast<uint16_t>(state_.pc + 2);
            dummy_to(wrong_indexed_address(base, state_.y));
            address = static_cast<uint16_t>(base + state_.y);
            break;
        }
        case AddressingMode::IndexedIndirect: {
            const uint8_t operand = bus_read(state_.pc++);
            dummy_to(operand);
            address = read16_zp_at(static_cast<uint8_t>(operand + state_.x));
            break;
        }
        case AddressingMode::IndirectIndexed: {
            const uint8_t operand = bus_read(state_.pc++);
            const uint16_t base = read16_zp_at(operand);
            dummy_to(wrong_indexed_address(base, state_.y));
            address = static_cast<uint16_t>(base + state_.y);
            break;
        }
        default:
            dummy_to(state_.pc);
            break;
        }
        const uint8_t old_value = bus_read(address);
        bus_write(address, old_value);
        const uint8_t new_value = transform(old_value);
        bus_write(address, new_value);
        set_zn(new_value);
        return new_value;
    };

    switch (info.operation) {
    case Operation::LDA: {
        uint16_t address = 0;
        state_.a = read_operand_exact(info.mode, address);
        set_zn(state_.a);
        break;
    }
    case Operation::LDX: {
        uint16_t address = 0;
        state_.x = read_operand_exact(info.mode, address);
        set_zn(state_.x);
        break;
    }
    case Operation::LDY: {
        uint16_t address = 0;
        state_.y = read_operand_exact(info.mode, address);
        set_zn(state_.y);
        break;
    }
    case Operation::STA:
        write_operand_exact(info.mode, state_.a);
        break;
    case Operation::STX:
        write_operand_exact(info.mode, state_.x);
        break;
    case Operation::STY:
        write_operand_exact(info.mode, state_.y);
        break;
    case Operation::AND: {
        uint16_t address = 0;
        state_.a = static_cast<uint8_t>(state_.a & read_operand_exact(info.mode, address));
        set_zn(state_.a);
        break;
    }
    case Operation::ORA: {
        uint16_t address = 0;
        state_.a = static_cast<uint8_t>(state_.a | read_operand_exact(info.mode, address));
        set_zn(state_.a);
        break;
    }
    case Operation::EOR: {
        uint16_t address = 0;
        state_.a = static_cast<uint8_t>(state_.a ^ read_operand_exact(info.mode, address));
        set_zn(state_.a);
        break;
    }
    case Operation::BIT: {
        uint16_t address = 0;
        const uint8_t value = read_operand_exact(info.mode, address);
        set_flag(FLAG_Z, (state_.a & value) == 0);
        set_flag(FLAG_N, (value & FLAG_N) != 0);
        set_flag(FLAG_V, (value & FLAG_V) != 0);
        break;
    }
    case Operation::ADC: {
        uint16_t address = 0;
        adc(read_operand_exact(info.mode, address));
        break;
    }
    case Operation::SBC: {
        uint16_t address = 0;
        sbc(read_operand_exact(info.mode, address));
        break;
    }
    case Operation::CMP: {
        uint16_t address = 0;
        compare(state_.a, read_operand_exact(info.mode, address));
        break;
    }
    case Operation::CPX: {
        uint16_t address = 0;
        compare(state_.x, read_operand_exact(info.mode, address));
        break;
    }
    case Operation::CPY: {
        uint16_t address = 0;
        compare(state_.y, read_operand_exact(info.mode, address));
        break;
    }
    case Operation::INC:
        rmw_operand_exact(info.mode, [](uint8_t value) { return static_cast<uint8_t>(value + 1); });
        break;
    case Operation::DEC:
        rmw_operand_exact(info.mode, [](uint8_t value) { return static_cast<uint8_t>(value - 1); });
        break;
    case Operation::ASL:
        rmw_operand_exact(info.mode, [this](uint8_t value) {
            set_flag(FLAG_C, (value & 0x80) != 0);
            return static_cast<uint8_t>(value << 1);
        });
        break;
    case Operation::LSR:
        rmw_operand_exact(info.mode, [this](uint8_t value) {
            set_flag(FLAG_C, (value & 0x01) != 0);
            return static_cast<uint8_t>(value >> 1);
        });
        break;
    case Operation::ROL:
        rmw_operand_exact(info.mode, [this](uint8_t value) {
            const uint8_t result = static_cast<uint8_t>((value << 1) | (flag(FLAG_C) ? 1 : 0));
            set_flag(FLAG_C, (value & 0x80) != 0);
            return result;
        });
        break;
    case Operation::ROR:
        rmw_operand_exact(info.mode, [this](uint8_t value) {
            const uint8_t result = static_cast<uint8_t>((value >> 1) | (flag(FLAG_C) ? 0x80 : 0));
            set_flag(FLAG_C, (value & 0x01) != 0);
            return result;
        });
        break;
    case Operation::TAX:
        dummy_to(state_.pc);
        state_.x = state_.a;
        set_zn(state_.x);
        break;
    case Operation::TAY:
        dummy_to(state_.pc);
        state_.y = state_.a;
        set_zn(state_.y);
        break;
    case Operation::TXA:
        dummy_to(state_.pc);
        state_.a = state_.x;
        set_zn(state_.a);
        break;
    case Operation::TYA:
        dummy_to(state_.pc);
        state_.a = state_.y;
        set_zn(state_.a);
        break;
    case Operation::TSX:
        dummy_to(state_.pc);
        state_.x = state_.sp;
        set_zn(state_.x);
        break;
    case Operation::TXS:
        dummy_to(state_.pc);
        state_.sp = state_.x;
        break;
    case Operation::INX:
        dummy_to(state_.pc);
        state_.x = static_cast<uint8_t>(state_.x + 1);
        set_zn(state_.x);
        break;
    case Operation::INY:
        dummy_to(state_.pc);
        state_.y = static_cast<uint8_t>(state_.y + 1);
        set_zn(state_.y);
        break;
    case Operation::DEX:
        dummy_to(state_.pc);
        state_.x = static_cast<uint8_t>(state_.x - 1);
        set_zn(state_.x);
        break;
    case Operation::DEY:
        dummy_to(state_.pc);
        state_.y = static_cast<uint8_t>(state_.y - 1);
        set_zn(state_.y);
        break;
    case Operation::PHA:
        dummy_to(state_.pc);
        stack_write(state_.a);
        break;
    case Operation::PHP:
        dummy_to(state_.pc);
        stack_write(static_cast<uint8_t>(state_.p | FLAG_B | FLAG_U));
        break;
    case Operation::PLA:
        dummy_to(state_.pc);
        (void)stack_read(static_cast<uint8_t>(state_.sp + 1));
        state_.a = stack_pull();
        set_zn(state_.a);
        break;
    case Operation::PLP:
        dummy_to(state_.pc);
        (void)stack_read(static_cast<uint8_t>(state_.sp + 1));
        state_.p = normalized_p(stack_pull());
        break;
    case Operation::CLC:
        dummy_to(state_.pc);
        set_flag(FLAG_C, false);
        break;
    case Operation::SEC:
        dummy_to(state_.pc);
        set_flag(FLAG_C, true);
        break;
    case Operation::CLI:
        dummy_to(state_.pc);
        set_flag(FLAG_I, false);
        break;
    case Operation::SEI:
        dummy_to(state_.pc);
        set_flag(FLAG_I, true);
        break;
    case Operation::CLV:
        dummy_to(state_.pc);
        set_flag(FLAG_V, false);
        break;
    case Operation::CLD:
        dummy_to(state_.pc);
        set_flag(FLAG_D, false);
        break;
    case Operation::SED:
        dummy_to(state_.pc);
        set_flag(FLAG_D, true);
        break;
    case Operation::JMP:
        if (info.mode == AddressingMode::Indirect) {
            const uint16_t pointer = read16_at(state_.pc);
            state_.pc = read16_nmos_at(pointer);
        } else {
            state_.pc = read16_at(state_.pc);
        }
        break;
    case Operation::JSR: {
        const uint8_t low = bus_read(state_.pc++);
        dummy_to(static_cast<uint16_t>(0x0100 | state_.sp));
        const uint8_t high = bus_read(state_.pc++);
        const uint16_t return_address = static_cast<uint16_t>(state_.pc - 1);
        stack_write(static_cast<uint8_t>(return_address >> 8));
        stack_write(static_cast<uint8_t>(return_address & 0x00FF));
        state_.pc = static_cast<uint16_t>((high << 8) | low);
        break;
    }
    case Operation::RTS: {
        dummy_to(state_.pc);
        (void)stack_read(static_cast<uint8_t>(state_.sp + 1));
        const uint8_t low = stack_pull();
        const uint8_t high = stack_pull();
        state_.pc = static_cast<uint16_t>((high << 8) | low);
        dummy_to(state_.pc);
        state_.pc = static_cast<uint16_t>(state_.pc + 1);
        break;
    }
    case Operation::BRK:
        (void)bus_read(state_.pc++);
        stack_write(static_cast<uint8_t>(state_.pc >> 8));
        stack_write(static_cast<uint8_t>(state_.pc & 0x00FF));
        stack_write(static_cast<uint8_t>(state_.p | FLAG_B | FLAG_U));
        set_flag(FLAG_I, true);
        state_.pc = read16_at(0xFFFE);
        break;
    case Operation::RTI: {
        dummy_to(state_.pc);
        (void)stack_read(static_cast<uint8_t>(state_.sp + 1));
        state_.p = normalized_p(stack_pull());
        const uint8_t low = stack_pull();
        const uint8_t high = stack_pull();
        state_.pc = static_cast<uint16_t>((high << 8) | low);
        break;
    }
    case Operation::NOP:
        if (info.mode == AddressingMode::Implied) {
            dummy_to(state_.pc);
        } else {
            uint16_t address = 0;
            (void)read_operand_exact(info.mode, address);
        }
        break;
    case Operation::BPL:
    case Operation::BMI:
    case Operation::BVC:
    case Operation::BVS:
    case Operation::BCC:
    case Operation::BCS:
    case Operation::BNE:
    case Operation::BEQ: {
        const int8_t offset = static_cast<int8_t>(bus_read(state_.pc++));
        bool condition = false;
        switch (info.operation) {
        case Operation::BPL:
            condition = !flag(FLAG_N);
            break;
        case Operation::BMI:
            condition = flag(FLAG_N);
            break;
        case Operation::BVC:
            condition = !flag(FLAG_V);
            break;
        case Operation::BVS:
            condition = flag(FLAG_V);
            break;
        case Operation::BCC:
            condition = !flag(FLAG_C);
            break;
        case Operation::BCS:
            condition = flag(FLAG_C);
            break;
        case Operation::BNE:
            condition = !flag(FLAG_Z);
            break;
        case Operation::BEQ:
            condition = flag(FLAG_Z);
            break;
        default:
            break;
        }
        if (condition) {
            const uint16_t old_pc = state_.pc;
            const uint16_t new_pc = static_cast<uint16_t>(state_.pc + offset);
            dummy_to(state_.pc);
            if (!same_page(old_pc, new_pc)) {
                dummy_to(static_cast<uint16_t>((old_pc & 0xFF00) | (new_pc & 0x00FF)));
            }
            state_.pc = new_pc;
        }
        break;
    }
    case Operation::SLO: {
        const uint8_t value = rmw_operand_exact(info.mode, [this](uint8_t old_value) {
            set_flag(FLAG_C, (old_value & 0x80) != 0);
            return static_cast<uint8_t>(old_value << 1);
        });
        state_.a = static_cast<uint8_t>(state_.a | value);
        set_zn(state_.a);
        break;
    }
    case Operation::RLA: {
        const uint8_t value = rmw_operand_exact(info.mode, [this](uint8_t old_value) {
            const uint8_t result = static_cast<uint8_t>((old_value << 1) | (flag(FLAG_C) ? 1 : 0));
            set_flag(FLAG_C, (old_value & 0x80) != 0);
            return result;
        });
        state_.a = static_cast<uint8_t>(state_.a & value);
        set_zn(state_.a);
        break;
    }
    case Operation::SRE: {
        const uint8_t value = rmw_operand_exact(info.mode, [this](uint8_t old_value) {
            set_flag(FLAG_C, (old_value & 0x01) != 0);
            return static_cast<uint8_t>(old_value >> 1);
        });
        state_.a = static_cast<uint8_t>(state_.a ^ value);
        set_zn(state_.a);
        break;
    }
    case Operation::RRA: {
        const uint8_t value = rmw_operand_exact(info.mode, [this](uint8_t old_value) {
            const uint8_t result = static_cast<uint8_t>((old_value >> 1) | (flag(FLAG_C) ? 0x80 : 0));
            set_flag(FLAG_C, (old_value & 0x01) != 0);
            return result;
        });
        adc(value);
        break;
    }
    case Operation::SAX:
        write_operand_exact(info.mode, static_cast<uint8_t>(state_.a & state_.x));
        break;
    case Operation::LAX: {
        uint16_t address = 0;
        const uint8_t value = read_operand_exact(info.mode, address);
        state_.a = value;
        state_.x = value;
        set_zn(value);
        break;
    }
    case Operation::DCP: {
        const uint8_t value = rmw_operand_exact(info.mode, [](uint8_t old_value) {
            return static_cast<uint8_t>(old_value - 1);
        });
        compare(state_.a, value);
        break;
    }
    case Operation::ISC: {
        const uint8_t value = rmw_operand_exact(info.mode, [](uint8_t old_value) {
            return static_cast<uint8_t>(old_value + 1);
        });
        sbc(value);
        break;
    }
    case Operation::Illegal:
        state_.pc = instruction_pc;
        return StepResult::IllegalOpcode;
    }

    return StepResult::Ok;
}

} // namespace j6510
