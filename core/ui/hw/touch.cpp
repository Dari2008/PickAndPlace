#include "touch.h"
#include "../config.h"
#include "../prefs.h"

#include <algorithm>
#include <cstdio>

Touch touch;

void Touch::begin(bool simulate)
{
    cal_.xMin = prefs.getInt("touch_xmin", cal_.xMin);
    cal_.xMax = prefs.getInt("touch_xmax", cal_.xMax);
    cal_.yMin = prefs.getInt("touch_ymin", cal_.yMin);
    cal_.yMax = prefs.getInt("touch_ymax", cal_.yMax);
    cal_.swapXY = prefs.getInt("touch_swap", cal_.swapXY) != 0;
    opt_.enabled = prefs.getInt("touch_on", opt_.enabled);
    opt_.minPressure = prefs.getInt("touch_min_z", opt_.minPressure);
    opt_.dragStart = prefs.getInt("touch_drag", opt_.dragStart);
    opt_.minPressMs = prefs.getInt("touch_min_ms", opt_.minPressMs);
    if (!simulate && !spi_.open(TOUCH_SPI_DEV, TOUCH_SPI_HZ, 0))
        fprintf(stderr, "touch: %s not available, touch off\n", TOUCH_SPI_DEV);
}

void Touch::setOptions(const TouchOptions &o)
{
    opt_ = o;
    prefs.setInt("touch_on", opt_.enabled);
    prefs.setInt("touch_min_z", opt_.minPressure);
    prefs.setInt("touch_drag", opt_.dragStart);
    prefs.setInt("touch_min_ms", opt_.minPressMs);
}

void Touch::setCal(const TouchCal &c)
{
    cal_ = c;
    prefs.setInt("touch_xmin", cal_.xMin);
    prefs.setInt("touch_xmax", cal_.xMax);
    prefs.setInt("touch_ymin", cal_.yMin);
    prefs.setInt("touch_ymax", cal_.yMax);
    prefs.setInt("touch_swap", cal_.swapXY);
}

void Touch::simulate(int x, int y)
{
    sim_ = x < 0 || y < 0 ? -1 : (int32_t)(((uint32_t)x << 16) | ((uint32_t)y & 0xFFFF));
}

// One 12-bit conversion (control byte, then 16 clocks)
uint16_t Touch::sample(uint8_t cmd)
{
    uint8_t tx[3] = {cmd, 0, 0}, rx[3] = {0, 0, 0};
    if (!spi_.transfer(tx, rx, 3))
        return 0;
    return (uint16_t)((((rx[1] << 8) | rx[2]) >> 3) & 0x0FFF);
}

bool Touch::readRaw(int16_t &rx, int16_t &ry, uint16_t &pressure)
{
    const int32_t sim = sim_;
    if (sim >= 0)
    {
        const int sx = (sim >> 16) & 0xFFFF, sy = sim & 0xFFFF;
        // Inverse of read(): simulated touches go through the calibration too
        int a = cal_.xMin + (int)((long)sx * (cal_.xMax - cal_.xMin) / (SCREEN_W - 1));
        int b = cal_.yMin + (int)((long)sy * (cal_.yMax - cal_.yMin) / (SCREEN_H - 1));
        if (cal_.swapXY)
            std::swap(a, b);
        rx = (int16_t)a, ry = (int16_t)b, pressure = 40;
        return true;
    }
    if (!spi_.ok())
        return false;
    const int z1 = sample(0xB1), z2 = sample(0xC1);
    const int z = std::max(0, z1 + 4095 - z2) >> 6; // ~0 .. 64
    if (z < 6)
    {
        sample(0x90); // power down between conversions
        return false;
    }
    int xs[5], ys[5];
    for (int i = 0; i < 5; i++)
    {
        xs[i] = sample(0xD1);
        ys[i] = sample(0x91);
    }
    sample(0x90);
    std::sort(xs, xs + 5);
    std::sort(ys, ys + 5);
    rx = (int16_t)xs[2];
    ry = (int16_t)ys[2];
    pressure = (uint16_t)z;
    return true;
}

static long mapl(long v, long a, long b, long c, long d) { return b == a ? c : (v - a) * (d - c) / (b - a) + c; }

bool Touch::read(TouchPoint &p)
{
    int16_t rx, ry;
    uint16_t pressure;
    if (!opt_.enabled || !readRaw(rx, ry, pressure) || pressure < opt_.minPressure)
        return false;
    int16_t a = rx, b = ry;
    if (cal_.swapXY)
        std::swap(a, b);
    p.x = (int16_t)std::clamp(mapl(a, cal_.xMin, cal_.xMax, 0, SCREEN_W - 1), 0L, (long)SCREEN_W - 1);
    p.y = (int16_t)std::clamp(mapl(b, cal_.yMin, cal_.yMax, 0, SCREEN_H - 1), 0L, (long)SCREEN_H - 1);
    p.pressure = pressure;
    return true;
}
