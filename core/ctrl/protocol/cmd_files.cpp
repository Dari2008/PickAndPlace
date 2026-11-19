// SD PWD CD LS MKDIR RMDIR RM MV CAT UPLOAD
#include "commands.h"
#include "../settings/storage.h"
#include "../job/runner.h"

#include <mutex>

static const char TMP_DIR[] PROGMEM = "/tmp";
static const char UPLOAD_TMP[] PROGMEM = "/tmp/~upload.tmp";
static const uint8_t PAP_SEARCH_DEPTH = 4; // LS -p looks this deep for .pap files

bool sdMounted()
{
    return storage.mounted();
}

static bool needCard(Port &port)
{
    if (storage.ready())
        return true;
    out::error(port.io, PSTR("no SD card"));
    return false;
}

// Path argument `i` resolved, or an error printed.
static bool pathArg(Port &port, Args &args, uint8_t i, char *path)
{
    if (args.count() <= i)
    {
        out::error(port.io, PSTR("missing path"));
        return false;
    }
    if (!commands::resolvePath(port, args[i], path))
    {
        out::error(port.io, PSTR("path too long"));
        return false;
    }
    return true;
}

static bool isPap(const char *name)
{
    const uint8_t len = strlen(name);
    return len > 4 && !strcasecmp_P(name + len - 4, PSTR(".pap"));
}

static bool isRunningFile(const char *path)
{
    return runner.active() && !strcasecmp(runner.file(), path);
}

// ---------------------------------------------------------------------------

void cmdSd(Port &port, Args &args)
{
    Print &p = port.io;
    if (args.is(1, PSTR("MOUNT")))
    {
        if (runner.active())
        {
            out::error(p, PSTR("busy: a job is running"));
            return;
        }
        if (!storage.remount())
        {
            out::error(p, PSTR("no SD card"));
            return;
        }
    }
    if (!needCard(port))
        return;
    SdFat &sd = storage.fs();
    out::rec(p, PSTR("sd"));
    if (args.is(1, PSTR("FREE")))
    {
        const uint32_t free = sd.freeClusterCount();
        out::num(p, PSTR("free_mb"), (long)((uint64_t)free * sd.bytesPerCluster() / 1048576UL));
    }
    else
    {
        out::num(p, PSTR("mounted"), 1);
        const uint8_t t = sd.fatType();
        out::strP(p, PSTR("type"), t == 32 ? PSTR("FAT32") : t == 16 ? PSTR("FAT16") : PSTR("exFAT"));
        out::num(p, PSTR("size_mb"), (long)(sd.card()->sectorCount() / 2048UL));
    }
    p.println();
    out::ok(p);
}

void cmdPwd(Port &port, Args &)
{
    out::rec(port.io, PSTR("pwd"));
    out::str(port.io, PSTR("path"), port.cwd);
    port.io.println();
    out::ok(port.io);
}

void cmdCd(Port &port, Args &args)
{
    char path[PATH_MAX_LEN];
    if (!pathArg(port, args, 1, path) || !needCard(port))
        return;
    File dir;
    if (!dir.open(path, O_RDONLY) || !dir.isDir())
    {
        out::error2(port.io, PSTR("not a folder:"), path);
        return;
    }
    dir.close();
    strcpy(port.cwd, path);
    out::ok(port.io);
}

// True if `dir` holds a .pap file at most `depth` folder levels down.
static bool containsPap(File &dir, uint8_t depth)
{
    File entry;
    char name[64];
    bool found = false;
    dir.rewind();
    while (!found && storage.openNext(dir, entry))
    {
        entry.getName(name, sizeof(name));
        if (entry.isDir())
            found = depth > 0 && name[0] != '.' && containsPap(entry, depth - 1);
        else
            found = isPap(name);
        entry.close();
    }
    return found;
}

