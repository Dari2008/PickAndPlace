/**
 * touch.h - XPT2046 resistive touch on /dev/spidev0.1 (same API as the ESP32 version)
 * -----------------------------------------------------------------------------
 *   TouchPoint p;  if (touch.read(p)) ...   // screen pixels (480 x 320), calibrated
 * Calibration and options: Settings -> Touch (saved in <data>/ui.txt).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <atomic>
#include <cstdint>
#include "../../hal/spi.h"

struct TouchPoint
{
    int16_t x, y;
    uint16_t pressure;
};

struct TouchCal
{
    int16_t xMin = 250, xMax = 3850; // raw ADC at the left / right edge
    int16_t yMin = 300, yMax = 3800; // raw ADC at the top / bottom edge
    bool swapXY = true;              // panel axes vs landscape screen axes
};

struct TouchOptions
{
    uint8_t enabled = 1;
    uint16_t minPressure = 0; // 0 .. ~60 (the XPT2046's Z, scaled like LovyanGFX)
    uint8_t dragStart = 10;   // design px of movement before a touch scrolls
    uint16_t minPressMs = 0;  // shorter touches are ignored (ghost taps)
};

class Touch
{
public:
    void begin(bool simulate);
    bool read(TouchPoint &p);
    bool readRaw(int16_t &rx, int16_t &ry, uint16_t &pressure);

    TouchCal cal() const { return cal_; }
    void setCal(const TouchCal &c);

    TouchOptions options() const { return opt_; }
    void setOptions(const TouchOptions &o);

    // Simulated touch (tests, the website's display mirror): screen pixels, x < 0 = released
    void simulate(int x, int y);

private:
    uint16_t sample(uint8_t cmd);
    SpiDev spi_;
    TouchCal cal_;
    TouchOptions opt_;
    std::atomic<int32_t> sim_{-1}; // x << 16 | y, -1 = released (one value: never half a point)
};

extern Touch touch;
