/**
 * ui_main.h - the display UI on the Pi (3.5" ILI9488 + touch, joystick): its own thread
 * -----------------------------------------------------------------------------
 * The ESP32 screens (../../../esp32-ui/src/ui) run here unchanged in look and use;
 * they talk to the controller through an in-process command port (Channel), the
 * same protocol the ESP32 used over the serial link.
 *   panel::start(sim);   // sim: no SPI display (renders into memory, mirror via SCREEN)
 * -----------------------------------------------------------------------------
 */
#pragma once

namespace panel
{
    void start(bool simulate);
    void stop();
}
