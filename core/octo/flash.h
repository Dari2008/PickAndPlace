/**
 * flash.h - flash the Octopus firmware from the Pi (pap-core --flash [file.bin])
 * -----------------------------------------------------------------------------
 * 1. sends DFU to the running firmware: it restarts into the STM32's ROM USB bootloader
 *    (skipped when the board is already in DFU mode, e.g. BOOT0 jumper)
 * 2. dfu-util -d 0483:df11 -a 0 -s 0x08008000:leave -D file.bin (the BTT bootloader below
 *    0x08008000 stays), the new firmware starts right away
 * 3. waits until /dev/serial/by-id/...pap-motion... is back
 * Needs: sudo apt install dfu-util, and write access to the DFU device (udev rule, pi/README.md).
 * The first firmware with the DFU command has to go on once by microSD card.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <string>

// Default firmware file: <pap>/firmware/pap-motion.bin next to core/ (upload.bat puts it there)
std::string defaultFirmwarePath();

// dev: the Octopus serial port; returns true when the new firmware answers again
bool flashOctopus(const std::string &dev, const std::string &bin);
