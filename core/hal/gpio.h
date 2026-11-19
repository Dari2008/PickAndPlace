/**
 * gpio.h - Pi GPIO lines over the Linux GPIO character device (no library needed)
 * -----------------------------------------------------------------------------
 * Pi 5: the 40-pin header is on /dev/gpiochip4 (older kernels) or /dev/gpiochip0 (6.6+),
 * pick it with GpioLine::chip(); BCM numbers (GPIO17 = 17).
 *   GpioLine dc;  dc.output(GpioLine::chip(), 25, false);  dc.set(true);
 *   GpioLine btn; btn.input(GpioLine::chip(), 17, true);   bool pressed = !btn.get();
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <string>

class GpioLine
{
public:
    ~GpioLine() { release(); }
    bool output(const std::string &chip, unsigned line, bool value);
    bool input(const std::string &chip, unsigned line, bool pullUp, bool pullDown = false);
    void set(bool v);
    bool get();
    void release();
    bool ok() const { return fd_ >= 0; }
    static std::string chip(); // the chip of the 40-pin header

private:
    int fd_ = -1;
};
