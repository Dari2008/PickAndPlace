#include "gpio.h"

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

static bool request(int &fd, const std::string &chip, unsigned line, uint64_t flags, bool value)
{
    const int c = ::open(chip.c_str(), O_RDWR | O_CLOEXEC);
    if (c < 0)
        return false;
    gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));
    req.offsets[0] = line;
    req.num_lines = 1;
    strncpy(req.consumer, "pap-core", sizeof(req.consumer) - 1);
    req.config.flags = flags;
    if (flags & GPIO_V2_LINE_FLAG_OUTPUT)
    {
        req.config.num_attrs = 1;
        req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        req.config.attrs[0].attr.values = value ? 1 : 0;
        req.config.attrs[0].mask = 1;
    }
    const int r = ioctl(c, GPIO_V2_GET_LINE_IOCTL, &req);
    ::close(c);
    if (r < 0)
        return false;
    fd = req.fd;
    return true;
}

bool GpioLine::output(const std::string &chip, unsigned line, bool value)
{
    release();
    return request(fd_, chip, line, GPIO_V2_LINE_FLAG_OUTPUT, value);
}

bool GpioLine::input(const std::string &chip, unsigned line, bool pullUp, bool pullDown)
{
    release();
    return request(fd_, chip, line,
                   GPIO_V2_LINE_FLAG_INPUT | (pullUp     ? GPIO_V2_LINE_FLAG_BIAS_PULL_UP
                                              : pullDown ? GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN
                                                         : 0),
                   false);
}

void GpioLine::set(bool v)
{
    if (fd_ < 0)
        return;
    gpio_v2_line_values vals{};
    vals.bits = v ? 1 : 0;
    vals.mask = 1;
    ioctl(fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
}

bool GpioLine::get()
{
    if (fd_ < 0)
        return false;
    gpio_v2_line_values vals{};
    vals.mask = 1;
    if (ioctl(fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &vals) < 0)
        return false;
    return vals.bits & 1;
}

void GpioLine::release()
{
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
}

std::string GpioLine::chip()
{
    // The header's chip is labelled "pinctrl-rp1" on the Pi 5
    for (int i = 0; i < 16; i++)
    {
        const std::string path = "/dev/gpiochip" + std::to_string(i);
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        gpiochip_info info{};
        const bool ok = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0;
        ::close(fd);
        if (ok && (strstr(info.label, "rp1") || strstr(info.label, "bcm2835") || strstr(info.label, "bcm2711")))
            return path;
    }
    return "/dev/gpiochip0";
}
