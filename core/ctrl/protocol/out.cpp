#include "out.h"

namespace out
{
    void rec(Print &p, PGM_P type)
    {
        p.print((const __FlashStringHelper *)type);
    }

    static void key(Print &p, PGM_P k)
    {
        p.write(' ');
        p.print((const __FlashStringHelper *)k);
        p.write('=');
    }

    static void value(Print &p, const char *v)
    {
        bool quote = !*v;
        for (const char *c = v; *c && !quote; c++)
            if (*c == ' ' || *c == '=' || *c == '"' || *c == '\t')
                quote = true;
        if (!quote)
        {
            p.print(v);
            return;
        }
        p.write('"');
        for (const char *c = v; *c; c++)
            p.write(*c == '"' ? '\'' : *c);
        p.write('"');
    }

    void str(Print &p, PGM_P k, const char *v)
    {
        key(p, k);
        value(p, v);
    }

    void strP(Print &p, PGM_P k, PGM_P v)
    {
        char buf[64];
        strncpy_P(buf, v, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        str(p, k, buf);
    }

    void num(Print &p, PGM_P k, long v)
    {
        key(p, k);
        p.print(v);
    }

    void flt(Print &p, PGM_P k, float v, uint8_t decimals)
    {
        key(p, k);
        p.print(v, decimals);
    }

    void ok(Print &p)
    {
        p.println(F("ok"));
    }

    void error(Print &p, PGM_P text)
    {
        p.print(F("error "));
        p.println((const __FlashStringHelper *)text);
    }

    void error2(Print &p, PGM_P text, const char *detail)
    {
        p.print(F("error "));
        p.print((const __FlashStringHelper *)text);
        p.write(' ');
        p.println(detail);
    }
}
