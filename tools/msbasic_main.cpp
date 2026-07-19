// Host-side Microsoft BASIC demo and real-workload benchmark.
//
// Wires the OSI Microsoft BASIC ROM (examples/j6510_basic_demo/msbasic_osi_rom.h)
// to a memory-mapped serial console on stdin/stdout, using the same machine
// model as the ESP32-S2 demo: ROM at $A000, cold start $BD11, monitor
// trampolines at $FFEB/$FFEE/$FFF1 and serial registers at $D001-$D003.
//
// Usage:
//   msbasic_demo [max_instructions] ["10 PRINT ...\n20 ..."]
// With no program argument a small FOR/NEXT benchmark runs. Output is printed
// live; cache and throughput statistics go to stderr at the end.

#include "cpu6510_bus.h"
#include "cpu6510_core.h"
#include "../examples/j6510_basic_demo/msbasic_osi_rom.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

using namespace j6510;

namespace {

constexpr uint16_t kBasicRomBase = 0xA000;
constexpr uint16_t kBasicColdStart = 0xBD11;

constexpr uint16_t kLedRegister = 0xD000;
constexpr uint16_t kSerialOutRegister = 0xD001;
constexpr uint16_t kSerialStatusRegister = 0xD002;
constexpr uint16_t kSerialInRegister = 0xD003;

constexpr uint16_t kInputRoutine = 0xD010;
constexpr uint16_t kOutputRoutine = 0xD020;
constexpr uint16_t kBreakCheckRoutine = 0xD030;

constexpr uint16_t kMonitorReadKey = 0xFFEB;
constexpr uint16_t kMonitorWriteChar = 0xFFEE;
constexpr uint16_t kMonitorBreakCheck = 0xFFF1;
constexpr uint16_t kMonitorLoad = 0xFFF4;
constexpr uint16_t kMonitorSave = 0xFFF7;

constexpr uint16_t kResetVector = 0xFFFC;

constexpr uint8_t low(uint16_t value) {
    return static_cast<uint8_t>(value & 0xFF);
}

constexpr uint8_t high(uint16_t value) {
    return static_cast<uint8_t>(value >> 8);
}

class BasicHostBus final : public Bus {
public:
    void initialize(const std::string& script) {
        memory_.fill(0);
        script_ = script;
        script_pos_ = 0;

        for (uint16_t i = 0; i < kMsBasicOsiRom_len; ++i) {
            memory_[static_cast<uint16_t>(kBasicRomBase + i)] = kMsBasicOsiRom[i];
        }

        install_jump(kMonitorReadKey, kInputRoutine);
        install_jump(kMonitorWriteChar, kOutputRoutine);
        install_jump(kMonitorBreakCheck, kBreakCheckRoutine);
        memory_[kMonitorLoad] = 0x60; // RTS: LOAD not implemented.
        memory_[kMonitorSave] = 0x60; // RTS: SAVE not implemented.

        const uint8_t input_routine[] = {
            0xAD, low(kSerialInRegister), high(kSerialInRegister), // LDA $D003
            0x60,                                                  // RTS
        };
        load_bytes(kInputRoutine, input_routine, sizeof(input_routine));

        const uint8_t output_routine[] = {
            0x8D, low(kSerialOutRegister), high(kSerialOutRegister), // STA $D001
            0x60,                                                    // RTS
        };
        load_bytes(kOutputRoutine, output_routine, sizeof(output_routine));

        const uint8_t break_check_routine[] = {0x18, 0x60}; // CLC; RTS
        load_bytes(kBreakCheckRoutine, break_check_routine, sizeof(break_check_routine));

        memory_[kResetVector] = low(kBasicColdStart);
        memory_[kResetVector + 1] = high(kBasicColdStart);
    }

    uint8_t read(uint16_t address) override {
        switch (address) {
        case kLedRegister:
            return led_value_;
        case kSerialStatusRegister:
            return input_available() ? 1 : 0;
        case kSerialInRegister:
            return read_input_byte();
        default:
            return memory_[address];
        }
    }

