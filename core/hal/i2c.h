/**
 * i2c.h - Linux i2c-dev access (/dev/i2c-1 = Pi header pins 3 SDA, 5 SCL)
 * -----------------------------------------------------------------------------
 *   I2cBus bus;  bus.open("/dev/i2c-1");
 *   bus.writeReg(0x48, 0x01, buf, 2);  bus.readReg(0x76, 0xF7, buf, 6);
 * One mutex per bus: several devices / threads can share it.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

class I2cBus
{
public:
    ~I2cBus() { close(); }
    bool open(const std::string &dev);
    void close();
    bool isOpen() const { return fd_ >= 0; }
    bool writeReg(uint8_t addr, uint8_t reg, const uint8_t *data, size_t n);
    bool readReg(uint8_t addr, uint8_t reg, uint8_t *data, size_t n);

private:
    int fd_ = -1;
    std::mutex m_;
};
