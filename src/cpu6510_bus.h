#pragma once

#include <array>
#include <cstdint>

namespace j6510 {

class Bus {
public:
    virtual ~Bus() = default;

    virtual uint8_t read(uint16_t address) = 0;
    virtual void write(uint16_t address, uint8_t value) = 0;
    virtual uint8_t* direct_memory();
};

class RamBus final : public Bus {
public:
    uint8_t read(uint16_t address) override;
    void write(uint16_t address, uint8_t value) override;
    uint8_t* direct_memory() override;

    void load(uint16_t start, const uint8_t* data, uint16_t size);
    void set_reset_vector(uint16_t address);
    void set_nmi_vector(uint16_t address);
    void set_irq_brk_vector(uint16_t address);

    std::array<uint8_t, 65536> memory{};

private:
    void set_vector(uint16_t vector_address, uint16_t target);
};

} // namespace j6510
