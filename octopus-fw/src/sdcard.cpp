#include "sdcard.h"
#include <Arduino.h>
#include <STM32SD.h>

namespace sdcard
{
    static const uint32_t DETECT_PIN = pinNametoDigitalPin(PC_14);
    static bool mounted = false;

    static bool mount(std::string &err)
    {
        // Card detect switch: LOW = card in the slot
        pinMode(DETECT_PIN, INPUT_PULLUP);
        if (digitalRead(DETECT_PIN) != LOW)
        {
            mounted = false;
            err = "no card in the Octopus slot";
            return false;
        }
        if (!mounted)
        {
            mounted = SD.begin(DETECT_PIN);
            if (!mounted)
                err = "card not readable (FAT32 / exFAT?)";
        }
        return mounted;
    }

    bool list(const std::string &dir, std::function<void(const char *, uint32_t, bool)> fn, std::string &err)
    {
        if (!mount(err))
            return false;
        File d = SD.open(dir.empty() ? "/" : dir.c_str());
        if (!d || !d.isDirectory())
        {
            err = "no such folder";
            mounted = false; // the card may have been changed: mount again next time
            return false;
        }
        for (File f = d.openNextFile(); f; f = d.openNextFile())
        {
            const char *name = f.name();
            const char *slash = strrchr(name, '/');
            fn(slash ? slash + 1 : name, (uint32_t)f.size(), f.isDirectory());
            f.close();
        }
        d.close();
        return true;
    }

    bool read(const std::string &path, uint32_t offset, uint32_t maxBytes, std::function<void(const std::string &)> fn,
              uint32_t &next, uint32_t &size, bool &eof, std::string &err)
    {
        if (!mount(err))
            return false;
        File f = SD.open(path.c_str());
        if (!f || f.isDirectory())
        {
            err = "no such file";
            mounted = false;
            return false;
        }
        size = (uint32_t)f.size();
        if (offset > size || !f.seek(offset))
        {
            f.close();
            err = "offset past the end";
            return false;
        }
        uint32_t pos = offset, sent = 0;
        std::string line;
        eof = false;
        while (true)
        {
            const int c = f.read();
            if (c < 0)
            {
                // Last line without a newline
                if (!line.empty())
                    fn(line);
                pos = size;
                eof = true;
                break;
            }
            pos++;
            if (c == '\n')
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                fn(line);
                sent += line.size() + 1;
                line.clear();
                if (sent >= maxBytes)
                    break;
                continue;
            }
            line += (char)c;
            if (line.size() > 180) // a line longer than the protocol allows: cut it here
            {
                fn(line);
                sent += line.size();
                line.clear();
                if (sent >= maxBytes)
                    break;
            }
        }
        if (pos >= size)
            eof = true;
        next = pos;
        f.close();
        return true;
    }
}
