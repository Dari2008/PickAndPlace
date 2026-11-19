/**
 * joystick.h - the panel joystick: X / Y from the ADS1115 (sensors.h), button on a Pi GPIO
 * -----------------------------------------------------------------------------
 *   JoyState j = joystick.read();   // x, y in -1 .. 1 after calibration, dead zone, invert, swap
 * Settings: joy_x_min/_center/_max, joy_y_*, joy_deadzone, joy_invert_x/_y, joy_swap.
 * Button: GPIO 17 (pin 11). begin(gpio, false): switch to GND, internal pull-up (pressed = LOW);
 * begin(gpio, true): the stick drives 3V3 when pressed, internal pull-down (pressed = HIGH). Without hardware:
 * centred and released (the UI can feed a simulated joystick, see setSimulated()).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <atomic>
#include <cstdint>

struct JoyState
{
    float x = 0, y = 0; // -1 .. 1, 0 inside the dead zone
    bool button = false;
    int16_t rawX = 0, rawY = 0;
};

class Joystick
{
public:
    void begin(unsigned buttonGpio, bool activeHigh);
    JoyState read();
    void setSimulated(float x, float y, bool button); // keyboard / tests
    static float axis(float raw, float mn, float ctr, float mx, float deadzonePct, bool invert);

private:
    bool hw_ = false, activeHigh_ = false;
    std::atomic<float> simX_{0}, simY_{0};
    std::atomic<bool> simB_{false};
};

extern Joystick joystick;
