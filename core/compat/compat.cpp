// Linux implementation of the Arduino / SdFat subset in Arduino.h and SdFat.h
#include "Arduino.h"
#include "SdFat.h"

#include <algorithm>
#include <chrono>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <thread>
#include <unistd.h>

// ---- time -------------------------------------------------------------------------------
static const auto T0 = std::chrono::steady_clock::now();

uint32_t millis()
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - T0).count();
}

uint32_t micros()
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - T0).count();
}

void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

char *itoa(int value, char *buf, int base)
{
    if (base == 16)
        sprintf(buf, "%x", value);
    else
        sprintf(buf, "%d", value);
    return buf;
}

char *dtostrf(double v, signed char width, unsigned char prec, char *buf)
{
    sprintf(buf, "%*.*f", (int)width, (int)prec, v);
    return buf;
}

// ---- Print --------------------------------------------------------------------------------
size_t Print::print(long v, int base)
{
    char b[32];
    snprintf(b, sizeof(b), base == 16 ? "%lX" : "%ld", v);
    return write(b);
}

size_t Print::print(unsigned long v, int base)
{
    char b[32];
    snprintf(b, sizeof(b), base == 16 ? "%lX" : "%lu", v);
    return write(b);
}

size_t Print::print(double v, int digits)
{
    char b[48];
    snprintf(b, sizeof(b), "%.*f", digits, v);
    return write(b);
}

// ---- file system --------------------------------------------------------------------------
static std::string g_root = ".";

void sdfs_set_root(const std::string &root)
{
    g_root = root;
    while (g_root.size() > 1 && g_root.back() == '/')
        g_root.pop_back();
}

const std::string &sdfs_root() { return g_root; }

std::string sdfs_path(const char *path)
{
    std::string p = path ? path : "";
    if (p.empty() || p[0] != '/')
        p = "/" + p;
    // No escaping the data folder
    if (p.find("/../") != std::string::npos || (p.size() >= 3 && p.compare(p.size() - 3, 3, "/..") == 0))
        return g_root + "/__invalid__";
    return g_root + p;
}

