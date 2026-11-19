/**
 * display.h - 3.5" ILI9488 over the Pi's spidev (480 x 320, 18-bit colour)
 * -----------------------------------------------------------------------------
 * The UI draws into an LGFX_Sprite (whole frame, RGB565); push() converts the
 * changed area to RGB666 and sends it. The last frame is kept for the website's
 * display mirror (SCREEN command -> jpeg()). Without the panel (--sim, or no
 * /dev/spidev0.0) only the mirror is updated.
 * Pins / device: ../config.h. Preferences: tft_rot (1 / 3), tft_bgr (1), tft_invert (0), tft_spi_mhz (20).
 * -----------------------------------------------------------------------------
 */
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <mutex>
#include <string>
#include <vector>
#include "../../hal/gpio.h"
#include "../../hal/spi.h"
#include "../config.h"

class Display
{
public:
    void begin(bool simulate);
    bool hardware() const { return spi_.ok(); }
    void push(LGFX_Sprite &frame, int x, int y, int w, int h);
    std::string jpeg(int quality, int &w, int &h);
    std::string savePng(const std::string &path); // tests / docs ("" = ok, else the error)

    int rotation() const { return rot_; }
    void setRotation(int r); // 1 or 3 (saved)
    void setBacklight(bool on);

private:
    void command(uint8_t c, const uint8_t *data = nullptr, size_t n = 0);
    void command(uint8_t c, std::initializer_list<uint8_t> data);
    void init();
    void window(int x0, int y0, int x1, int y1);

    SpiDev spi_;
    GpioLine dc_, rst_, bl_;
    int rot_ = DISPLAY_ROTATION;
    bool bgr_ = true, invert_ = false;
    std::mutex fbm_;
    std::vector<uint8_t> fb_ = std::vector<uint8_t>((size_t)SCREEN_W * SCREEN_H * 3, 0); // RGB888
    std::vector<uint8_t> tx_;
};

extern Display display;
