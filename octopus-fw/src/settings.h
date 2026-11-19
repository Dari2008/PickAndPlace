/**
 * settings.h - machine values of the Octopus (SET / GET)
 * -----------------------------------------------------------------------------
 * The Pi is the owner of all settings: it sends the machine values with SET on
 * every connect. The firmware starts with safe defaults and keeps them in RAM.
 *
 *   SET steps_x 80         SET tmc_cur_e1 700       SET z_home flag
 *   GET                    -> "set key=steps_x value=80" for every key
 *
 * Per axis (suffix x y z rot paste e0 e1 x2):
 *   steps_   steps per unit at the configured microstepping (mm, deg; paste: mm of plunger)
 *   speed_   top speed, units/s          accel_   acceleration, units/s^2
 *   invert_  1 = flip the DIR pin        test_speed_  MOTOR test speed, motor-shaft deg/s
 *   tmc_cur_ run current mA (RMS)        tmc_hold_    hold current % of run
 *   tmc_ms_  microsteps 1..256           tmc_stealth_ 1 = StealthChop, 0 = SpreadCycle
 *   tmc_sg_  StallGuard threshold SGTHRS (0..255, 0 = off; DIAG fires when SG_RESULT <= 2 * SGTHRS)
 * X2 follows X: only its invert_ and tmc_cur_ / _hold_ / _stealth_ / _sg_ do anything, microsteps are those of X.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include "pap_board.h"

namespace cfg
{
    // Per-axis groups
    enum AxisKey : uint8_t
    {
        STEPS, SPEED, ACCEL, INVERT, TEST_SPEED, TMC_CUR, TMC_HOLD, TMC_MS, TMC_STEALTH, TMC_SG, N_AXIS_KEYS
    };
    // Global keys
    enum Key : uint8_t
    {
        TRAVEL_X, TRAVEL_Y, TRAVEL_E1,
        XY_HOME,       // 0 none, 1 switch (DIAG0 / 1 / 7), 2 sensorless (DIAG jumpers 0 / 1 / 7)
        HOME_SPEED, HOME_SLOW, HOME_PULLOFF,
        HOME_CUR,      // X + X2 current while seeking a stall, % of tmc_cur_
        HOME_BLANK,    // mm at the start of a sensorless seek where stalls are ignored
        HOME_SKEW_MAX, // mm one X side may run on after the other stopped, more = homing fails
        X2_OFFSET,     // mm X2 moves alone after homing: squares the gantry if the two stops are not
        Z_HOME,        // 0 none (level by hand), 1 flag on DIAG2
        Z_TRAVEL, Z_FLAG_SPEED, Z_FLAG_SLOW, Z_FLAG_BACKOFF,
        E1_HOME,       // 0 none, 1 switch on DIAG6, 2 sensorless (DIAG jumper 6)
        E1_HOME_DIR,   // -1 = home towards E1 min, +1 = towards max
        E1_HOME_SPEED, E1_HOME_PULLOFF,
        ESTOP_INPUT,   // input number 0..7 of the e-stop button, -1 = none
        WATCHDOG_MS,   // stop when nothing arrives for this long while busy (0 = off)
        PUMP_POWER, SYRINGE_D, PASTE_SPEED, ROT_SPEED, E0_SPEED, E1_SPEED, JOG_SPEED,
        JUNCTION_DEV,
        IN_INVERT_0, IN_INVERT_1, IN_INVERT_2, IN_INVERT_3, IN_INVERT_4, IN_INVERT_5, IN_INVERT_6, IN_INVERT_7,
        LIGHT_TOP_IDLE, // % of the top light when nothing else asks (it is always on)
        CAM_FAN,        // % of the head camera fan (FAN2), always on
        BOX_FAN,        // % of the housing fan (FAN3), always on
        PI_FAN,         // % of the Raspberry Pi housing fan (FAN4), always on
        NEO_COUNT, NEO_R, NEO_G, NEO_B, // bottom light NeoPixel ring: LEDs, colour at full brightness
        LC_SCALE,   // load cell: HX711 counts per gram (LOADCELL CAL sets it; sign does not matter)
        LC_TRIGGER, // load cell: PROBE z= stops at this force, grams
        PUMP_USED,  // 1 = a vacuum pump is connected (FAN5), 0 = none: PUMP keeps the output off
        N_KEYS
    };

    void init(); // defaults (called once at start, and by DEFAULTS)

    float get(Key k);
    float axis(AxisKey k, uint8_t axis);

    // SET: value is a number or one of the option words (z_home: none|flag, ...).
    // Returns false with err filled when the key is unknown or the value is bad.
    // `changed` receives the global index of the setting (for apply hooks).
    bool set(const char *key, const char *value, std::string &err, int *changed = nullptr);

    size_t count();
    const char *name(size_t i);
    std::string text(size_t i); // value as written by GET
    bool isAxisKey(size_t i, AxisKey &k, uint8_t &axis);

    int find(const char *key); // global index or -1
}
