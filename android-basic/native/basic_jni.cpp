#include <jni.h>

#include "cpu6510_bus.h"
#include "cpu6510_core.h"
#include "msbasic_osi_rom.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

using namespace j6510;

namespace {

constexpr uint16_t kBasicRomBase = 0xA000;
constexpr uint16_t kBasicColdStart = 0xBD11;
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

class AndroidBasicBus final : public Bus {
public:
    explicit AndroidBasicBus(std::atomic<bool>& break_requested)
        : break_requested_(break_requested) {
    }

    void initialize() {
        memory_.fill(0);
        input_.clear();
        output_.clear();
        previous_output_was_cr_ = false;

        for (uint16_t i = 0; i < kMsBasicOsiRom_len; ++i) {
            memory_[static_cast<uint16_t>(kBasicRomBase + i)] = kMsBasicOsiRom[i];
        }

        install_jump(kMonitorReadKey, kInputRoutine);
        install_jump(kMonitorWriteChar, kOutputRoutine);
        install_jump(kMonitorBreakCheck, kBreakCheckRoutine);
        memory_[kMonitorLoad] = 0x60;
        memory_[kMonitorSave] = 0x60;

        const uint8_t input_routine[] = {
            0xAD, low(kSerialInRegister), high(kSerialInRegister),
            0x60,
        };
        load(kInputRoutine, input_routine, sizeof(input_routine));

        const uint8_t output_routine[] = {
            0x8D, low(kSerialOutRegister), high(kSerialOutRegister),
            0x60,
        };
        load(kOutputRoutine, output_routine, sizeof(output_routine));

        memory_[kBreakCheckRoutine] = 0x18;
        memory_[static_cast<uint16_t>(kBreakCheckRoutine + 1)] = 0x60;
        memory_[kResetVector] = low(kBasicColdStart);
        memory_[static_cast<uint16_t>(kResetVector + 1)] = high(kBasicColdStart);

        enqueue_raw("\r\r");
    }

    uint8_t read(uint16_t address) override {
        if (address == kBreakCheckRoutine) {
            if (break_requested_.exchange(false)) {
                break_sequence_active_ = true;
                return 0xA9; // LDA #$03
            }
            return 0x18; // CLC: no break key
        }
        if (break_sequence_active_) {
            if (address == static_cast<uint16_t>(kBreakCheckRoutine + 1)) {
                return 0x03;
            }
            if (address == static_cast<uint16_t>(kBreakCheckRoutine + 2)) {
                return 0x38; // SEC: break key available
            }
            if (address == static_cast<uint16_t>(kBreakCheckRoutine + 3)) {
                break_sequence_active_ = false;
                return 0x60; // RTS
            }
        }
        if (address == kSerialStatusRegister) {
            return input_.empty() ? 0 : 1;
        }
        if (address == kSerialInRegister) {
            if (input_.empty()) {
                return 0;
            }
            const uint8_t value = input_.front();
            input_.pop_front();
            return value;
        }
        return memory_[address];
    }

    void write(uint16_t address, uint8_t value) override {
        if (address == kSerialOutRegister) {
            append_output(value);
            return;
        }
        if (address >= kBasicRomBase
                && address < static_cast<uint16_t>(kBasicRomBase + kMsBasicOsiRom_len)) {
            return;
        }
        memory_[address] = value;
    }

    void enqueue(const std::string& text) {
        enqueue_keys(text);
        input_.push_back('\r');
    }

    void enqueue_keys(const std::string& text) {
        for (unsigned char value : text) {
            if (value == '\n') {
                input_.push_back('\r');
            } else if (value != '\r') {
                if (value >= 'a' && value <= 'z') {
                    value = static_cast<unsigned char>(value - ('a' - 'A'));
                }
                input_.push_back(value);
            }
        }
    }

    std::string take_output() {
        std::string result;
        result.swap(output_);
        return result;
    }

private:
    static constexpr uint8_t low(uint16_t value) {
        return static_cast<uint8_t>(value & 0xFF);
    }

    static constexpr uint8_t high(uint16_t value) {
        return static_cast<uint8_t>(value >> 8);
    }

    void load(uint16_t start, const uint8_t* bytes, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            memory_[static_cast<uint16_t>(start + i)] = bytes[i];
        }
    }

