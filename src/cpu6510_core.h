#pragma once

#include "cpu6510_bus.h"
#include "cpu6510_opcode_table.h"

#include <cstdint>
#include <functional>
#include <array>

namespace j6510 {

constexpr uint8_t FLAG_C = 0x01;
constexpr uint8_t FLAG_Z = 0x02;
constexpr uint8_t FLAG_I = 0x04;
constexpr uint8_t FLAG_D = 0x08;
constexpr uint8_t FLAG_B = 0x10;
constexpr uint8_t FLAG_U = 0x20;
constexpr uint8_t FLAG_V = 0x40;
constexpr uint8_t FLAG_N = 0x80;

struct Cpu6510State {
    uint8_t a = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t sp = 0xFD;
    uint16_t pc = 0;
    uint8_t p = FLAG_U | FLAG_I;
};

struct InterruptState {
    bool reset_pending = false;
    bool nmi_pending = false;
    bool irq_level = false;
};

struct Port6510State {
    uint8_t ddr = 0x2F;
    uint8_t data = 0x37;
    uint8_t external_inputs = 0xFF;
    uint8_t active_mask = 0x3F;
};

struct Cpu6510Config {
    bool port_enabled = true;
};

class Cpu6510;
using InterruptPollCallback = std::function<void(Cpu6510&)>;

enum class StepResult {
    Ok,
    IllegalOpcode,
};

struct RunResult {
    StepResult result = StepResult::Ok;
    uint32_t instructions_executed = 0;
    uint16_t stop_pc = 0;
};

enum class RunStopReason {
    BudgetExhausted,
    ControlFlow,
    InterruptPending,
    IllegalOpcode,
};

struct BlockRunResult {
    StepResult result = StepResult::Ok;
    RunStopReason stop_reason = RunStopReason::BudgetExhausted;
    uint32_t instructions_executed = 0;
    uint16_t start_pc = 0;
    uint16_t stop_pc = 0;
};

struct BlockCacheStats {
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t invalidations = 0;
};

class Cpu6510 {
public:
    explicit Cpu6510(Bus& bus);
    Cpu6510(Bus& bus, Cpu6510Config config);

    Cpu6510State& state();
    const Cpu6510State& state() const;
    const InterruptState& interrupts() const;
    const Port6510State& port() const;

    void request_reset();
    void pulse_nmi();
    void set_irq_level(bool active);
    void clear_irq_level();
    void poll_target_interrupts();
    void set_interrupt_poll_callback(InterruptPollCallback callback);
    void service_pending_interrupt_if_needed();

    void reset();
    StepResult step();
    RunResult run(uint32_t max_instructions);
    BlockRunResult run_block(uint32_t max_instructions);
    RunResult run_cached(uint32_t max_instructions);
    const BlockCacheStats& block_cache_stats() const;
    void clear_block_cache();

    void set_port_external_inputs(uint8_t value);
    void set_port_active_mask(uint8_t mask);
    uint8_t port_output() const;
    void set_port_changed_callback(std::function<void(uint8_t)> callback);

    void push(uint8_t value);
    uint8_t pull();

private:
    Bus& bus_;
    uint8_t* direct_memory_ = nullptr;
    Cpu6510Config config_{};
    Cpu6510State state_{};
    InterruptState interrupts_{};
    Port6510State port_{};
    std::function<void(uint8_t)> port_changed_callback_{};
    InterruptPollCallback interrupt_poll_callback_{};
    enum class CachedOpKind : uint8_t {
        Fallback,
        LdaImm,
        LdxImm,
        LdyImm,
        LdaAbs,
        LdxAbs,
        LdaZpX,
        LdaAbsY,
        StaAbs,
        StxAbs,
        StaZp,
        StaZpX,
        StaAbsY,
        Tax,
        Txa,
        Inx,
        Dex,
        Dey,
        Nop,
        Bne,
        JmpAbs,
    };
    struct CachedOp {
        CachedOpKind kind = CachedOpKind::Fallback;
        uint16_t operand = 0;
    };
    struct CachedBlock {
        bool valid = false;
        bool executable = false;
        uint16_t start_pc = 0;
        uint8_t count = 0;
        uint8_t page_start = 0;
        uint8_t page_end = 0;
        uint8_t lengths[32] = {};
        uint8_t opcodes[32] = {};
        CachedOp ops[32] = {};
        RunStopReason terminator = RunStopReason::BudgetExhausted;
    };
    std::array<CachedBlock, 256> block_cache_{};
    std::array<uint16_t, 256> cached_page_use_count_{};
    uint16_t valid_cached_blocks_ = 0;
    BlockCacheStats block_cache_stats_{};

    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);
    void invalidate_block_cache_for_write(uint16_t address);
    void add_block_to_page_counts(const CachedBlock& block);
    void remove_block_from_page_counts(const CachedBlock& block);
    bool block_uses_page(const CachedBlock& block, uint8_t page) const;
    CachedBlock& block_cache_slot(uint16_t pc);
    CachedBlock decode_block(uint16_t pc);
    bool execute_cached_block(const CachedBlock& block, uint32_t remaining_budget, RunResult& result);
    bool decode_cached_op(uint8_t opcode, uint16_t operand, CachedOp& op) const;
    bool cached_write_is_safe_for_block(const CachedBlock& block, const CachedOp& op) const;
    void execute_cached_op(const CachedOp& op);
    uint8_t fetch8();
    uint16_t fetch16();
    uint16_t read16(uint16_t address);
    uint16_t read16_zp(uint8_t address);
    uint16_t read16_nmos_indirect(uint16_t address);

    void set_flag(uint8_t flag, bool enabled);
    bool flag(uint8_t flag) const;
    void set_zn(uint8_t value);
    uint8_t normalized_p(uint8_t value) const;
    bool is_port_address(uint16_t address) const;
    uint8_t read_port(uint16_t address) const;
    void write_port(uint16_t address, uint8_t value);
    void notify_port_if_changed(uint8_t old_output);
    bool can_use_fast_run_path() const;
    bool run_fast_instruction(StepResult& result);
    bool is_block_terminator(uint8_t opcode) const;
    bool has_pending_interrupt_work() const;
    uint8_t fast_fetch8();
    uint16_t fast_fetch16();
    uint16_t fast_zp_x();
    uint16_t fast_zp_y();
    uint16_t fast_abs_x();
    uint16_t fast_abs_y();
    uint16_t fast_indexed_indirect();
    uint16_t fast_indirect_indexed();
    void fast_branch_if(bool condition);
    uint8_t read_operand(AddressingMode mode);
    void write_operand(AddressingMode mode, uint8_t value);
    void compare(uint8_t lhs, uint8_t rhs);
    void adc(uint8_t value);
    void sbc(uint8_t value);
    void asl(AddressingMode mode);
    void lsr(AddressingMode mode);
    void rol(AddressingMode mode);
    void ror(AddressingMode mode);
    void branch_if(bool condition);
    void interrupt(uint16_t vector, bool break_flag, uint16_t return_pc);
    uint16_t operand_address(AddressingMode mode);
};

} // namespace j6510