    void write(uint16_t address, uint8_t value) override {
        switch (address) {
        case kLedRegister:
            led_value_ = value;
            return;
        case kSerialOutRegister:
            std::fputc(value == '\r' ? '\n' : value, stdout);
            std::fflush(stdout);
            return;
        default:
            if (address >= kBasicRomBase &&
                address < static_cast<uint16_t>(kBasicRomBase + kMsBasicOsiRom_len)) {
                return;
            }
            memory_[address] = value;
            return;
        }
    }

private:
    void load_bytes(uint16_t start, const uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            memory_[static_cast<uint16_t>(start + i)] = data[i];
        }
    }

    void install_jump(uint16_t from, uint16_t to) {
        memory_[from] = 0x4C; // JMP absolute
        memory_[static_cast<uint16_t>(from + 1)] = low(to);
        memory_[static_cast<uint16_t>(from + 2)] = high(to);
    }

    bool input_available() {
        if (script_pos_ < script_.size()) {
            return true;
        }
        uint8_t byte = 0;
        if (::read(STDIN_FILENO, &byte, 1) == 1) {
            pending_ = byte == '\n' ? '\r' : byte;
            has_pending_ = true;
        }
        return has_pending_;
    }

    uint8_t read_input_byte() {
        if (script_pos_ < script_.size()) {
            return static_cast<uint8_t>(script_[script_pos_++]);
        }
        if (has_pending_) {
            has_pending_ = false;
            return pending_;
        }
        if (input_available()) {
            has_pending_ = false;
            return pending_;
        }
        return 0;
    }

    std::array<uint8_t, 65536> memory_{};
    std::string script_;
    size_t script_pos_ = 0;
    uint8_t pending_ = 0;
    bool has_pending_ = false;
    uint8_t led_value_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    const uint64_t max_instructions = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 200000000ULL;
    const std::string program =
        argc > 2 ? argv[2]
                 : "10 X=0\n20 FOR I=1 TO 50000\n30 X=X+I\n40 NEXT\n50 PRINT X\n60 PRINT \"DONE\"\n";

    // Poll stdin without ever blocking the emulated CPU on a read().
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    std::string script = "\r\r"; // accept the default memory size at cold start
    for (char c : program) {
        script += (c == '\n') ? '\r' : c;
    }
    script += "\rRUN\r";

    BasicHostBus bus;
    bus.initialize(script);
    Cpu6510 cpu(bus, Cpu6510Config{false});
    cpu.reset();

    const auto start = std::chrono::steady_clock::now();
    uint64_t executed = 0;
    while (executed < max_instructions) {
        RunResult result = cpu.run_cached(100000);
        executed += result.instructions_executed;
        if (result.result != StepResult::Ok) {
            std::fprintf(stderr, "\nillegal opcode at $%04x\n", result.stop_pc);
            return 1;
        }
    }
    const auto finish = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(finish - start).count();

    const BlockCacheStats& stats = cpu.block_cache_stats();
    const uint64_t total = stats.ir_instructions + stats.fallback_instructions;
    const double ir_share = total > 0 ? 100.0 * stats.ir_instructions / total : 0.0;
    std::fprintf(stderr, "\nexecuted: %llu instructions in %.2f s (%.1f M instr/s)\n",
                 (unsigned long long)executed, seconds, executed / seconds / 1e6);
    std::fprintf(stderr, "ir/fallback: %llu / %llu (%.1f%% in IR)\n",
                 (unsigned long long)stats.ir_instructions, (unsigned long long)stats.fallback_instructions,
                 ir_share);
    std::fprintf(stderr, "cache hits/misses/invalidations: %llu / %llu / %llu\n",
                 (unsigned long long)stats.hits, (unsigned long long)stats.misses,
                 (unsigned long long)stats.invalidations);
    return 0;
}
