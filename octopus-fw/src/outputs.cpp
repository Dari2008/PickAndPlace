#include "outputs.h"

#include "settings.h"
#include <Arduino.h>

Outputs outputs;

static const uint32_t PWM_HZ = 1000;

void Outputs::begin()
{
    for (PinName pn : UNUSED_MOSFETS)
    {
        pinMode(pinNametoDigitalPin(pn), OUTPUT);
        digitalWrite(pinNametoDigitalPin(pn), LOW);
    }
    analogWriteResolution(12);
    analogWriteFrequency(PWM_HZ);
    for (uint8_t i = 0; i < N_OUTPUTS; i++)
    {
        pinMode(pinNametoDigitalPin(OUTPUT_PINS[i]), OUTPUT);
        digitalWrite(pinNametoDigitalPin(OUTPUT_PINS[i]), LOW);
        value_[i] = 0;
    }
}

void Outputs::set(uint8_t out, float f)
{
    if (out >= N_OUTPUTS)
        return;
    if (f < 0)
        f = 0;
    if (f > 1)
        f = 1;
    const uint32_t pin = pinNametoDigitalPin(OUTPUT_PINS[out]);
    if (out == OUT_VALVE)
        f = f > 0.0f ? 1.0f : 0.0f; // a valve is open or closed
    if (out == OUT_PUMP && cfg::get(cfg::PUMP_USED) < 0.5f)
        f = 0.0f; // pump_used 0: no pump connected, the output stays off
    if (f <= 0.0f || f >= 1.0f)
    {
        // Plain on / off: no PWM timer involved
        pinMode(pin, OUTPUT);
        digitalWrite(pin, f > 0 ? HIGH : LOW);
    }
    else
        analogWrite(pin, (int)(f * 4095.0f + 0.5f));
    value_[out] = f;
}

void Outputs::allOff()
{
    for (uint8_t i = 0; i < N_OUTPUTS; i++)
        if (i != OUT_CAM_FAN && i != OUT_BOX_FAN && i != OUT_PI_FAN)
            set(i, 0);
}

void Outputs::fansOn()
{
    set(OUT_CAM_FAN, cfg::get(cfg::CAM_FAN) / 100.0f);
    set(OUT_BOX_FAN, cfg::get(cfg::BOX_FAN) / 100.0f);
    set(OUT_PI_FAN, cfg::get(cfg::PI_FAN) / 100.0f);
}

bool Outputs::anyOn() const
{
    for (uint8_t i = 0; i < N_OUTPUTS; i++)
        if (value_[i] > 0 && (i == OUT_PUMP || i == OUT_VALVE))
            return true;
    return false;
}
