// ESP32-S2 Saola BASIC demo for the j6510 core.
//
// Serial console: 115200 baud over USB serial.
// LED control from BASIC: POKE 53248,0 turns the Saola RGB LED off,
// POKE 53248,1 turns it on.

#ifndef J6510_ENABLE_BLOCK_CACHE
#define J6510_ENABLE_BLOCK_CACHE 0
#endif

#include <array>

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

// Arduino defines DEC for Serial formatting and interrupts() as a macro.
// The CPU core uses Operation::DEC and Cpu6510::interrupts(), so clear those
// names before including the platform-neutral core.
#ifdef DEC
#undef DEC
#endif

#ifdef interrupts
#undef interrupts
#endif

#include "msbasic_osi_rom.h"

#include "../../src/cpu6510_bus.h"
#include "../../src/cpu6510_core.h"

#include "../../src/cpu6510_bus.cpp"
#include "../../src/cpu6510_opcode_table.cpp"
#include "../../src/cpu6510_core.cpp"

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
constexpr uint8_t kSaolaRgbLedPin = 18;

Adafruit_NeoPixel status_led(1, kSaolaRgbLedPin, NEO_GRB + NEO_KHZ800);

void set_led_register(uint8_t value) {
    if (value == 0) {
        status_led.clear();
    } else {
        status_led.setPixelColor(0, status_led.Color(0, 24, 0));
    }
    status_led.show();
}

class BasicDemoBus final : public Bus {
public:
    void initialize() {
        memory.fill(0);
        injected_input_ = "\r\r";
        injected_pos_ = 0;

        for (uint16_t i = 0; i < kMsBasicOsiRom_len; ++i) {
            memory[static_cast<uint16_t>(kBasicRomBase + i)] = kMsBasicOsiRom[i];
        }

        install_jump(kMonitorReadKey, kInputRoutine);
        install_jump(kMonitorWriteChar, kOutputRoutine);
        install_jump(kMonitorBreakCheck, kBreakCheckRoutine);
        memory[kMonitorLoad] = 0x60; // RTS: LOAD is not implemented in this demo.
        memory[kMonitorSave] = 0x60; // RTS: SAVE is not implemented in this demo.

        // MONRDKEY: return the next serial byte in A, or 0 if none is ready.
        // OSI BASIC ignores non-printing characters while building an input
        // line, so returning 0 lets the host keep polling without trapping the
        // CPU in a tight 6502-side wait loop.
        const uint8_t input_routine[] = {
            0xAD, low(kSerialInRegister), high(kSerialInRegister), // LDA $D003
            0x60,                                                  // RTS
        };
        load_bytes(kInputRoutine, input_routine, sizeof(input_routine));

        // MONCOUT: write A to serial output.
        const uint8_t output_routine[] = {
            0x8D, low(kSerialOutRegister), high(kSerialOutRegister), // STA $D001
            0x60,                                                    // RTS
        };
        load_bytes(kOutputRoutine, output_routine, sizeof(output_routine));

        // MONISCNTC: report no break key by returning with carry clear.
        const uint8_t break_check_routine[] = {0x18, 0x60}; // CLC; RTS
        load_bytes(kBreakCheckRoutine, break_check_routine, sizeof(break_check_routine));

        memory[kResetVector] = low(kBasicColdStart);
        memory[kResetVector + 1] = high(kBasicColdStart);
    }

    uint8_t read(uint16_t address) override {
        switch (address) {
        case kLedRegister:
            return led_value_;
        case kSerialStatusRegister:
            return serial_available() ? 1 : 0;
        case kSerialInRegister:
            return read_serial_byte();
        default:
            return memory[address];
        }
    }

    void write(uint16_t address, uint8_t value) override {
        switch (address) {
        case kLedRegister:
            led_value_ = value;
            set_led_register(value);
            return;
        case kSerialOutRegister:
            Serial.write(value);
            return;
        default:
            if (address >= kBasicRomBase &&
                address < static_cast<uint16_t>(kBasicRomBase + kMsBasicOsiRom_len)) {
                return;
            }
            memory[address] = value;
            return;
        }
    }

private:
    static constexpr uint8_t low(uint16_t value) {
        return static_cast<uint8_t>(value & 0xFF);
    }

    static constexpr uint8_t high(uint16_t value) {
        return static_cast<uint8_t>(value >> 8);
    }

    void load_bytes(uint16_t start, const uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            memory[static_cast<uint16_t>(start + i)] = data[i];
        }
    }

    void install_jump(uint16_t from, uint16_t to) {
        memory[from] = 0x4C; // JMP absolute
        memory[static_cast<uint16_t>(from + 1)] = low(to);
        memory[static_cast<uint16_t>(from + 2)] = high(to);
    }

    bool serial_available() const {
        return injected_pos_ < 2 || Serial.available() > 0;
    }

    uint8_t read_serial_byte() {
        if (injected_pos_ < 2) {
            return static_cast<uint8_t>(injected_input_[injected_pos_++]);
        }

        int value = Serial.read();
        if (value < 0) {
            return 0;
        }
        if (value == '\n') {
            return '\r';
        }
        return static_cast<uint8_t>(value);
    }

    std::array<uint8_t, 65536> memory{};
    const char* injected_input_ = "\r\r";
    uint8_t injected_pos_ = 0;
    uint8_t led_value_ = 0;
};

BasicDemoBus bus;
Cpu6510 cpu(bus, Cpu6510Config{false});

} // namespace

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
    }

    status_led.begin();
    status_led.setBrightness(32);
    set_led_register(0);

    bus.initialize();
    cpu.reset();

    Serial.println();
    Serial.println("j6510 BASIC demo");
    Serial.println("Use: POKE 53248,1  /  POKE 53248,0");
}

void loop() {
    const RunResult result = cpu.run(1000);
    if (result.result != StepResult::Ok) {
        Serial.print("CPU stopped: pc=$");
        Serial.println(result.stop_pc, HEX);
        delay(1000);
    }
    yield();
}
