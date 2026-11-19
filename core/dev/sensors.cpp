#include "sensors.h"
#include "../hal/i2c.h"
#include "../octo/octopus.h"
#include "../compat/Arduino.h"

Sensors sensors;
static I2cBus g_bus;

static const uint8_t ADS_ADDR = 0x48;

void Sensors::start(const std::string &bus, bool simulate)
{
    stop();
    busDev_ = bus;
    sim_ = simulate;
    if (!simulate && g_bus.open(bus))
    {
        bmp_ = initBmp();
        ads_ = initAds();
    }
    if (!bmp_ && !ads_)
        sim_ = true; // nothing found: simulate (also covers a PC)
    for (auto &a : adc_)
        a = 13200;
    running_ = true;
    t_ = std::thread(&Sensors::run, this);
}

void Sensors::stop()
{
    running_ = false;
    if (t_.joinable())
        t_.join();
}

// ---- BMP280 (datasheet BST-BMP280-DS001, compensation formulas 3.11.3, double version) ----
bool Sensors::initBmp()
{
    for (uint8_t addr : {(uint8_t)0x76, (uint8_t)0x77})
    {
        uint8_t id = 0;
        if (g_bus.readReg(addr, 0xD0, &id, 1) && (id == 0x58 || id == 0x60)) // BMP280 (0x60 = BME280)
        {
            bmpAddr_ = addr;
            uint8_t c[24];
            if (!g_bus.readReg(addr, 0x88, c, 24))
                return false;
            auto u16 = [&](int i) { return (uint16_t)(c[i] | (c[i + 1] << 8)); };
            auto s16 = [&](int i) { return (int16_t)u16(i); };
            T1_ = u16(0);
            T2_ = s16(2);
            T3_ = s16(4);
            P1_ = u16(6);
            P2_ = s16(8);
            P3_ = s16(10);
            P4_ = s16(12);
            P5_ = s16(14);
            P6_ = s16(16);
            P7_ = s16(18);
            P8_ = s16(20);
            P9_ = s16(22);
            // config: t_standby 0.5 ms, IIR filter 4; ctrl_meas: temp x2, pressure x16, normal mode
            const uint8_t cfg = (0 << 5) | (2 << 2);
            const uint8_t meas = (2 << 5) | (5 << 2) | 3;
            return g_bus.writeReg(addr, 0xF5, &cfg, 1) && g_bus.writeReg(addr, 0xF4, &meas, 1);
        }
    }
    return false;
}

bool Sensors::readBmp()
{
    uint8_t d[6];
    if (!g_bus.readReg(bmpAddr_, 0xF7, d, 6))
        return false;
    const int32_t adcP = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
    const int32_t adcT = (d[3] << 12) | (d[4] << 4) | (d[5] >> 4);
    double v1 = (adcT / 16384.0 - T1_ / 1024.0) * T2_;
    double v2 = ((adcT / 131072.0 - T1_ / 8192.0) * (adcT / 131072.0 - T1_ / 8192.0)) * T3_;
    const double tFine = v1 + v2;
    temp_ = (float)(tFine / 5120.0);
    v1 = tFine / 2.0 - 64000.0;
    v2 = v1 * v1 * P6_ / 32768.0;
    v2 = v2 + v1 * P5_ * 2.0;
    v2 = v2 / 4.0 + P4_ * 65536.0;
    v1 = (P3_ * v1 * v1 / 524288.0 + P2_ * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * P1_;
    if (v1 == 0.0)
        return false;
    double p = 1048576.0 - adcP;
    p = (p - v2 / 4096.0) * 6250.0 / v1;
    v1 = P9_ * p * p / 2147483648.0;
    v2 = p * P8_ / 32768.0;
    p = p + (v1 + v2 + P7_) / 16.0;
    pressure_ = (float)(p / 100.0);
    return true;
}

// ---- ADS1115: single-shot, +-4.096 V, 860 SPS -----------------------------------------------
bool Sensors::initAds()
{
    uint8_t cfg[2];
    return g_bus.readReg(ADS_ADDR, 0x01, cfg, 2);
}

bool Sensors::readAds(int ch, int16_t &v)
{
    // OS start | MUX AINch vs GND (100 + ch) | PGA 4.096 V | single shot | 860 SPS | comparator off
    const uint16_t cfg = 0x8000 | ((uint16_t)(4 + ch) << 12) | (1 << 9) | (1 << 8) | (7 << 5) | 3;
    const uint8_t b[2] = {(uint8_t)(cfg >> 8), (uint8_t)cfg};
    if (!g_bus.writeReg(ADS_ADDR, 0x01, b, 2))
        return false;
    for (int i = 0; i < 10; i++)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(1200));
        uint8_t r[2];
        if (g_bus.readReg(ADS_ADDR, 0x01, r, 2) && (r[0] & 0x80))
            break;
    }
    uint8_t r[2];
    if (!g_bus.readReg(ADS_ADDR, 0x00, r, 2))
        return false;
    v = (int16_t)((r[0] << 8) | r[1]);
    return true;
}

void Sensors::run()
{
    uint32_t lastBmp = 0;
    float simP = 1013.25f;
    while (running_)
    {
        if (sim_)
        {
            // Vacuum: pump on + valve open -> the line drops to ~650 hPa within ~0.2 s
            const OctoStatus s = octo.status();
            const float target = (s.pump > 0 && s.valve) ? 650.0f : 1013.25f;
            simP += (target - simP) * 0.25f;
            pressure_ = simP;
            temp_ = 22.0f;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (ads_)
            for (int ch = 0; ch < 2; ch++)
            {
                int16_t v;
                if (readAds(ch, v))
                    adc_[ch] = v;
            }
        if (bmp_ && millis() - lastBmp >= 40)
        {
            lastBmp = millis();
            readBmp();
        }
        if (!ads_)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
