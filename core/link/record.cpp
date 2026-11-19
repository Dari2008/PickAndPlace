#include "record.h"
#include <cstdlib>
#include <cstring>

Record Record::parse(const std::string &line)
{
    Record r;
    if (!line.empty() && line[0] == '|')
    {
        r.type = "|";
        r.text = line.substr(1);
        return r;
    }
    r.text = line;
    size_t i = line[0] == '!' ? 1 : 0;
    const size_t n = line.size();
    while (i < n && line[i] != ' ')
        r.type += line[i++];
    while (i < n)
    {
        while (i < n && line[i] == ' ')
            i++;
        if (i >= n)
            break;
        std::string key;
        while (i < n && line[i] != '=' && line[i] != ' ')
            key += line[i++];
        std::string value;
        if (i < n && line[i] == '=')
        {
            i++;
            if (i < n && line[i] == '"')
            {
                i++;
                while (i < n && line[i] != '"')
                    value += line[i++];
                i++; // closing quote
            }
            else
                while (i < n && line[i] != ' ')
                    value += line[i++];
        }
        r.fields.emplace_back(std::move(key), std::move(value));
    }
    return r;
}

const std::string *Record::find(const char *key) const
{
    for (auto &f : fields)
        if (f.first == key)
            return &f.second;
    return nullptr;
}

std::string Record::str(const char *key, const char *def) const
{
    const std::string *v = find(key);
    return v ? *v : std::string(def);
}

float Record::num(const char *key, float def) const
{
    const std::string *v = find(key);
    return v && !v->empty() ? strtof(v->c_str(), nullptr) : def;
}

long Record::integer(const char *key, long def) const
{
    const std::string *v = find(key);
    return v && !v->empty() ? strtol(v->c_str(), nullptr, 10) : def;
}
