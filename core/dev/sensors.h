/**
 * sensors.h - BMP280 vacuum sensor and ADS1115 joystick ADC on the Pi's I2C bus
 * -----------------------------------------------------------------------------
 *   BMP280 (0x76 or 0x77): absolute pressure in the vacuum line, hPa, ~25 Hz
 *   ADS1115 (0x48): AIN0 = joystick X, AIN1 = joystick Y, AIN2 / AIN3 spare, ~100 Hz per channel
 * A background thread reads both; the getters return the latest values.
 * Without the bus / chips (PC, simulator): simulated values (pressure drops while
 * the Octopus reports pump on + valve open; joystick at its centre).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>

class Sensors
{
public:
    void start(const std::string &bus, bool simulate);
    void stop();

    bool bmpPresent() const { return bmp_; }
    bool adsPresent() const { return ads_; }
    float pressureHpa() const { return pressure_; } // NAN without a reading
    float temperatureC() const { return temp_; }
    int16_t adc(int ch) const { return ch >= 0 && ch < 4 ? adc_[ch].load() : 0; }
    bool simulated() const { return sim_; }

private:
    void run();
    bool initBmp();
    bool readBmp();
    bool initAds();
    bool readAds(int ch, int16_t &v);

    std::string busDev_;
    std::thread t_;
    std::atomic<bool> running_{false};
    std::atomic<bool> bmp_{false}, ads_{false}, sim_{false};
    std::atomic<float> pressure_{NAN}, temp_{NAN};
    std::atomic<int16_t> adc_[4] = {};
    int16_t adsHist_[2][3] = {};   // last three readings per stick channel (median)
    int adsCount_[2] = {};
    uint8_t bmpAddr_ = 0x76;
    // BMP280 calibration
    uint16_t T1_ = 0, P1_ = 0;
    int16_t T2_ = 0, T3_ = 0, P2_ = 0, P3_ = 0, P4_ = 0, P5_ = 0, P6_ = 0, P7_ = 0, P8_ = 0, P9_ = 0;
};

extern Sensors sensors;
