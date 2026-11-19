/**
 * loadcell.h - load cell (HX711) for touching off the nozzle tip: PROBE z=, LOADCELL
 * -----------------------------------------------------------------------------
 * HX711 board on EXP1, one connector for everything: DT = EXP1 pin 3 (PE9), SCK = EXP1 pin 4 (PE10),
 * GND = EXP1 pin 9, VCC = 3.3 V from a small LDO (MCP1700-3302 / AMS1117-3.3) fed from EXP1 pin 10 (5 V).
 * The HX711 logic runs at its supply, 3.3 V matches the STM32. An LDO, not a buck: switching ripple is
 * noise in a 24-bit reading. (Was EXP2 + the I2C header's 3.3 V until 8 Nov 2026: a loose jumper.)
 * Tie the HX711's RATE pin to VCC for 80 samples/s (most boards have a jumper or pad); at 10 /s the
 * touch-off only gets slower, not wrong.
 *
 * Samples are read without blocking in loadcell::poll() (main loop and while motion blocks): each SCK
 * high pulse is ~1 us with interrupts off, so the step ISR runs freely in between. While armed, a force
 * above lc_trigger grams (either direction) sets the virtual input IN_LOADCELL, and the step ISR stops
 * the probing axis on its next step (same mechanism as the feeder switch).
 *
 *   LOADCELL               loadcell ok=1 g=12.3 raw=84213 tare=80000 scale=420.0 rate=80
 *   LOADCELL TARE          zero = the current reading
 *   LOADCELL CAL <grams>   put a known weight on it (after TARE): sets lc_scale
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>

namespace loadcell
{
    void begin();
    void poll();            // reads a sample when one is ready
    bool present();         // samples arrived in the last 500 ms
    int32_t raw();          // last sample
    int32_t tareRaw();
    float grams();          // (raw - tare) / lc_scale
    float rate();           // samples per second (measured)
    bool tare(uint16_t ms = 300); // average for `ms` (blocking, polls); false without samples
    void arm();             // tare, then trigger IN_LOADCELL above lc_trigger
    void disarm();
    float peak();           // largest |grams| while armed
}