static bool isDirPath(const std::string &real)
{
    struct stat st;
    return stat(real.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool File::open(const char *path, oflag_t flags)
{
    close();
    real_ = sdfs_path(path);
    const char *slash = strrchr(path, '/');
    name_ = slash ? slash + 1 : path;
    if (isDirPath(real_))
    {
        if (flags & (O_WRONLY | O_RDWR))
            return false;
        isDir_ = true;
        entries_.clear();
        if (DIR *d = opendir(real_.c_str()))
        {
            while (dirent *e = readdir(d))
                if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
                    entries_.push_back(e->d_name);
            closedir(d);
        }
        std::sort(entries_.begin(), entries_.end());
        next_ = 0;
        return true;
    }
    const char *mode = "rb";
    if (flags & (O_WRONLY | O_RDWR))
        mode = (flags & O_TRUNC) ? (flags & O_RDWR ? "w+b" : "wb") : ((flags & O_APPEND) ? "ab" : "r+b");
    fp_ = fopen(real_.c_str(), mode);
    if (!fp_ && (flags & O_CREAT) && !(flags & O_TRUNC))
        fp_ = fopen(real_.c_str(), "w+b");
    return fp_ != nullptr;
}

bool File::openNext(File *dir, oflag_t flags)
{
    close();
    if (!dir || !dir->isDir_)
        return false;
    while (dir->next_ < dir->entries_.size())
    {
        const size_t i = dir->next_++;
        const std::string rel = dir->real_.substr(g_root.size()) + "/" + dir->entries_[i];
        if (open(rel.c_str(), flags))
        {
            index_ = (uint16_t)i;
            return true;
        }
    }
    return false;
}

bool File::close()
{
    bool ok = true;
    if (fp_)
        ok = fclose(fp_) == 0;
    fp_ = nullptr;
    isDir_ = false;
    entries_.clear();
    next_ = 0;
    return ok;
}

int File::read() { return fp_ ? fgetc(fp_) : -1; }

int File::read(void *buf, size_t n) { return fp_ ? (int)fread(buf, 1, n, fp_) : -1; }

int File::peek()
{
    if (!fp_)
        return -1;
    const int c = fgetc(fp_);
    if (c >= 0)
        ungetc(c, fp_);
    return c;
}

int File::available()
{
    if (!fp_)
        return 0;
    const long rest = (long)fileSize() - (long)curPosition();
    return rest > 0 ? (int)std::min<long>(rest, 0x7FFF) : 0;
}

size_t File::write(uint8_t c) { return fp_ && fputc(c, fp_) != EOF ? 1 : 0; }

size_t File::write(const uint8_t *buf, size_t n) { return fp_ ? fwrite(buf, 1, n, fp_) : 0; }

int File::fgets(char *buf, int size)
{
    if (!fp_ || size <= 1)
        return 0;
    if (!::fgets(buf, size, fp_))
        return 0;
    return (int)strlen(buf);
}

bool File::seekSet(uint32_t pos)
{
    if (isDir_)
    {
        next_ = pos / 32; // folder entries are "32 bytes" each, like FAT
        return true;
    }
    return fp_ && fseek(fp_, (long)pos, SEEK_SET) == 0;
}

uint32_t File::curPosition() const
{
    if (isDir_)
        return (uint32_t)(next_ * 32);
    return fp_ ? (uint32_t)ftell(fp_) : 0;
}

uint32_t File::fileSize() const
{
    struct stat st;
    if (fp_ && fstat(fileno(fp_), &st) == 0)
        return (uint32_t)st.st_size;
    return 0;
}

bool File::getName(char *buf, size_t size) const
{
    if (!size)
        return false;
    strncpy(buf, name_.c_str(), size - 1);
    buf[size - 1] = '\0';
    return true;
}

void File::rewind()
{
    if (isDir_)
        next_ = 0;
    else if (fp_)
        fseek(fp_, 0, SEEK_SET);
}

bool File::sync() { return fp_ ? fflush(fp_) == 0 : false; }

// ---- SdFat ----------------------------------------------------------------------------------
bool SdFat::exists(const char *path) { return access(sdfs_path(path).c_str(), F_OK) == 0; }

bool SdFat::mkdir(const char *path, bool parents)
{
    const std::string real = sdfs_path(path);
    if (!parents)
        return ::mkdir(real.c_str(), 0775) == 0;
    std::string cur;
    size_t pos = g_root.size();
    bool ok = true;
    while (pos != std::string::npos)
    {
        pos = real.find('/', pos + 1);
        cur = real.substr(0, pos);
        if (!isDirPath(cur))
            ok = ::mkdir(cur.c_str(), 0775) == 0;
    }
    return ok && isDirPath(real);
}

bool SdFat::rmdir(const char *path) { return ::rmdir(sdfs_path(path).c_str()) == 0; }

bool SdFat::remove(const char *path) { return ::unlink(sdfs_path(path).c_str()) == 0; }

bool SdFat::rename(const char *from, const char *to)
{
    if (exists(to))
        return false; // like FAT: no overwrite
    return ::rename(sdfs_path(from).c_str(), sdfs_path(to).c_str()) == 0;
}

uint32_t SdFat::freeClusterCount()
{
    struct statvfs v;
    if (statvfs(g_root.c_str(), &v) != 0)
        return 0;
    return (uint32_t)((uint64_t)v.f_bavail * v.f_frsize / bytesPerCluster());
}

SdCardInfo *SdFat::card()
{
    struct statvfs v;
    info_.sectors = statvfs(g_root.c_str(), &v) == 0 ? (uint64_t)v.f_blocks * v.f_frsize / 512 : 0;
    return &info_;
}
