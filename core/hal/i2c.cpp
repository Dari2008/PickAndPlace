#include "i2c.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

bool I2cBus::open(const std::string &dev)
{
    close();
    fd_ = ::open(dev.c_str(), O_RDWR | O_CLOEXEC);
    return fd_ >= 0;
}

void I2cBus::close()
{
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
}

bool I2cBus::writeReg(uint8_t addr, uint8_t reg, const uint8_t *data, size_t n)
{
    std::lock_guard<std::mutex> l(m_);
    if (fd_ < 0)
        return false;
    std::vector<uint8_t> buf(n + 1);
    buf[0] = reg;
    for (size_t i = 0; i < n; i++)
        buf[i + 1] = data[i];
    i2c_msg msg{addr, 0, (uint16_t)buf.size(), buf.data()};
    i2c_rdwr_ioctl_data x{&msg, 1};
    return ioctl(fd_, I2C_RDWR, &x) == 1;
}

bool I2cBus::readReg(uint8_t addr, uint8_t reg, uint8_t *data, size_t n)
{
    std::lock_guard<std::mutex> l(m_);
    if (fd_ < 0)
        return false;
    i2c_msg msgs[2] = {{addr, 0, 1, &reg}, {addr, I2C_M_RD, (uint16_t)n, data}};
    i2c_rdwr_ioctl_data x{msgs, 2};
    return ioctl(fd_, I2C_RDWR, &x) == 2;
}
