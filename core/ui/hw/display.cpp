#include "display.h"
#include "../prefs.h"
#include "../../vision/camera.h"

#include <cstdio>
#include <cstring>
#include <thread>

#define STBI_WRITE_NO_STDIO
#include "../../third_party/stb_image_write.h"

Display display;

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void Display::begin(bool simulate)
{
    rot_ = prefs.getInt("tft_rot", DISPLAY_ROTATION) == 3 ? 3 : 1;
    bgr_ = prefs.getInt("tft_bgr", 1) != 0;
    invert_ = prefs.getInt("tft_invert", 0) != 0;
    if (simulate)
        return;
    const std::string chip = GpioLine::chip();
    const int mhz = prefs.getInt("tft_spi_mhz", TFT_SPI_HZ / 1000000);
    if (!spi_.open(TFT_SPI_DEV, (uint32_t)(mhz >= 1 && mhz <= 80 ? mhz : TFT_SPI_HZ / 1000000) * 1000000u, 0))
    {
        fprintf(stderr, "display: %s not available (dtparam=spi=on?), running without the panel\n", TFT_SPI_DEV);
        return;
    }
    if (!dc_.output(chip, TFT_DC, true) || !rst_.output(chip, TFT_RST, true))
    {
        fprintf(stderr, "display: GPIO %d / %d not available\n", TFT_DC, TFT_RST);
        spi_.close();
        return;
    }
    bl_.output(chip, TFT_BL, false);
    init();
    std::lock_guard<std::mutex> l(fbm_);
    // Blank the panel until the first frame
    window(0, 0, SCREEN_W - 1, SCREEN_H - 1);
    tx_.assign((size_t)SCREEN_W * SCREEN_H * 3, 0);
    dc_.set(true);
    spi_.write(tx_.data(), tx_.size());
    setBacklight(true);
}

void Display::command(uint8_t c, const uint8_t *data, size_t n)
{
    dc_.set(false);
    spi_.write(&c, 1);
    if (n)
    {
        dc_.set(true);
        spi_.write(data, n);
    }
}

void Display::command(uint8_t c, std::initializer_list<uint8_t> data)
{
    std::vector<uint8_t> d(data);
    command(c, d.data(), d.size());
}

void Display::init()
{
    rst_.set(true);
    sleepMs(5);
    rst_.set(false);
    sleepMs(20);
    rst_.set(true);
    sleepMs(150);
    command(0x01); // software reset
    sleepMs(150);
    command(0xE0, {0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F}); // gamma +
    command(0xE1, {0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F}); // gamma -
    command(0xC0, {0x17, 0x15});       // power control 1
    command(0xC1, {0x41});             // power control 2
    command(0xC5, {0x00, 0x12, 0x80}); // VCOM
    command(0x3A, {0x66});             // 18 bit / pixel (the only SPI mode with colour)
    command(0xB0, {0x00});             // interface mode
    command(0xB1, {0xA0});             // frame rate 60 Hz
    command(0xB4, {0x02});             // 2-dot inversion
    command(0xB6, {0x02, 0x02, 0x3B}); // display function
    command(0xB7, {0xC6});             // entry mode
    command(0xF7, {0xA9, 0x51, 0x2C, 0x82});
    command(invert_ ? 0x21 : 0x20);
    // MADCTL: landscape; 3 = turned by 180 degrees
    const uint8_t mad = (rot_ == 3 ? 0xE0 : 0x20) | (bgr_ ? 0x08 : 0x00);
    command(0x36, &mad, 1);
    command(0x11); // sleep out
    sleepMs(120);
    command(0x29); // display on
    sleepMs(20);
}

void Display::window(int x0, int y0, int x1, int y1)
{
    command(0x2A, {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1});
    command(0x2B, {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1});
    command(0x2C);
}

void Display::setRotation(int r)
{
    rot_ = r == 3 ? 3 : 1;
    prefs.setInt("tft_rot", rot_);
    if (!spi_.ok())
        return;
    std::lock_guard<std::mutex> l(fbm_);
    const uint8_t mad = (rot_ == 3 ? 0xE0 : 0x20) | (bgr_ ? 0x08 : 0x00);
    command(0x36, &mad, 1);
    // The panel keeps its pixels in the old orientation: send the whole last frame again
    window(0, 0, SCREEN_W - 1, SCREEN_H - 1);
    dc_.set(true);
    spi_.write(fb_.data(), fb_.size());
}

void Display::setBacklight(bool on)
{
    if (bl_.ok())
        bl_.set(on);
}

void Display::push(LGFX_Sprite &frame, int x, int y, int w, int h)
{
    if (x < 0)
        w += x, x = 0;
    if (y < 0)
        h += y, y = 0;
    w = std::min(w, SCREEN_W - x);
    h = std::min(h, SCREEN_H - y);
    if (w <= 0 || h <= 0)
        return;
    std::lock_guard<std::mutex> l(fbm_);
    tx_.resize((size_t)w * h * 3);
    frame.readRectRGB(x, y, w, h, tx_.data()); // R, G, B bytes
    for (int r = 0; r < h; r++)
        memcpy(&fb_[((size_t)(y + r) * SCREEN_W + x) * 3], &tx_[(size_t)r * w * 3], (size_t)w * 3);
    if (!spi_.ok())
        return;
    window(x, y, x + w - 1, y + h - 1);
    dc_.set(true);
    spi_.write(tx_.data(), tx_.size()); // the panel uses the top 6 bits of each byte
}

std::string Display::jpeg(int quality, int &w, int &h)
{
    std::vector<uint8_t> copy;
    {
        std::lock_guard<std::mutex> l(fbm_);
        copy = fb_;
    }
    w = SCREEN_W;
    h = SCREEN_H;
    return encodeJpeg(copy, w, h, quality);
}

std::string Display::savePng(const std::string &path)
{
    std::lock_guard<std::mutex> l(fbm_);
    FILE *f = fopen(path.c_str(), "wb");
    if (!f)
        return "cannot write " + path;
    auto put = [](void *ctx, void *data, int n) { fwrite(data, 1, (size_t)n, (FILE *)ctx); };
    const int ok = stbi_write_png_to_func(put, f, SCREEN_W, SCREEN_H, 3, fb_.data(), SCREEN_W * 3);
    fclose(f);
    return ok ? "" : "cannot encode " + path;
}
