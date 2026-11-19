/**
 * record.h - one answer or event line of the controller protocol
 * -----------------------------------------------------------------------------
 *   file name="my job.pap" size=2817      -> type "file", fields name / size
 *   !status state=idle x=3.00 ...          -> type "status" (event: '!' cut off)
 *   |[job]                                 -> type "|", text "[job]" (CAT lines)
 * See ../../../protocol.md.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

struct Record
{
    std::string type;
    std::vector<std::pair<std::string, std::string>> fields;
    std::string text; // the whole line (for "|" records: without the '|')

    static Record parse(const std::string &line);

    const std::string *find(const char *key) const;
    std::string str(const char *key, const char *def = "") const;
    float num(const char *key, float def = 0) const;
    long integer(const char *key, long def = 0) const;
    bool flag(const char *key) const { return integer(key) != 0; }
};
