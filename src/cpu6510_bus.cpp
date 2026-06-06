#include "cpu6510_bus.h"

namespace j6510 {

uint8_t RamBus::read(uint16_t address) {
    return memory[address];
}

void RamBus::write(uint16_t address, uint8_t value) {
    memory[address] = value;
}

void RamBus::load(uint16_t start, const uint8_t* data, uint16_t size) {
    for (uint16_t i = 0; i < size; ++i) {
        memory[static_cast<uint16_t>(start + i)] = data[i];
    }
}

void RamBus::set_reset_vector(uint16_t address) {
    set_vector(0xFFFC, address);
}

void RamBus::set_nmi_vector(uint16_t address) {
    set_vector(0xFFFA, address);
}

void RamBus::set_irq_brk_vector(uint16_t address) {
    set_vector(0xFFFE, address);
}

void RamBus::set_vector(uint16_t vector_address, uint16_t target) {
    memory[vector_address] = static_cast<uint8_t>(target & 0x00FF);
    memory[static_cast<uint16_t>(vector_address + 1)] = static_cast<uint8_t>(target >> 8);
}

} // namespace j6510
