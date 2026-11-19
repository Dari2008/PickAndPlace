#include "buzzer.h"
#include "../config.h"
#include <Arduino.h>

Buzzer buzzer;

void Buzzer::begin(bool simulate)
{
    if (!simulate)
        pin_.output(GpioLine::chip(), BUZZER_PIN, false);
}

// beep() may come from the link thread (!beep events); update() switches it off on the UI thread
void Buzzer::beep(uint16_t ms)
{
    if (!pin_.ok())
        return;
    pin_.set(true);
    offAt_ = millis() + ms;
    on_ = true;
}

void Buzzer::update()
{
    if (on_ && (int32_t)(millis() - offAt_) >= 0)
    {
        on_ = false;
        pin_.set(false);
    }
}
