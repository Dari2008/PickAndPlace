/**
 * toolbus.h - slot sensors of the nozzle rack, one byte over one wire
 * -----------------------------------------------------------------------------
 * The tool changer board (../../ToolChanger, CH32V003) sends one byte 5 times a second, one bit per sensor, on
 * the TFT header RX (PA10), 9600 8N1. Which bit is which slot is the Pi's business (nozzle_bit_n, NOZZLE LEARN).
 *
 * There is no framing and no checksum, so a value only counts once it arrived twice in a row: one damaged byte
 * can not fake an empty slot. With no good value for STALE_MS the state is not fresh, and the Pi must not trust it.
 * Worst case from a sensor changing to the new value here: 2 periods + 1 byte, about 410 ms.
 * The struct has no hardware in it, test/test_native runs it on the PC.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>

#define TOOLBUS_BAUD 9600 // must match ToolChanger/src/main.cpp

struct ToolBus
{
    static constexpr uint32_t STALE_MS = 1000;

    void feed(uint8_t b, uint32_t now)
    {
        if (have_ && b == last_)
        {
            value_ = b;
            seen_ = now;
            valid_ = true;
        }
        last_ = b;
        have_ = true;
    }
    bool fresh(uint32_t now) const { return valid_ && now - seen_ < STALE_MS; }
    uint8_t value(uint32_t now) const { return fresh(now) ? value_ : 0; }

private:
    uint8_t last_ = 0, value_ = 0;
    bool have_ = false, valid_ = false;
    uint32_t seen_ = 0;
};

#ifndef PAP_NATIVE
void toolbus_begin();
void toolbus_poll();     // main loop
bool toolbus_fresh();    // false: the board is silent, do not use the bits
uint8_t toolbus_value(); // bit n = sensor n, raw; 0 when not fresh
#endif
