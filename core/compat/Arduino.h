/**
 * Arduino.h - the small part of the Arduino API the ported ATmega controller code uses, on Linux
 * -----------------------------------------------------------------------------
 * The job runner, settings, feeders and protocol handlers (../ctrl/, ported from
 * ../../../src/) were written for an ATmega2560. They keep their code; this header
 * maps PROGMEM helpers to plain C, and Print / Stream to std streams.
 * -----------------------------------------------------------------------------
 */
#ifndef PAP_COMPAT_ARDUINO_H
#define PAP_COMPAT_ARDUINO_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>

// ---- PROGMEM: plain memory on Linux --------------------------------------------------
#define PROGMEM
typedef const char *PGM_P;
class __FlashStringHelper;
#define PSTR(s) (s)
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(s))
#define strcasecmp_P strcasecmp
#define strncasecmp_P strncasecmp
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strcat_P strcat
#define strlen_P strlen
#define strchr_P strchr
#ifndef __LGFX_PGMSPACE_H__ // LovyanGFX has its own (and skips it when this one came first)
#define PAP_COMPAT_MEMCPY_P
static inline void *memcpy_P(void *dst, const void *src, size_t n) { return memcpy(dst, src, n); }
#endif
#define snprintf_P snprintf
#define sprintf_P sprintf
#ifndef pgm_read_byte
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_ptr(p) (*(void *const *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#endif

#define DEC 10
#define HEX 16

// ---- time ------------------------------------------------------------------------------
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);

using std::max;
using std::min;

template <class T, class L, class H>
inline T constrain(T v, L lo, H hi) { return v < (T)lo ? (T)lo : (v > (T)hi ? (T)hi : v); }

char *itoa(int value, char *buf, int base);
char *dtostrf(double v, signed char width, unsigned char prec, char *buf);

// ---- Print / Stream ------------------------------------------------------------------------
class Print
{
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *buf, size_t n)
    {
        for (size_t i = 0; i < n; i++)
            write(buf[i]);
        return n;
    }
    size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
    size_t write(const char *s, size_t n) { return write((const uint8_t *)s, n); }

    size_t print(const char *s) { return write(s); }
    size_t print(const std::string &s) { return write(s.data(), s.size()); }
    size_t print(const __FlashStringHelper *s) { return write(reinterpret_cast<const char *>(s)); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v, int base = DEC) { return print((long)v, base); }
    size_t print(unsigned v, int base = DEC) { return print((unsigned long)v, base); }
    size_t print(long v, int base = DEC);
    size_t print(unsigned long v, int base = DEC);
    size_t print(long long v, int base = DEC) { return print((long)v, base); }
    size_t print(unsigned long long v, int base = DEC) { return print((unsigned long)v, base); }
    size_t print(double v, int digits = 2);

    size_t println() { return write((const uint8_t *)"\n", 1); }
    template <class T>
    size_t println(const T &v)
    {
        size_t n = print(v);
        return n + println();
    }
    template <class T>
    size_t println(const T &v, int fmt)
    {
        size_t n = print(v, fmt);
        return n + println();
    }
};

class Stream : public Print
{
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() {}
};

// Print into a std::string (answers collected for the API / UI channels)
class StringPrint : public Print
{
public:
    std::string s;
    size_t write(uint8_t c) override
    {
        s += (char)c;
        return 1;
    }
    size_t write(const uint8_t *b, size_t n) override
    {
        s.append((const char *)b, n);
        return n;
    }
    using Print::write;
};

#endif // PAP_COMPAT_ARDUINO_H
