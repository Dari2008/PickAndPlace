#include "backlight.h"
#include "display.h"
#include "../prefs.h"
#include <Arduino.h>

#include <algorithm>

Backlight backlight;

bool Backlight::available() { return display.hardware(); }

void Backlight::begin()
{
    bright_ = (uint8_t)std::clamp(prefs.getInt("bl_pct", 100), 5, 100);
    offMin_ = (uint16_t)prefs.getInt("bl_off", 15);
    lastActivity_ = millis();
    apply(bright_);
}

void Backlight::apply(uint8_t percent) { display.setBacklight(percent > 0); }

void Backlight::update()
{
    if (!off_ && offMin_ && millis() - lastActivity_ > offMin_ * 60000UL)
    {
        off_ = true;
        apply(0);
    }
}

bool Backlight::wake()
{
    lastActivity_ = millis();
    if (!off_)
        return false;
    off_ = false;
    apply(bright_);
    return true;
}

void Backlight::sleep()
{
    off_ = true;
    apply(0);
}

void Backlight::setBrightness(uint8_t percent)
{
    bright_ = (uint8_t)std::clamp((int)percent, 5, 100);
    if (!off_)
        apply(bright_);
    prefs.setInt("bl_pct", bright_);
}

void Backlight::setOffMinutes(uint16_t minutes)
{
    offMin_ = minutes;
    lastActivity_ = millis();
    prefs.setInt("bl_off", offMin_);
}
