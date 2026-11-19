#include "out.h"

#include <stdio.h>
#include <string.h>

std::string quoteValue(const char *v)
{
    bool quote = !*v;
    for (const char *c = v; *c && !quote; c++)
        if (*c == ' ' || *c == '=' || *c == '"' || *c == '\t')
            quote = true;
    if (!quote)
        return v;
    std::string r = "\"";
    for (const char *c = v; *c; c++)
        r += (*c == '"') ? '\'' : *c;
    r += '"';
    return r;
}

Line &Line::str(const char *key, const char *value)
{
    s += ' ';
    s += key;
    s += '=';
    s += quoteValue(value);
    return *this;
}

Line &Line::num(const char *key, long value)
{
    char buf[24];
    snprintf(buf, sizeof(buf), " %s=%ld", key, value);
    s += buf;
    return *this;
}

Line &Line::flt(const char *key, float value, uint8_t decimals)
{
    char buf[48];
    snprintf(buf, sizeof(buf), " %s=%.*f", key, (int)decimals, (double)value);
    s += buf;
    return *this;
}

void Sink::line(const std::string &l)
{
    write(l.data(), l.size());
    write("\n", 1);
}

void Sink::line(const char *l)
{
    write(l, strlen(l));
    write("\n", 1);
}
