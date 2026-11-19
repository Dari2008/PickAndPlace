/**
 * spi.h - Linux spidev (no library needed)
 * -----------------------------------------------------------------------------
 *   SpiDev s;  s.open("/dev/spidev0.0", 40000000, 0);
 *   s.write(buf, n);                 // split into chunks of the driver's bufsiz
 *   s.transfer(tx, rx, n);           // full duplex
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class SpiDev
{
public:
    ~SpiDev() { close(); }
    bool open(const std::string &dev, uint32_t hz, uint8_t mode);
    void close();
    bool ok() const { return fd_ >= 0; }
    bool write(const uint8_t *data, size_t n);
    bool transfer(const uint8_t *tx, uint8_t *rx, size_t n, uint32_t hz = 0);

private:
    int fd_ = -1;
    uint32_t hz_ = 0;
    size_t chunk_ = 4096;
};
