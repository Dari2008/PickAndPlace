/**
 * input.h - the panel joystick as the UI's "knob" (same API as the ESP32 rotary encoder)
 * -----------------------------------------------------------------------------
 *   up / down     focus steps (repeating, faster the further it is pushed)
 *   left (flick)  back (like holding the knob)
 *   button        press; held joy_hold_ms (0.8 s) = hold
 * Screens that jog the machine with the stick (Move, Teach, Cameras) switch the
 * steps off (setSteps(false)) and read stick() for proportional moves.
 * The stick itself: ../../dev/joystick.h (ADS1115, calibration in Settings).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <atomic>
#include <cstdint>
#include "../../dev/joystick.h"

enum ButtonEvent : uint8_t
{
    BUTTON_NONE,
    BUTTON_PRESS, // released before HOLD_MS
    BUTTON_HOLD   // held for HOLD_MS (fires once while still held)
};

class Input
{
public:
    void begin();
    void update();
    int8_t takeSteps();
    ButtonEvent takeButton();
    bool buttonDown() const { return down_; }

    void setSteps(bool on) { stepsOn_ = on; }
    JoyState stick() const { return last_; }
    // From the website's display mirror (any thread)
    void inject(int8_t steps, ButtonEvent b);

private:
    bool down_ = false;
    bool holdSent_ = false;
    uint32_t changedAt_ = 0, downAt_ = 0;
    bool raw_ = false;
    std::atomic<uint8_t> pending_{BUTTON_NONE};

    JoyState last_;
    bool stepsOn_ = true;
    std::atomic<int> steps_{0};
    int8_t dir_ = 0;
    uint32_t nextStep_ = 0;
    bool leftArmed_ = true;
};

extern Input input;
