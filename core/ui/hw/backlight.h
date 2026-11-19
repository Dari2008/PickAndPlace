/**
 * backlight.h - display LED (GPIO18): on / off after the idle time (same API as the ESP32 version)
 * The Pi drives it as a plain GPIO; brightness is stored and shown, the panel is
 * full on above 0 (wire the LED pin through a PWM-capable driver for dimming).
 */
#pragma once

#include <cstdint>

class Backlight
{
public:
    void begin();
    void update();
    bool wake();
    void sleep();
    bool isOff() const { return off_; }

    uint8_t brightness() const { return bright_; }
    void setBrightness(uint8_t percent);
    uint16_t offMinutes() const { return offMin_; }
    void setOffMinutes(uint16_t minutes);
    static bool available();

private:
    void apply(uint8_t percent);
    uint32_t lastActivity_ = 0;
    bool off_ = false;
    uint8_t bright_ = 100;
    uint16_t offMin_ = 15;
};

extern Backlight backlight;