    void install_jump(uint16_t from, uint16_t to) {
        memory_[from] = 0x4C;
        memory_[static_cast<uint16_t>(from + 1)] = low(to);
        memory_[static_cast<uint16_t>(from + 2)] = high(to);
    }

    void enqueue_raw(const char* text) {
        while (*text != '\0') {
            input_.push_back(static_cast<uint8_t>(*text++));
        }
    }

    void append_output(uint8_t value) {
        value &= 0x7F;
        if (value == '\r') {
            output_.push_back('\n');
            previous_output_was_cr_ = true;
            return;
        }
        if (value == '\n' && previous_output_was_cr_) {
            previous_output_was_cr_ = false;
            return;
        }
        previous_output_was_cr_ = false;
        if (value == 0x08 || value == 0x7F) {
            if (!output_.empty() && output_.back() != '\n') {
                output_.pop_back();
            }
            return;
        }
        if (value == '\n' || value == '\t' || value >= 0x20) {
            output_.push_back(static_cast<char>(value));
        }
    }

    std::array<uint8_t, 65536> memory_{};
    std::deque<uint8_t> input_{};
    std::string output_{};
    bool previous_output_was_cr_ = false;
    bool break_sequence_active_ = false;
    std::atomic<bool>& break_requested_;
};

class BasicMachine {
public:
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        break_requested_.store(false);
        bus_ = std::make_unique<AndroidBasicBus>(break_requested_);
        bus_->initialize();
        cpu_ = std::make_unique<Cpu6510>(*bus_, Cpu6510Config{false});
        cpu_->reset();
    }

    void send(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        ensure_initialized();
        bus_->enqueue(text);
    }

    void type(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        ensure_initialized();
        bus_->enqueue_keys(text);
    }

    std::string pump(uint32_t instruction_budget) {
        std::lock_guard<std::mutex> lock(mutex_);
        ensure_initialized();
        const RunResult result = cpu_->run(instruction_budget);
        std::string output = bus_->take_output();
        if (result.result != StepResult::Ok) {
            output += "\n?CPU ERROR AT $";
            static constexpr char hex[] = "0123456789ABCDEF";
            output.push_back(hex[(result.stop_pc >> 12) & 0x0F]);
            output.push_back(hex[(result.stop_pc >> 8) & 0x0F]);
            output.push_back(hex[(result.stop_pc >> 4) & 0x0F]);
            output.push_back(hex[result.stop_pc & 0x0F]);
            output += "\n";
        }
        return output;
    }

    void request_break() {
        break_requested_.store(true);
    }

private:
    void ensure_initialized() {
        if (!cpu_) {
            bus_ = std::make_unique<AndroidBasicBus>(break_requested_);
            bus_->initialize();
            cpu_ = std::make_unique<Cpu6510>(*bus_, Cpu6510Config{false});
            cpu_->reset();
        }
    }

    std::mutex mutex_{};
    std::atomic<bool> break_requested_{false};
    std::unique_ptr<AndroidBasicBus> bus_{};
    std::unique_ptr<Cpu6510> cpu_{};
};

BasicMachine machine;

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_pba77_j6510basic_MainActivity_nativeReset(JNIEnv*, jclass) {
    machine.reset();
}

extern "C" JNIEXPORT void JNICALL
Java_com_pba77_j6510basic_MainActivity_nativeSend(JNIEnv* env, jclass, jstring value) {
    if (value == nullptr) {
        machine.send("");
        return;
    }
    const char* utf = env->GetStringUTFChars(value, nullptr);
    if (utf != nullptr) {
        machine.send(utf);
        env->ReleaseStringUTFChars(value, utf);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_pba77_j6510basic_MainActivity_nativeType(JNIEnv* env, jclass, jstring value) {
    if (value == nullptr) {
        return;
    }
    const char* utf = env->GetStringUTFChars(value, nullptr);
    if (utf != nullptr) {
        machine.type(utf);
        env->ReleaseStringUTFChars(value, utf);
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_pba77_j6510basic_MainActivity_nativePump(JNIEnv* env, jclass, jint budget) {
    const uint32_t safe_budget = budget > 0 ? static_cast<uint32_t>(budget) : 1;
    const std::string output = machine.pump(safe_budget);
    return env->NewStringUTF(output.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_pba77_j6510basic_MainActivity_nativeBreak(JNIEnv*, jclass) {
    machine.request_break();
}
