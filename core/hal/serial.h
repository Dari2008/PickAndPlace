/**
 * serial.h - a serial port on Linux (termios, raw 8N1); the Octopus USB CDC port
 * -----------------------------------------------------------------------------
 *   SerialPort s;  s.open("/dev/ttyACM0", 115200);
 *   s.writeAll("STATUS\n");  int n = s.readSome(buf, sizeof(buf), 50); // waits up to 50 ms
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <cstddef>
#include <string>

class SerialPort
{
public:
    ~SerialPort() { close(); }
    bool open(const std::string &dev, int baud);
    void close();
    bool isOpen() const { return fd_ >= 0; }
    bool writeAll(const std::string &data);
    // Read what is there, waiting at most timeoutMs for the first byte. -1 = error (unplugged).
    int readSome(char *buf, size_t size, int timeoutMs);
    const std::string &device() const { return dev_; }

private:
    int fd_ = -1;
    std::string dev_;
};
