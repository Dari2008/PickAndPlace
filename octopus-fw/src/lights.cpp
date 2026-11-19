#include "lights.h"
#include "outputs.h"
#include "settings.h"
#include "motion/motion.h"
#include <Arduino.h>

namespace lights
{
    static float topCmd = 0, bottomCmd = 0;
    static bool neoDirty = true;

    static const PinName NEO_PIN = PB_0; // RGB header DATA (74LVC1G125 to 5 V on the board)
    static const uint16_t NEO_MAX = 256;

    // WS2812: 800 kHz, GRB, MSB first; 0 = 0.4 us high, 1 = 0.8 us high
    static void sendFrame(const uint8_t *grb, uint16_t bytes)
    {
        GPIO_TypeDef *port = get_GPIO_Port(STM_PORT(NEO_PIN));
        const uint32_t pin = 1u << STM_PIN(NEO_PIN);
        const uint32_t T = SystemCoreClock / 800000, T0H = SystemCoreClock / 2500000, T1H = SystemCoreClock / 1250000;
        __disable_irq();
        uint32_t start = DWT->CYCCNT - T;
        for (uint16_t i = 0; i < bytes; i++)
            for (uint8_t mask = 0x80; mask; mask >>= 1)
            {
                while (DWT->CYCCNT - start < T)
                {
                }
                start = DWT->CYCCNT;
                port->BSRR = pin;
                const uint32_t high = (grb[i] & mask) ? T1H : T0H;
                while (DWT->CYCCNT - start < high)
                {
                }
                port->BSRR = pin << 16;
            }
        while (DWT->CYCCNT - start < T)
        {
        }
        __enable_irq();
        delayMicroseconds(300); // latch (WS2812B: > 280 us low)
    }

    static void apply()
    {
        outputs.set(OUT_LIGHT_B, bottomCmd);
        outputs.set(OUT_LIGHT_T, bottomCmd > 0 ? 0.0f : topCmd); // never both: the top light blinds the bottom camera
        neoDirty = true;
    }

    void begin()
    {
        pinMode(pinNametoDigitalPin(NEO_PIN), OUTPUT);
        digitalWrite(pinNametoDigitalPin(NEO_PIN), LOW);
        topCmd = cfg::get(cfg::LIGHT_TOP_IDLE) / 100.0f;
        bottomCmd = 0;
        apply();
    }

    void setTop(float f)
    {
        topCmd = f;
        apply();
    }

    void setTopIdle() { setTop(cfg::get(cfg::LIGHT_TOP_IDLE) / 100.0f); }

    void setBottom(float f)
    {
        bottomCmd = f;
        apply();
    }

    void allOff()
    {
        topCmd = bottomCmd = 0;
        apply();
    }

    float top() { return outputs.get(OUT_LIGHT_T); }
    float bottom() { return bottomCmd; }

    void loop()
    {
        if (!neoDirty || motion.isBusy())
            return;
        neoDirty = false;
        const uint16_t n = (uint16_t)min((int)cfg::get(cfg::NEO_COUNT), (int)NEO_MAX);
        if (!n)
            return;
        static uint8_t buf[NEO_MAX * 3];
        const float b = bottomCmd;
        const uint8_t r = (uint8_t)(cfg::get(cfg::NEO_R) * b + 0.5f), g = (uint8_t)(cfg::get(cfg::NEO_G) * b + 0.5f),
                      bl = (uint8_t)(cfg::get(cfg::NEO_B) * b + 0.5f);
        for (uint16_t i = 0; i < n; i++)
        {
            buf[i * 3] = g;
            buf[i * 3 + 1] = r;
            buf[i * 3 + 2] = bl;
        }
        sendFrame(buf, n * 3);
    }
}
