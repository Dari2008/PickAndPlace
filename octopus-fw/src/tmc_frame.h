/**
 * tmc_frame.h - TMC2209 UART datagrams and register values (no hardware access)
 * -----------------------------------------------------------------------------
 * Datasheet TMC2209 rev 1.09, chapter 4 (UART) and 5 (registers).
 *   write: 0x05, addr, reg | 0x80, data[31:24] .. data[7:0], CRC   (8 bytes)
 *   read : 0x05, addr, reg, CRC                                    (4 bytes)
 *   reply: 0x05, 0xFF, reg, data[31:24] .. data[7:0], CRC          (8 bytes)
 * The Octopus gives every slot its own UART line, so the address is always 0
 * (MS1 / MS2 of the module pulled low).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>

namespace tmc
{
    // Registers
    constexpr uint8_t GCONF = 0x00, GSTAT = 0x01, IFCNT = 0x02, IOIN = 0x06, IHOLD_IRUN = 0x10,
                      TPOWERDOWN = 0x11, TSTEP = 0x12, TPWMTHRS = 0x13, TCOOLTHRS = 0x14, SGTHRS = 0x40,
                      SG_RESULT = 0x41, COOLCONF = 0x42, MSCNT = 0x6A, CHOPCONF = 0x6C, DRV_STATUS = 0x6F,
                      PWMCONF = 0x70;

    uint8_t crc8(const uint8_t *data, uint8_t len);
    void writeFrame(uint8_t out[8], uint8_t addr, uint8_t reg, uint32_t value);
    void readFrame(uint8_t out[4], uint8_t addr, uint8_t reg);
    // Check a reply; false on bad sync / CRC / register.
    bool parseReply(const uint8_t in[8], uint8_t reg, uint32_t &value);

    struct Config
    {
        float currentMa = 600;  // RMS run current
        float holdPercent = 50; // hold current, % of run
        uint16_t microsteps = 16;
        bool stealth = true;
        uint8_t sgthrs = 0;     // StallGuard threshold (0 = off)
        float rsense = 0.11f;   // ohms on the usual StepStick modules
    };

    // Register values for a configuration
    uint32_t gconf(const Config &c);
    uint32_t chopconf(const Config &c);
    uint32_t iholdIrun(const Config &c);
    // Current scale CS (0..31) and vsense for `ma`; returns the real current in mA
    float currentScale(float ma, float rsense, uint8_t &cs, bool &vsense);
    uint8_t mres(uint16_t microsteps); // 256 -> 0, 128 -> 1 ... 1 -> 8 (nearest power of two)
}
