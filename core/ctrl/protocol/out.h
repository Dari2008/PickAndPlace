/**
 * out.h - writing answers and events (protocol.md, "Answers")
 * -----------------------------------------------------------------------------
 *   out::rec(p, PSTR("file"));        // record type word
 *   out::str(p, PSTR("name"), name);  //  name=...  (quoted when needed)
 *   out::num(p, PSTR("size"), 2817);  //  size=2817
 *   out::flt(p, PSTR("x"), 12.5f, 2); //  x=12.50
 *   p.println();
 *   out::ok(p);  /  out::error(p, PSTR("no card"));
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <Arduino.h>

namespace out
{
    void rec(Print &p, PGM_P type);
    void str(Print &p, PGM_P key, const char *value);
    void strP(Print &p, PGM_P key, PGM_P value);
    void num(Print &p, PGM_P key, long value);
    void flt(Print &p, PGM_P key, float value, uint8_t decimals);
    void ok(Print &p);
    void error(Print &p, PGM_P text);
    void error2(Print &p, PGM_P text, const char *detail); // "error <text> <detail>"
}
