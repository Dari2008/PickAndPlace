/**
 * pap_text.h - small text helpers for reading .pap files (pap-format.md)
 */
#pragma once

#include <Arduino.h>
#include <SdFat.h>

#define PAP_LINE_MAX 120 // longest allowed line (pap-format.md, Limits)

namespace pap
{
    // Next line of `f` into buf (without line end). False at the end of the
    // file. `tooLong` is set when the line had more than PAP_LINE_MAX
    // characters (the rest of it is skipped).
    bool readLine(File &f, char *buf, bool &tooLong);

    // Cut off a comment ('#' at the start or after a space, not inside
    // quotes) and surrounding spaces. Returns the trimmed text.
    char *clean(char *s);

    // "[part R1]" -> kind "part", name "R1". False if not a header.
    bool header(char *s, char *&kind, char *&name);

    // "key = value" -> key, value (trimmed). False if there is no '='.
    bool keyValue(char *s, char *&key, char *&value);

    bool number(const char *s, float &v);

    // "12.5, 8" -> v[0], v[1]. Accepts minCount..maxCount numbers.
    // Returns the count, 0 if malformed.
    uint8_t point(const char *s, float *v, uint8_t minCount, uint8_t maxCount);

    // 16-bit hash of a name, without case (function and feeder lookup).
    uint16_t hash(const char *s);

    char *trim(char *s);
}
