// The display UI thread: the ESP32's setup() / loop() (../../../esp32-ui/src/main.cpp) on the Pi
#include "ui_main.h"
#include "config.h"
#include "esp/ui.h"
#include "hw/backlight.h"
#include "hw/buzzer.h"
#include "hw/display.h"
#include "hw/input.h"
#include "hw/touch.h"
#include "link/link.h"
#include "machine/machine.h"
#include "net/wifi_setup.h"
#include <Arduino.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

extern std::string (*screenJpegHook)(int quality, int &w, int &h);
extern bool (*screenInputHook)(const char *what, int x, int y);
extern int (*screenTurnHook)(int want);
void setScreenTurned(bool on);  // esp/screens_settings.cpp
void openTouchCalibration();    // esp/screens_test.cpp

namespace panel
{
    static std::thread thread_;
    static std::atomic<bool> running_{false};
    static std::atomic<int> turnWanted_{-1}; // from the website (SCREEN TURN), applied in the UI thread
    static std::atomic<bool> calWanted_{false}; // SCREEN CALIBRATE: open the touch calibration

    static std::string jpegHook(int q, int &w, int &h) { return display.jpeg(q, w, h); }

    static int turnHook(int want)
    {
        if (want < 0)
        {
            const int pending = turnWanted_;
            return pending >= 0 ? pending : display.rotation() == 3;
        }
        turnWanted_ = want ? 1 : 0;
        return turnWanted_;
    }

    static bool inputHook(const char *what, int x, int y)
    {
        if (!strcmp(what, "touch"))
            touch.simulate(x, y);
        else if (!strcmp(what, "release"))
            touch.simulate(-1, -1);
        else if (!strcmp(what, "up"))
            input.inject(-1, BUTTON_NONE);
        else if (!strcmp(what, "down"))
            input.inject(1, BUTTON_NONE);
        else if (!strcmp(what, "press"))
            input.inject(0, BUTTON_PRESS);
        else if (!strcmp(what, "hold") || !strcmp(what, "back"))
            input.inject(0, BUTTON_HOLD);
        else if (!strcmp(what, "calibrate"))
            calWanted_ = true;
        else
            return false;
        return true;
    }

    static void run(bool sim)
    {
        display.begin(sim);
        touch.begin(sim);
        buzzer.begin(sim);
        backlight.begin();
        input.begin();
        wifiSetup.begin(sim);
        ctrl.begin();
        machine.begin();
        ::ui.begin();
        screenJpegHook = jpegHook;
        screenInputHook = inputHook;
        screenTurnHook = turnHook;
        fprintf(stderr, "[ui] display %s\n", display.hardware() ? "ILI9488 on " TFT_SPI_DEV : "simulated (mirror only)");

        while (running_)
        {
            const int turn = turnWanted_.exchange(-1);
            if (turn >= 0)
            {
                setScreenTurned(turn != 0);
                ::ui.redraw();
            }
            if (calWanted_.exchange(false))
            {
                backlight.wake();
                openTouchCalibration();
            }
            input.update();
            buzzer.update();
            backlight.update();
            wifiSetup.update();
            machine.tick();
            ::ui.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        screenInputHook = nullptr;
        screenTurnHook = nullptr;
        screenJpegHook = nullptr;
        ctrl.end();
        display.setBacklight(false);
    }

    void start(bool simulate)
    {
        if (running_)
            return;
        running_ = true;
        thread_ = std::thread(run, simulate);
    }

    void stop()
    {
        running_ = false;
        if (thread_.joinable())
            thread_.join();
    }
}
