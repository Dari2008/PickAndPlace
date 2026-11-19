#include "pap_text.h"

namespace pap
{
    bool readLine(File &f, char *buf, bool &tooLong)
    {
        tooLong = false;
        uint8_t n = 0;
        int c;
        bool any = false;
        while ((c = f.read()) >= 0)
        {
            any = true;
            if (c == '\n')
                break;
            if (c == '\r')
                continue;
            if (n < PAP_LINE_MAX)
                buf[n++] = (char)c;
            else
                tooLong = true;
        }
        buf[n] = '\0';
        return any;
    }

    char *trim(char *s)
    {
        while (*s == ' ' || *s == '\t')
            s++;
        char *end = s + strlen(s);
        while (end > s && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
        return s;
    }

    char *clean(char *s)
    {
        bool inStr = false;
        for (char *c = s; *c; c++)
        {
            if (*c == '"')
                inStr = !inStr;
            else if (*c == '#' && !inStr && (c == s || c[-1] == ' ' || c[-1] == '\t'))
            {
                *c = '\0';
                break;
            }
        }
        return trim(s);
    }

    bool header(char *s, char *&kind, char *&name)
    {
        if (s[0] != '[')
            return false;
        char *close = strchr(s, ']');
        if (!close)
            return false;
        *close = '\0';
        kind = trim(s + 1);
        name = kind;
        while (*name && *name != ' ' && *name != '\t')
            name++;
        if (*name)
            *name++ = '\0';
        name = trim(name);
        return true;
    }

    bool keyValue(char *s, char *&key, char *&value)
    {
        char *eq = strchr(s, '=');
        if (!eq)
            return false;
        *eq = '\0';
        key = trim(s);
        value = trim(eq + 1);
        return *key != '\0';
    }

    bool number(const char *s, float &v)
    {
        char *end;
        v = strtod(s, &end);
        while (*end == ' ')
            end++;
        return end != s && *end == '\0';
    }

    uint8_t point(const char *s, float *v, uint8_t minCount, uint8_t maxCount)
    {
        uint8_t n = 0;
        const char *p = s;
        while (true)
        {
            char *end;
            const float x = strtod(p, &end);
            if (end == p || n >= maxCount)
                return 0;
            v[n++] = x;
            while (*end == ' ')
                end++;
            if (*end == '\0')
                break;
            if (*end != ',')
                return 0;
            p = end + 1;
        }
        return n >= minCount ? n : 0;
    }

    uint16_t hash(const char *s)
    {
        uint16_t h = 0x811C;
        for (; *s; s++)
        {
            char c = *s;
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            h = (h ^ (uint8_t)c) * 0x0193;
        }
        return h ? h : 1;
    }
}
