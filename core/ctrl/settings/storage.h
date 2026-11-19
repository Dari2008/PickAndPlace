/**
 * storage.h - settings and job files in the data folder of the Pi (default ~/pap-data)
 * -----------------------------------------------------------------------------
 * Same interface as the ATmega's SD card storage (../../../../src/settings/storage.h):
 *   /settings.txt            general settings (general_settings.h)
 *   /machine.txt             Octopus machine values, sent with SET on every connect
 *   /feeders/feederNN.txt    one file per feeder (feeder_store.h)
 *   /jobs, /tmp, /captures   jobs, uploads, placement photos
 * Files are plain "key = value" text, '#' starts a comment.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <SdFat.h>

class Storage
{
public:
    typedef void (*KvHandler)(const char *key, const char *value, void *ctx);

    bool ready();   // data folder exists (created on first use)
    bool remount() { return ready(); }
    SdFat &fs() { return sd_; }
    bool mounted() { return ready(); }
    bool readKv(const char *path, KvHandler handler, void *ctx);
    bool openWrite(File &file, const char *path);
    bool openNext(File &dir, File &entry) { return entry.openNext(&dir, O_RDONLY); }
    uint8_t fatType() { return 32; }
    PGM_P lastError() const { return error_; }

private:
    SdFat sd_;
    PGM_P error_ = nullptr;
    bool checked_ = false;
};

extern Storage storage;
