#include "flash.h"
#include "../hal/serial.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace
{
    const char *const DFU_ID = "0483:df11"; // STM32 ROM bootloader

    void log(const std::string &s) { std::cerr << "[flash] " << s << std::endl; }

    void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

    // Run a shell command, collect its output (also echoed to stderr when echo is set)
    int run(const std::string &cmd, std::string *out = nullptr, bool echo = false)
    {
        FILE *f = popen((cmd + " 2>&1").c_str(), "r");
        if (!f)
            return -1;
        char buf[256];
        while (fgets(buf, sizeof(buf), f))
        {
            if (out)
                *out += buf;
            if (echo)
                std::cerr << "  " << buf;
        }
        const int st = pclose(f);
        return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }

    // lsusb (usbutils) if there, else dfu-util's own list
    bool inDfu()
    {
        if (run(std::string("lsusb -d ") + DFU_ID) == 0)
            return true;
        return run(std::string("dfu-util -l | grep -qi '") + DFU_ID + "'") == 0;
    }

    std::string octopusPort()
    {
        std::string out;
        if (run("ls /dev/serial/by-id/ 2>/dev/null | grep -i pap-motion | head -1", &out) != 0 || out.empty())
            return "";
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        return out.empty() ? "" : "/dev/serial/by-id/" + out;
    }

    template <class F>
    bool waitFor(F cond, int ms)
    {
        for (int t = 0; t < ms; t += 200)
        {
            if (cond())
                return true;
            sleepMs(200);
        }
        return cond();
    }

    // DFU command to the running firmware; true when it answered ok
    bool sendDfu(const std::string &dev)
    {
        SerialPort s;
        if (!s.open(dev, 115200))
        {
            log("can't open " + dev);
            return false;
        }
        s.writeAll("\nDFU\n");
        std::string got;
        char buf[256];
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < end && got.find("\nok") == std::string::npos &&
               got.find("unknown command DFU") == std::string::npos)
        {
            const int n = s.readSome(buf, sizeof(buf), 100);
            if (n < 0)
                break; // USB gone: the board is already restarting
            got.append(buf, n > 0 ? n : 0);
            if (got.size() > 4096)
                got.erase(0, got.size() - 1024);
        }
        if (got.find("unknown command DFU") != std::string::npos)
        {
            log("the firmware on the Octopus has no DFU command yet: flash this firmware once by microSD card "
                "(or fit the BOOT0 jumper and power-cycle), then --flash works");
            return false;
        }
        return true; // answered ok, or went away mid-answer: both mean it restarts
    }
}

std::string defaultFirmwarePath()
{
    // <pap>/core/build/pap-core -> <pap>/firmware/pap-motion.bin
    char exe[512];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0)
    {
        std::string p(exe, (size_t)n);
        for (int i = 0; i < 3; i++)
            p = p.substr(0, p.find_last_of('/'));
        return p + "/firmware/pap-motion.bin";
    }
    const char *home = getenv("HOME");
    return std::string(home ? home : ".") + "/pap/firmware/pap-motion.bin";
}

bool flashOctopus(const std::string &dev, const std::string &bin)
{
    struct stat st;
    if (stat(bin.c_str(), &st) != 0 || st.st_size < 1024)
    {
        log("firmware file " + bin + " not found (upload.bat copies octopus-fw/.pio/build/octopus_f446/firmware.bin there)");
        return false;
    }
    if (run("command -v dfu-util") != 0)
    {
        log("dfu-util is missing: sudo apt install dfu-util");
        return false;
    }
    log("firmware " + bin + " (" + std::to_string(st.st_size / 1024) + " KiB)");

    if (!inDfu())
    {
        std::string port = octopusPort();
        if (port.empty())
            port = dev;
        if (port.empty() || port == "sim" || access(port.c_str(), F_OK) != 0)
        {
            log("Octopus not found (no " + (port.empty() || port == "sim" ? std::string("serial port") : port) +
                ", no DFU device): USB cable? powered?");
            return false;
        }
        log("restarting the Octopus into DFU mode (" + port + ")");
        if (!sendDfu(port))
            return false;
        if (!waitFor(inDfu, 10000))
        {
            log("no DFU device (" + std::string(DFU_ID) + ") after 10 s - check lsusb. Is a microSD card in the "
                "Octopus? It blocks the ROM bootloader's USB (SDIO pins = its USART3): take it out, power-cycle, try again");
            return false;
        }
    }
    else
        log("Octopus is already in DFU mode");

    sleepMs(500);
    log("flashing ...");
    std::string out;
    const int rc = run(std::string("dfu-util -d ") + DFU_ID + " -a 0 -s 0x08008000:leave -D '" + bin + "'", &out, true);
    // With ":leave" some dfu-util versions report an error after a good download (the board is gone)
    const bool written = rc == 0 || out.find("File downloaded successfully") != std::string::npos ||
                         out.find("Download done") != std::string::npos;
    if (!written)
    {
        if (out.find("Cannot open DFU device") != std::string::npos || out.find("LIBUSB_ERROR_ACCESS") != std::string::npos)
            log("no permission for the DFU device: add the udev rule from pi/README.md (or run with sudo)");
        log("dfu-util failed (exit " + std::to_string(rc) + ")");
        return false;
    }

    log("written, waiting for the new firmware ...");
    if (!waitFor([] { return !octopusPort().empty(); }, 15000))
    {
        log("the firmware didn't come back on USB within 15 s: power-cycle the Octopus (it is flashed)");
        return false;
    }
    log("done: " + octopusPort());
    sleepMs(500); // let udev finish the permissions of the new tty
    return true;
}
