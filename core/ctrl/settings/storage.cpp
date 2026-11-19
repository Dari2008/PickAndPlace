#include "storage.h"

#include <sys/stat.h>

Storage storage;

bool Storage::ready()
{
    if (checked_)
        return true;
    struct stat st;
    if (stat(sdfs_root().c_str(), &st) != 0)
        mkdir(sdfs_root().c_str(), 0775);
    checked_ = stat(sdfs_root().c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    if (!checked_)
    {
        error_ = "data folder missing";
        return false;
    }
    for (const char *d : {"/jobs", "/feeders", "/tmp", "/captures"})
        sd_.mkdir(d, true);
    error_ = nullptr;
    return true;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
    return s;
}

bool Storage::readKv(const char *path, KvHandler handler, void *ctx)
{
    if (!ready())
        return false;
    File file;
    if (!file.open(path, O_RDONLY) || file.isDir())
    {
        error_ = "can't read file";
        return false;
    }
    char line[256];
    while (file.fgets(line, sizeof(line)) > 0)
    {
        char *hash = line;
        while ((hash = strchr(hash, '#')) != nullptr)
        {
            if (hash == line || hash[-1] == ' ' || hash[-1] == '\t')
            {
                *hash = '\0';
                break;
            }
            hash++;
        }
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = trim(line);
        char *value = trim(eq + 1);
        if (*key)
            handler(key, value, ctx);
    }
    file.close();
    return true;
}

bool Storage::openWrite(File &file, const char *path)
{
    if (!ready())
        return false;
    if (file.open(path, O_WRONLY | O_CREAT | O_TRUNC))
        return true;
    error_ = "can't write file";
    return false;
}
