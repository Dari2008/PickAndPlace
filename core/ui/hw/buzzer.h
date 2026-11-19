// buzzer.h - active buzzer on GPIO27 through an NPN (same API as the ESP32 version)
#pragma once

#include <cstdint>
#include "../../hal/gpio.h"

class Buzzer
{
public:
    void begin(bool simulate);
    void beep(uint16_t ms);
    void click() { beep(15); }
    void update(); // from the UI loop

private:
    GpioLine pin_;
    uint32_t offAt_ = 0;
    bool on_ = false;
};

extern Buzzer buzzer;
