#include "spi.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

bool SpiDev::open(const std::string &dev, uint32_t hz, uint8_t mode)
{
    close();
    fd_ = ::open(dev.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0)
        return false;
    uint8_t bits = 8;
    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 || ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0)
    {
        close();
        return false;
    }
    hz_ = hz;
    // The kernel limits one transfer to spidev.bufsiz (4096 unless set on the kernel command line)
    if (FILE *f = fopen("/sys/module/spidev/parameters/bufsiz", "r"))
    {
        unsigned long b = 0;
        if (fscanf(f, "%lu", &b) == 1 && b >= 64)
            chunk_ = b;
        fclose(f);
    }
    return true;
}

void SpiDev::close()
{
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
}

bool SpiDev::transfer(const uint8_t *tx, uint8_t *rx, size_t n, uint32_t hz)
{
    if (fd_ < 0)
        return false;
    while (n)
    {
        const size_t k = n < chunk_ ? n : chunk_;
        spi_ioc_transfer t;
        memset(&t, 0, sizeof(t));
        t.tx_buf = (uintptr_t)tx;
        t.rx_buf = (uintptr_t)rx;
        t.len = (uint32_t)k;
        t.speed_hz = hz ? hz : hz_;
        t.bits_per_word = 8;
        if (ioctl(fd_, SPI_IOC_MESSAGE(1), &t) < 0)
            return false;
        n -= k;
        if (tx)
            tx += k;
        if (rx)
            rx += k;
    }
    return true;
}

bool SpiDev::write(const uint8_t *data, size_t n)
{
    return transfer(data, nullptr, n);
}