void cmdLs(Port &port, Args &args)
{
    const bool papOnly = args.is(1, PSTR("-p"));
    const uint8_t pathIndex = papOnly ? 2 : 1;
    char path[PATH_MAX_LEN];
    if (args.count() > pathIndex)
    {
        if (!pathArg(port, args, pathIndex, path))
            return;
    }
    else
        strcpy(path, port.cwd);
    if (!needCard(port))
        return;

    File dir;
    if (!dir.open(path, O_RDONLY) || !dir.isDir())
    {
        out::error2(port.io, PSTR("not a folder:"), path);
        return;
    }
    Print &p = port.io;
    File entry;
    char name[64];
    while (storage.openNext(dir, entry))
    {
        entry.getName(name, sizeof(name));
        if (entry.isDir())
        {
            if (!papOnly || (name[0] != '.' && containsPap(entry, PAP_SEARCH_DEPTH)))
            {
                out::rec(p, PSTR("dir"));
                out::str(p, PSTR("name"), name);
                p.println();
            }
        }
        else if (!papOnly || isPap(name))
        {
            out::rec(p, PSTR("file"));
            out::str(p, PSTR("name"), name);
            out::num(p, PSTR("size"), (long)entry.fileSize());
            p.println();
        }
        entry.close();
    }
    dir.close();
    out::ok(p);
}

void cmdMkdir(Port &port, Args &args)
{
    char path[PATH_MAX_LEN];
    if (!pathArg(port, args, 1, path) || !needCard(port))
        return;
    if (!storage.fs().mkdir(path, true))
        out::error2(port.io, PSTR("can't create"), path);
    else
        out::ok(port.io);
}

void cmdRmdir(Port &port, Args &args)
{
    char path[PATH_MAX_LEN];
    if (!pathArg(port, args, 1, path) || !needCard(port))
        return;
    if (!storage.fs().rmdir(path))
        out::error2(port.io, PSTR("can't delete (not empty?)"), path);
    else
        out::ok(port.io);
}

void cmdRm(Port &port, Args &args)
{
    char path[PATH_MAX_LEN];
    if (!pathArg(port, args, 1, path) || !needCard(port))
        return;
    if (!isPap(path))
    {
        out::error(port.io, PSTR("only .pap files can be deleted"));
        return;
    }
    if (isRunningFile(path))
    {
        out::error(port.io, PSTR("the job is running this file"));
        return;
    }
    if (!storage.fs().remove(path))
        out::error2(port.io, PSTR("can't delete"), path);
    else
        out::ok(port.io);
}

void cmdMv(Port &port, Args &args)
{
    char from[PATH_MAX_LEN], to[PATH_MAX_LEN];
    if (!pathArg(port, args, 1, from) || !pathArg(port, args, 2, to) || !needCard(port))
        return;
    if (!isPap(from) || !isPap(to))
    {
        out::error(port.io, PSTR("only .pap files can be moved"));
        return;
    }
    if (isRunningFile(from))
    {
        out::error(port.io, PSTR("the job is running this file"));
        return;
    }
    if (!storage.fs().rename(from, to))
        out::error(port.io, PSTR("can't move (target exists?)"));
    else
        out::ok(port.io);
}

void cmdCat(Port &port, Args &args)
{
    char path[PATH_MAX_LEN];
    if (!pathArg(port, args, 1, path) || !needCard(port))
        return;
    File f;
    if (!f.open(path, O_RDONLY) || f.isDir())
    {
        out::error2(port.io, PSTR("can't open"), path);
        return;
    }
    char line[LINE_MAX - 4];
    int n;
    while ((n = f.fgets(line, sizeof(line))) > 0)
    {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        port.io.write('|');
        port.io.println(line);
    }
    f.close();
    out::ok(port.io);
}

// ---------------------------------------------------------------------------
// Upload
// ---------------------------------------------------------------------------

static struct
{
    Port *port = nullptr;
    File file;
    char target[PATH_MAX_LEN];
    uint32_t crc;
    uint32_t bytes;
} up;

static uint32_t crcUpdate(uint32_t crc, uint8_t c)
{
    crc ^= c;
    for (uint8_t k = 0; k < 8; k++)
        crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
    return crc;
}

// Upload progress for the display's banner (read from the UI thread)
static std::mutex upInfoM;
static UploadInfo upInfo;

UploadInfo uploadInfo()
{
    std::lock_guard<std::mutex> l(upInfoM);
    return upInfo;
}

static void upInfoSet(bool active, bool ok, const char *error)
{
    std::lock_guard<std::mutex> l(upInfoM);
    upInfo.active = active;
    upInfo.ok = ok;
    upInfo.error = error ? error : "";
    upInfo.done = up.bytes;
    if (!active)
        upInfo.endedAt = millis();
}

bool uploadActive(const Port &port)
{
    return up.port == &port;
}

