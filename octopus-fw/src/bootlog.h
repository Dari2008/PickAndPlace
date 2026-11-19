/**
 * bootlog.h - start-up diagnostics on the Octopus microSD card (BOOTLOG.TXT)
 * -----------------------------------------------------------------------------
 * Every start appends: reset cause, the clock state the bootloader left and what
 * SystemClock_Config made of it, each setup step, then the USB state 1 / 3 / 10 / 30 s
 * after the start. A HardFault is noted (with the faulting address, look it up with
 * arm-none-eabi-addr2line -e .pio/build/<env>/firmware.elf <pc>) in an RTC backup register
 * and written at the next start: press the reset button, don't power off (no battery).
 * No card, or a card that can't be written: nothing happens. Starts over above 32 KB.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>

// Filled by SystemClock_Config (clock.cpp) before anything else runs
struct ClockReport
{
    uint32_t entryCr, entryCfgr, entryPll; // RCC registers as the bootloader left them
    uint32_t oscRc, clkRc;                 // HAL_RCC_OscConfig / ClockConfig result (0 = ok)
};
extern ClockReport clockReport;

namespace bootlog
{
    void begin();                // first thing in setup(): header, reset cause, clocks
    void step(const char *what); // one line with the time since start
    void poll();                 // from loop(): USB state at 1 / 3 / 10 / 30 s
}
