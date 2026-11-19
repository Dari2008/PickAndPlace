#include "loadcell.h"
#include "motion/stepper.h"
#include "settings.h"
#include <Arduino.h>
#include <math.h>

namespace loadcell
{
    static const uint32_t PIN_DT = pinNametoDigitalPin(PE_9);  // EXP1 pin 3
    static const uint32_t PIN_SCK = pinNametoDigitalPin(PE_10); // EXP1 pin 4
    static int32_t last = 0, zero = 0;
    static uint32_t lastMs = 0, count = 0, rateStart = 0;
    static float rate_ = 0, peak_ = 0;
    static bool armed = false;

    void begin()
    {
        pinMode(PIN_SCK, OUTPUT);
        digitalWrite(PIN_SCK, LOW); // SCK high > 60 us would power the HX711 down
        pinMode(PIN_DT, INPUT_PULLUP); // nothing connected: stays high = "no sample"
        rateStart = millis();
    }

    // 24 bits MSB first + 1 pulse (channel A, gain 128 next time)
    static int32_t readSample()
    {
        uint32_t v = 0;
        for (uint8_t i = 0; i < 25; i++)
        {
            noInterrupts();
            digitalWrite(PIN_SCK, HIGH);
            delayMicroseconds(1);
            const int bit = digitalRead(PIN_DT);
            digitalWrite(PIN_SCK, LOW);
            interrupts();
            if (i < 24)
                v = (v << 1) | (uint32_t)bit;
            delayMicroseconds(1);
        }
        if (v & 0x800000)
            v |= 0xFF000000; // sign
        return (int32_t)v;
    }

    void poll()
    {
        const uint32_t now = millis();
        if (now - rateStart >= 1000)
        {
            rate_ = count * 1000.0f / (float)(now - rateStart);
            count = 0;
            rateStart = now;
        }
        if (digitalRead(PIN_DT) != LOW)
            return;
        last = readSample();
        lastMs = now;
        count++;
        if (armed)
        {
            const float g = fabsf(grams());
            if (g > peak_)
                peak_ = g;
            if (g >= cfg::get(cfg::LC_TRIGGER))
                inputs_set_loadcell(true);
        }
    }

    bool present() { return lastMs && millis() - lastMs < 500; }
    int32_t raw() { return last; }
    int32_t tareRaw() { return zero; }
    float rate() { return rate_; }
    float peak() { return peak_; }

    float grams()
    {
        const float s = cfg::get(cfg::LC_SCALE);
        return (float)(last - zero) / (s != 0 ? s : 1.0f);
    }

    bool tare(uint16_t ms)
    {
        const uint32_t t0 = millis();
        uint32_t seen = lastMs;
        int64_t sum = 0;
        uint32_t n = 0;
        while (millis() - t0 < ms || n == 0)
        {
            poll();
            if (lastMs != seen)
            {
                seen = lastMs;
                sum += last;
                n++;
            }
            if (millis() - t0 > ms + 400) // nothing arrives
                return false;
        }
        zero = (int32_t)(sum / (int64_t)n);
        return true;
    }

    void arm()
    {
        inputs_set_loadcell(false);
        peak_ = 0;
        armed = true;
    }

    void disarm()
    {
        armed = false;
        inputs_set_loadcell(false);
    }
}