void cmdUpload(Port &port, Args &args)
{
    if (up.port)
    {
        out::error(port.io, PSTR("busy: another upload is running"));
        return;
    }
    if (args.count() < 2)
    {
        out::error(port.io, PSTR("usage: UPLOAD <name.pap>"));
        return;
    }
    // A bare name goes to /tmp; a path with a folder is used as given.
    char path[PATH_MAX_LEN];
    if (!strchr(args[1], '/'))
    {
        if (strlen(args[1]) + 6 >= PATH_MAX_LEN)
        {
            out::error(port.io, PSTR("path too long"));
            return;
        }
        strcpy_P(path, TMP_DIR);
        strcat_P(path, PSTR("/"));
        strcat(path, args[1]);
    }
    else if (!pathArg(port, args, 1, path))
        return;
    if (!isPap(path))
    {
        out::error(port.io, PSTR("only .pap files can be uploaded"));
        return;
    }
    if (isRunningFile(path))
    {
        out::error(port.io, PSTR("the job is running this file"));
        return;
    }
    if (!needCard(port))
        return;
    char tmp[24];
    strcpy_P(tmp, TMP_DIR);
    storage.fs().mkdir(tmp, true); // fails harmlessly when it exists
    strcpy_P(tmp, UPLOAD_TMP);
    if (!storage.openWrite(up.file, tmp))
    {
        out::error(port.io, PSTR("can't write to the SD card"));
        return;
    }
    strcpy(up.target, path);
    up.crc = 0xFFFFFFFFUL;
    up.bytes = 0;
    up.port = &port;
    {
        std::lock_guard<std::mutex> l(upInfoM);
        upInfo = UploadInfo();
        upInfo.active = true;
        upInfo.path = path;
        upInfo.total = args.count() > 2 ? strtoul(args[2], nullptr, 10) : 0; // UPLOAD <path> [bytes]
        upInfo.fromUi = port.channel && port.channel->name == "ui";
    }
    out::ok(port.io);
}

static void uploadEnd()
{
    up.file.close();
    up.port = nullptr;
}

void uploadLine(Port &port, char *line)
{
    Print &p = port.io;
    if (line[0] == '|')
    {
        const char *data = line + 1;
        const size_t len = strlen(data);
        if (up.file.write(data, len) != len || up.file.write('\n') != 1)
        {
            char tmp[24];
            strcpy_P(tmp, UPLOAD_TMP);
            uploadEnd();
            storage.fs().remove(tmp);
            upInfoSet(false, false, "write failed");
            out::error(p, PSTR("SD write failed, upload cancelled"));
            return;
        }
        for (size_t i = 0; i < len; i++)
            up.crc = crcUpdate(up.crc, data[i]);
        up.crc = crcUpdate(up.crc, '\n');
        up.bytes += len + 1;
        {
            std::lock_guard<std::mutex> l(upInfoM);
            upInfo.done = up.bytes;
        }
        out::ok(p);
        return;
    }

    Args args(line);
    char tmp[24];
    strcpy_P(tmp, UPLOAD_TMP);
    if (args.is(0, PSTR("ABORT")))
    {
        uploadEnd();
        storage.fs().remove(tmp);
        upInfoSet(false, false, "cancelled");
        out::ok(p);
        return;
    }
    if (!args.is(0, PSTR("END")))
    {
        out::error(p, PSTR("upload running: send |line, END or ABORT"));
        return;
    }
    const uint32_t crc = up.crc ^ 0xFFFFFFFFUL;
    if (args.count() > 1 && strtoul(args[1], nullptr, 16) != crc)
    {
        uploadEnd();
        storage.fs().remove(tmp);
        char got[10];
        sprintf_P(got, PSTR("%08lX"), (unsigned long)crc);
        upInfoSet(false, false, "CRC mismatch");
        out::error2(p, PSTR("CRC mismatch, file not saved; got"), got);
        return;
    }
    const bool closed = up.file.close();
    up.port = nullptr;
    SdFat &sd = storage.fs();
    // Create the target folder if needed, then replace the old file.
    char dir[PATH_MAX_LEN];
    strcpy(dir, up.target);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir)
    {
        *slash = '\0';
        sd.mkdir(dir, true);
    }
    if (sd.exists(up.target))
        sd.remove(up.target);
    if (!closed || !sd.rename(tmp, up.target))
    {
        upInfoSet(false, false, "can't save");
        out::error2(p, PSTR("can't save"), up.target);
        return;
    }
    upInfoSet(false, true, nullptr);
    out::rec(p, PSTR("saved"));
    out::str(p, PSTR("path"), up.target);
    out::num(p, PSTR("bytes"), (long)up.bytes);
    p.println();
    out::ok(p);
}
