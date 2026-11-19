/**
 * out.h - building answer records and events (protocol.md, "Answers")
 * -----------------------------------------------------------------------------
 *   Line l("status");  l.str("state", "idle");  l.num("busy", 0);  l.flt("x", 1.5f, 2);
 *   sink.line(l.s);     // "status state=idle busy=0 x=1.50"
 * Values with spaces, '=' or '"' are quoted ('"' inside becomes ').
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>
#include <string>

struct Line
{
    std::string s;
    explicit Line(const char *type) : s(type) {}
    Line &str(const char *key, const char *value);
    Line &str(const char *key, const std::string &value) { return str(key, value.c_str()); }
    Line &num(const char *key, long value);
    Line &flt(const char *key, float value, uint8_t decimals);
};

// Where answers go: the USB port (or a test buffer)
struct Sink
{
    virtual ~Sink() {}
    virtual void write(const char *data, size_t len) = 0;
    void line(const std::string &l);
    void line(const char *l);
    void ok() { line("ok"); }
    void error(const std::string &text) { line("error " + text); }
};

std::string quoteValue(const char *v);
