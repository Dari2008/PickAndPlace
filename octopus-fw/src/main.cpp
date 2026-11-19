/**
 * pap-motion - motion firmware for the pick & place machine on a BTT Octopus V1.1
 * -----------------------------------------------------------------------------
 * The Raspberry Pi 5 talks to it over USB (CDC serial, /dev/ttyACM0) with the
 * text protocol of ../protocol.md (motion part). See ../README.md of this folder.
 * -----------------------------------------------------------------------------
 */
#include <Arduino.h>

#include "app.h"
#include "bootlog.h"
#include "motion/motion.h"
#include "outputs.h"
#include "lights.h"
#include "loadcell.h"
#include "settings.h"
#include "tmc.h"
#include "toolbus.h"

namespace
{
    struct UsbSink : Sink
    {
        void write(const char *data, size_t len) override
        {
            // Drop output when no host is connected instead of blocking the firmware
            if (SerialUSB)
                SerialUSB.write((const uint8_t *)data, len);
        }
    } usb;
}

void setup()
{
    bootlog::begin(); // BOOTLOG.TXT on the microSD card (start-up diagnostics)
    cfg::init();
    bootlog::step("settings");
    outputs.begin(); // MOSFETs off before anything else
    outputs.fansOn(); // head camera, housing and Pi housing fan: always on
    bootlog::step("outputs, fans");
    motion.begin();  // drivers disabled (EN high) until the first move
    bootlog::step("motion");
    lights::begin(); // top camera light on (always), NeoPixel ring dark
    bootlog::step("lights");
    loadcell::begin(); // HX711 on EXP1 (optional)
    bootlog::step("load cell");
    toolbus_begin();   // tool changer board on the TFT header
    bootlog::step("tool bus");
    SerialUSB.begin(115200);
    tmcs.begin();    // current, microsteps, StallGuard for all 8 slots
    bootlog::step("TMC drivers");
    app::begin(usb);
    bootlog::step("ready, loop running");
}

void loop()
{
    bootlog::poll();
    toolbus_poll();
    app::loop();
}
