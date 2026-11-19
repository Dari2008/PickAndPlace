/**
 * sdcard.h - the Octopus microSD slot (SDIO), read-only access for the Pi
 * -----------------------------------------------------------------------------
 * Jobs live on the Pi; this card is only a source to copy files from:
 *   SDLS [dir]                 file name= size= dir=0|1 ... ok      (one directory)
 *   SDREAD <path> [offset]     |line ... sdread offset=<next> size=<file size> eof=0|1 ok
 *                              (about 1 KB of whole lines per call; the Pi repeats until eof=1)
 * Slot: SDIO D0..D3 = PC8..PC11, CLK = PC12, CMD = PD2, card detect = PC14 (LOW = card in).
 * Only while no motion runs (the card is read in blocking mode).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <functional>
#include <stdint.h>
#include <string>

namespace sdcard
{
    bool list(const std::string &dir, std::function<void(const char *name, uint32_t size, bool isDir)> fn, std::string &err);
    // Whole lines from byte `offset`, about `maxBytes`; `next` = where to go on, eof when the file is done
    bool read(const std::string &path, uint32_t offset, uint32_t maxBytes,
              std::function<void(const std::string &line)> fn, uint32_t &next, uint32_t &size, bool &eof, std::string &err);
}
