#include "joystick.h"
#include "sensors.h"
#include "../hal/gpio.h"
#include "../ctrl/settings/general_settings.h"

#include <cmath>

Joystick joystick;
static GpioLine g_button;

void Joystick::begin(unsigned buttonGpio, bool activeHigh)
{
    activeHigh_ = activeHigh;
    hw_ = g_button.input(GpioLine::chip(), buttonGpio, !activeHigh, activeHigh);
}

float Joystick::axis(float raw, float mn, float ctr, float mx, float deadzonePct, bool invert)
{
    float v;
    if (raw >= ctr)
        v = mx > ctr ? (raw - ctr) / (mx - ctr) : 0.0f;
    else
        v = ctr > mn ? (raw - ctr) / (ctr - mn) : 0.0f;
    if (v > 1)
        v = 1;
    if (v < -1)
        v = -1;
    const float dz = deadzonePct / 100.0f;
    if (fabsf(v) <= dz)
        v = 0;
    else
        v = (v > 0 ? v - dz : v + dz) / (1.0f - dz); // rescaled: just outside the dead zone = 0
    return invert ? -v : v;
}

JoyState Joystick::read()
{
    JoyState j;
    j.rawX = sensors.adc(0);
    j.rawY = sensors.adc(1);
    if (sensors.adsPresent())
    {
        float x = axis(j.rawX, settings.joyMin[0], settings.joyCenter[0], settings.joyMax[0], settings.joyDeadzone,
                       settings.joyInvert[0]);
        float y = axis(j.rawY, settings.joyMin[1], settings.joyCenter[1], settings.joyMax[1], settings.joyDeadzone,
                       settings.joyInvert[1]);
        if (settings.joySwap)
            std::swap(x, y);
        j.x = x;
        j.y = y;
    }
    else
    {
        j.x = simX_;
        j.y = simY_;
    }
    j.button = hw_ ? g_button.get() == activeHigh_ : simB_.load();
    return j;
}

void Joystick::setSimulated(float x, float y, bool b)
{
    simX_ = x;
    simY_ = y;
    simB_ = b;
}
