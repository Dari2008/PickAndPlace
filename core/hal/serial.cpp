#include "serial.h"

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

static speed_t baudConst(int baud)
{
    switch (baud)
    {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return B115200;
    }
}

bool SerialPort::open(const std::string &dev, int baud)
{
    close();
    fd_ = ::open(dev.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0)
        return false;
    termios t{};
    if (tcgetattr(fd_, &t) != 0)
    {
        close();
        return false;
    }
    cfmakeraw(&t);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~CRTSCTS;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    cfsetispeed(&t, baudConst(baud));
    cfsetospeed(&t, baudConst(baud));
    if (tcsetattr(fd_, TCSANOW, &t) != 0)
    {
        close();
        return false;
    }
    tcflush(fd_, TCIOFLUSH);
    dev_ = dev;
    return true;
}

void SerialPort::close()
{
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
}

bool SerialPort::writeAll(const std::string &data)
{
    size_t done = 0;
    while (fd_ >= 0 && done < data.size())
    {
        const ssize_t n = ::write(fd_, data.data() + done, data.size() - done);
        if (n > 0)
            done += (size_t)n;
        else if (n < 0 && (errno == EAGAIN || errno == EINTR))
        {
            pollfd p{fd_, POLLOUT, 0};
            ::poll(&p, 1, 100);
        }
        else
            return false;
    }
    return done == data.size();
}

int SerialPort::readSome(char *buf, size_t size, int timeoutMs)
{
    if (fd_ < 0)
        return -1;
    pollfd p{fd_, POLLIN, 0};
    const int r = ::poll(&p, 1, timeoutMs);
    if (r < 0)
        return errno == EINTR ? 0 : -1;
    if (r == 0)
        return 0;
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -1;
    const ssize_t n = ::read(fd_, buf, size);
    if (n < 0)
        return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    if (n == 0)
        return -1; // device gone
    return (int)n;
}
