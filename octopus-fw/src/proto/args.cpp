#include "args.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

Args::Args(char *p)
{
    while (*p && n_ < MAX)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '#')
            break;
        if (*p == '"')
        {
            w_[n_++] = ++p;
            while (*p && *p != '"')
                p++;
        }
        else
        {
            w_[n_++] = p;
            while (*p && *p != ' ' && *p != '\t')
            {
                if (*p == '"') // name="a b" -> name=a b
                {
                    memmove(p, p + 1, strlen(p));
                    while (*p && *p != '"')
                        p++;
                    if (*p)
                        memmove(p, p + 1, strlen(p));
                    continue;
                }
                p++;
            }
        }
        if (*p)
            *p++ = '\0';
    }
}

bool Args::is(uint8_t i, const char *s) const
{
    return i < n_ && !strcasecmp(w_[i], s);
}

const char *Args::value(const char *key) const
{
    const size_t len = strlen(key);
    for (uint8_t i = 1; i < n_; i++)
        if (!strncasecmp(w_[i], key, len) && w_[i][len] == '=')
            return w_[i] + len + 1;
    return nullptr;
}

bool Args::number(const char *s, float &out)
{
    char *end;
    out = strtof(s, &end);
    return end != s && *end == '\0';
}

bool Args::kv(const char *key, float &out, bool &bad) const
{
    const char *v = value(key);
    if (!v)
        return false;
    if (!number(v, out))
        bad = true;
    return true;
}
