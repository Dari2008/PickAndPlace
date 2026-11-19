/**
 * args.h - split a command line into words (protocol.md, "Lines")
 * -----------------------------------------------------------------------------
 *   Args a(line);         // line is modified in place
 *   a.count(), a[0]       // words, command word
 *   a.is(1, "ON")         // word 1 equals "ON" (any case)
 *   a.kv("x", v, bad)     // "x=12.5" anywhere -> v = 12.5
 * Words are separated by spaces; "quoted text" is one word. '#' starts a comment.
 * Same rules as the ATmega firmware (src/protocol/args.cpp), without PROGMEM.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>

class Args
{
public:
    static const uint8_t MAX = 16;

    explicit Args(char *line);

    uint8_t count() const { return n_; }
    const char *operator[](uint8_t i) const { return i < n_ ? w_[i] : ""; }
    bool is(uint8_t i, const char *s) const;

    const char *value(const char *key) const; // value of "key=value" or nullptr
    bool kv(const char *key, float &out, bool &bad) const;
    static bool number(const char *s, float &out);

private:
    char *w_[MAX];
    uint8_t n_ = 0;
};
