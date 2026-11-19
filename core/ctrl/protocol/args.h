/**
 * args.h - split a command line into words (protocol.md, "Lines")
 * -----------------------------------------------------------------------------
 *   Args a(line);              // line is modified in place
 *   a.count()                  // number of words
 *   a[0]                       // command word
 *   a.is(1, PSTR("ADD"))       // word 1 equals "ADD" (any case)
 *   a.kv(PSTR("x"), v)         // a "x=12.5" word anywhere -> v = 12.5
 *
 * Words are separated by spaces; "quoted text" is one word without the
 * quotes. A '#' at the start of a word starts a comment.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <Arduino.h>

class Args
{
public:
    static const uint8_t MAX = 10;

    explicit Args(char *line);

    uint8_t count() const { return n_; }
    const char *operator[](uint8_t i) const { return i < n_ ? w_[i] : ""; }

    // Word i equals `p` (PROGMEM), any case.
    bool is(uint8_t i, PGM_P p) const { return i < n_ && !strcasecmp_P(w_[i], p); }

    // Value of a "key=value" word (key any case). nullptr if not given.
    const char *value(PGM_P key) const;

    // Numeric "key=value". False if missing; `bad` set when given but not a number.
    bool kv(PGM_P key, float &out, bool &bad) const;

    // Parse a whole word as a number.
    static bool number(const char *s, float &out);

private:
    char *w_[MAX];
    uint8_t n_ = 0;
};
