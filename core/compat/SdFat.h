/**
 * SdFat.h - the SdFat calls of the ported controller code, on the Linux file system
 * -----------------------------------------------------------------------------
 * Paths of the protocol ("/jobs/a.pap") live below the data folder (default
 * ~/pap-data, see sdfs_set_root()). A File is a regular file (stdio) or a folder
 * (sorted entry list for openNext). Only what ../ctrl uses is implemented.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <fcntl.h>
#include <string>
#include <vector>
#include "Arduino.h"

typedef int oflag_t;

void sdfs_set_root(const std::string &root);
const std::string &sdfs_root();
std::string sdfs_path(const char *path); // protocol path -> real path

class File : public Stream
{
public:
    File() {}
    ~File() override { close(); }
    File(const File &) = delete;
    File &operator=(const File &) = delete;

    bool open(const char *path, oflag_t flags = O_RDONLY);
    bool openNext(File *dir, oflag_t flags = O_RDONLY);
    bool close();
    bool isOpen() const { return fp_ || isDir_; }
    bool isDir() const { return isDir_; }
    bool isFile() const { return fp_ != nullptr; }

    int read() override;
    int read(void *buf, size_t n);
    int peek() override;
    int available() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buf, size_t n) override;
    using Print::write;
    int fgets(char *buf, int size); // like SdFat: line incl. '\n', returns length (0 = end)
    bool seekSet(uint32_t pos);
    uint32_t curPosition() const;
    uint32_t fileSize() const;
    bool getName(char *buf, size_t size) const;
    void rewind();
    bool sync();
    uint16_t dirIndex() const { return index_; }

private:
    FILE *fp_ = nullptr;
    bool isDir_ = false;
    std::string real_, name_;
    std::vector<std::string> entries_; // folder: sorted names
    size_t next_ = 0;                  // folder: next entry for openNext
    uint16_t index_ = 0;
};

struct SdCardInfo
{
    uint64_t sectors = 0;
    uint64_t sectorCount() const { return sectors; }
};

class SdFat
{
public:
    bool begin() { return true; }
    void end() {}
    bool exists(const char *path);
    bool mkdir(const char *path, bool parents = true);
    bool rmdir(const char *path);
    bool remove(const char *path);
    bool rename(const char *from, const char *to);
    uint8_t fatType() { return 32; }
    uint32_t freeClusterCount();
    uint32_t bytesPerCluster() { return 4096; }
    SdCardInfo *card();

private:
    SdCardInfo info_;
};
