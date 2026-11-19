/**
 * tmc.h - TMC2209 configuration over the Octopus single-wire UART lines
 * -----------------------------------------------------------------------------
 * Every driver slot has its own UART pin (the slot's CS/UART line, with the UART
 * jumper fitted). The line is bit-banged half duplex at 57600 baud with the
 * cycle counter: about 1.4 ms per write, interrupts are off for one byte
 * (~170 us) at a time. Only called while the machine stands still.
 *
 *   tmcs.begin();             // all slots: GCONF, current, microsteps, StallGuard
 *   tmcs.configure(E1_AXIS);  // after SET tmc_*_e1
 *   TmcStatus s; tmcs.status(X_AXIS, s);
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>
#include "pap_board.h"

struct TmcStatus
{
    bool ok = false;        // driver answered (UART works, jumper fitted)
    uint8_t version = 0;    // IOIN[31:24], 0x21 for the TMC2209
    float currentMa = 0;    // configured run current (after rounding)
    uint16_t microsteps = 0;
    uint16_t sgResult = 0;  // StallGuard value (0 = stalled ... 510)
    uint32_t drvStatus = 0; // raw DRV_STATUS
    bool overTemp = false, preWarn = false, shortA = false, shortB = false, openA = false, openB = false,
         stealth = false, standstill = false;
};

class TmcBus
{
public:
    void begin();               // configure every slot from the settings
    // GCONF, IHOLD_IRUN, CHOPCONF, TPOWERDOWN, TCOOLTHRS, SGTHRS. runPercent scales the current (homing)
    bool configure(uint8_t axis, float runPercent = 100.0f);
    bool status(uint8_t axis, TmcStatus &s);
    bool writeReg(uint8_t axis, uint8_t reg, uint32_t value);
    bool readReg(uint8_t axis, uint8_t reg, uint32_t &value);
    bool present(uint8_t axis) const { return present_[axis]; }

private:
    void sendBytes(uint8_t axis, const uint8_t *data, uint8_t len);
    bool receiveBytes(uint8_t axis, uint8_t *data, uint8_t len);
    bool present_[N_AXIS] = {false};
};

extern TmcBus tmcs;
