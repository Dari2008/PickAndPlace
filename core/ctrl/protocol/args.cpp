#include "args.h"

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
            // A quoted value inside a word: name="a b" -> name=a b
            while (*p && *p != ' ' && *p != '\t')
            {
                if (*p == '"')
                {
                    memmove(p, p + 1, strlen(p)); // drop the opening quote
                    while (*p && *p != '"')
                        p++;
                    if (*p)
                        memmove(p, p + 1, strlen(p)); // and the closing one
                    continue;
                }
                p++;
            }
        }
        if (*p)
            *p++ = '\0';
    }
}

const char *Args::value(PGM_P key) const
{
    const uint8_t len = strlen_P(key);
    for (uint8_t i = 1; i < n_; i++)
        if (!strncasecmp_P(w_[i], key, len) && w_[i][len] == '=')
            return w_[i] + len + 1;
    return nullptr;
}

bool Args::number(const char *s, float &out)
{
    char *end;
    out = strtod(s, &end);
    return end != s && *end == '\0';
}

bool Args::kv(PGM_P key, float &out, bool &bad) const
{
    const char *v = value(key);
    if (!v)
        return false;
    if (!number(v, out))
        bad = true;
    return true;
}
