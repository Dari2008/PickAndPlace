/**
 * screens.h - every screen of the display UI (ui-mockups.md)
 *   screens_home.cpp      boot, offline, home, menu, homing, alarm
 *   screens_jobs.cpp      jobs list, job chosen, check problems, running job
 *   screens_move.cpp      move (jog), teach a position
 *   screens_feeders.cpp   feeders, feeder, pockets, pocket, auto detect
 *   screens_test.cpp      pump & fans, testing, sensors, NFC, motor test, touch calibration
 *   screens_settings.cpp  settings, setting lists, SD card
 *   screens_wifi.cpp      WiFi, network list, more, WPS
 */
#pragma once

#include "../machine/machine.h"
#include "widgets.h"

// ---- home ----------------------------------------------------------------------

// Proportional joystick jog (screens_pi.cpp): axis -1 = X + Y, 2 = Z, 3 = E1, 4 = E0 (stick up / down)
void stickJog(int axis);

class BootScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void tick() override;
    void action(uint16_t id) override;
    uint16_t defaultFocus() const override { return 1; } // WiFi settings
    uint8_t animRects(Rect *r) const override;
    uint16_t animMs() const override { return 40; }
};

class OfflineScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool back() override { return false; }
    void tick() override;
    uint16_t defaultFocus() const override { return 2; }
};

class HomeScreen : public Screen
{
public:
    void enter() override;
    void resume() override { enter(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool back() override { return false; }
    uint16_t defaultFocus() const override { return 10; }
    void tick() override;
    uint8_t liveRects(Rect *r) const override;

private:
    int layout_ = -1; // homed / job active: a change needs a full redraw
};

class MenuScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    ListView list_;
};

class HomingScreen : public Screen
{
public:
    // Home everything (or only E1), then call `then` (e.g. start a job).
    void start(bool e1Only = false, std::function<void()> then = nullptr);
    void draw(Gfx &g) override;
    bool back() override { return done_; }
    uint8_t animRects(Rect *r) const override;
    void tick() override;

private:
    void close(); // homed: back to the screen below, "Homed", then `then`
    bool done_ = false, failed_ = false, e1Only_ = false, closePending_ = false;
    std::string error_; // what the board said when homing failed
    uint32_t started_ = 0;
    std::function<void()> then_;
};

class AlarmScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void tick() override;
    uint16_t defaultFocus() const override { return 1; }
};

// ---- jobs ----------------------------------------------------------------------

class JobsScreen : public Screen
{
public:
    void open(const std::string &dir);
    void enter() override { load(); }
    void resume() override { load(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool back() override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    void load();
    struct Entry
    {
        std::string name;
        bool dir;
        long size;
    };
    std::string dir_ = "/";
    std::vector<Entry> entries_;
    bool loading_ = false;
    std::string error_;
    ListView list_;
};

class JobConfirmScreen : public Screen
{
public:
    void open(const std::string &path);
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    uint16_t defaultFocus() const override { return 1; }

private:
    void check();
    void run();
    std::string path_, name_;
    bool checking_ = false, ok_ = false;
    int parts_ = 0, dots_ = 0, feeders_ = 0;
    struct Problem
    {
        int line;
        std::string text;
    };
    std::vector<Problem> problems_;
    std::string error_;
    friend class JobProblemsScreen;
};

class JobProblemsScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    ListView list_;
};

class JobScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    uint16_t defaultFocus() const override { return 1; }
    void tick() override;
    uint8_t liveRects(Rect *r) const override;

private:
    std::string state_; // job state drawn last: a change needs a full redraw
};

// ---- move ----------------------------------------------------------------------

class MoveScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool turn(int8_t steps) override;
    bool press() override;
    uint16_t defaultFocus() const override { return 1; }
    uint8_t liveRects(Rect *r) const override
    {
        r[0] = {0, 30, 320, 44}; // positions
        return 1;
    }

    // Joystick: X / Y (or the chosen axis Z / E1 up / down) at a speed set by the deflection
    void tick() override { stickJog(axis_ <= 1 ? -1 : axis_); }
    bool usesStick() const override { return true; }

private:
    void jog(int axis, float dir);
    int axis_ = 0; // 0 X 1 Y 2 Z 3 E1 4 E0
    int step_ = 1; // index into STEPS
};

class TeachScreen : public Screen
{
public:
    // Jog X/Y (nozzle) or E1 to a position, then `onSave` with the position.
    void openXY(const std::string &title, std::function<void(float x, float y)> onSave);
    void openE1(const std::string &title, std::function<void(float e1)> onSave);
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool turn(int8_t steps) override;
    bool press() override;
    uint16_t defaultFocus() const override { return 1; }
    uint8_t liveRects(Rect *r) const override
    {
        r[0] = {0, 30, 320, 52}; // positions
        return 1;
    }

    void tick() override { stickJog(e1_ ? 3 : axis_ <= 1 ? -1 : axis_); }
    bool usesStick() const override { return true; }

private:
    void jog(int axis, int dir);
    bool e1_ = false;
    int axis_ = 0;
    int step_ = 1;
    std::string title_;
    std::function<void(float, float)> onSaveXY_;
    std::function<void(float)> onSaveE1_;
};

// ---- feeders -------------------------------------------------------------------

class FeedersScreen : public Screen
{
public:
    void enter() override { machine.refreshFeeders(); }
    void resume() override { enter(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    ListView list_;
};

class FeederScreen : public Screen
{
public:
    void open(int num);
    void enter() override { load(); }
    void resume() override { load(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }
    void load();

    int num() const { return f_.num; }
    const FeederInfo &info() const { return f_; }

private:
    FeederInfo f_;
    bool loaded_ = false;
    ListView list_;
};

class PocketsScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    ListView list_;
};

class PocketScreen : public Screen
{
public:
    void open(int index); // 1-based
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    int i_ = 1;
    ListView list_;
};

class ScanScreen : public Screen
{
public:
    void start();
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool back() override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    bool started_ = false;
    uint32_t startedAt_ = 0;
    ListView list_;
};

// ---- outputs and testing -------------------------------------------------------

class PumpScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }
    uint8_t liveRects(Rect *r) const override
    {
        r[0] = {0, 25, 320, 215}; // outputs list
        return 1;
    }

private:
    ListView list_;
};

class TestingScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    ListView list_;
};

// Testing -> Homing: home one axis at a time (HOME X / Y / Z / E1) to see how each one behaves
class HomeAxesScreen : public Screen
{
public:
    void enter() override { list_.reset(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    std::string running_;
    bool cancelled_ = false;
    ListView list_;
};

// Calibration -> Sensorless homing: jog an axis, measure StallGuard while it runs free (SGTEST),
// save the threshold (tmc_sg_*) and the homing current (home_cur), test-home it
class SgCalScreen : public Screen
{
public:
    void enter() override { list_.reset(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    // While measuring: the live values (Machine::sgLive) are repainted every 100 ms
    uint8_t animRects(Rect *r) const override
    {
        if (!busy_ || !measuring_)
            return 0;
        r[0] = {16, 62, 288, 46};
        return 1;
    }
    uint16_t animMs() const override { return 100; }
    std::string key(const char *what, int which = 0) const; // "tmc_sg_" -> tmc_sg_x / _x2 / _y / _e1
    void save(const std::string &key, float value);
    int axis_ = 0; // x, y, e1
    bool busy_ = false;
    bool cancelled_ = false;
    std::string busyWhat_; // "Measuring X" / "Homing X" while busy_
    bool measuring_ = false;
    uint32_t liveSeq_ = 0;
    int liveMin_[2] = {-1, -1}, liveMax_[2] = {-1, -1};
    std::string result_;
    int suggest_[2] = {-1, -1};
    ListView list_;
};

// Testing -> Find length: from home, run X / Y / the gantry to the far end until it stalls (FINDLEN),
// show the length and save it as travel_*. Needs the axis homed first (offers a Home button).
class FindLengthScreen : public Screen
{
public:
    void enter() override { list_.reset(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    int axis_ = 0; // x, y, e1
    bool busy_ = false, cancelled_ = false;
    std::string busyWhat_, result_;
    int suggest_ = -1; // travel to save, -1 = nothing measured
    ListView list_;
};

// Every sensor, live: switches, sensorless stall signals, vacuum, nozzle slots, joystick, cameras
class SensorsScreen : public Screen
{
public:
    void enter() override
    {
        lastPoll_ = 0;
        list_.reset();
    }
    void draw(Gfx &g) override;
    void tick() override;
    void drag(int dy) override { list_.dragBy(dy); }
    uint8_t animRects(Rect *r) const override
    {
        r[0] = {0, 25, 320, 215}; // the list (polled)
        return 1;
    }
    uint16_t animMs() const override { return 250; }

private:
    uint32_t lastPoll_ = 0;
    int waiting_ = 0;
    Record sensors_, vacuum_, joy_;
    std::vector<Record> nozzles_;
    ListView list_;
};


class MotorTestScreen : public Screen
{
public:
    void enter() override { turned_ = 0; }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool turn(int8_t steps) override;

private:
    void move(float deg);
    int axis_ = 3; // e0
    float turned_ = 0;
};

class TouchCalScreen : public Screen
{
public:
    void enter() override { point_ = 0; }
    void draw(Gfx &g) override;
    bool press() override;
    bool rawTouchMode() const override { return true; }
    void rawTouch(int16_t rx, int16_t ry) override;

private:
    int point_ = 0;
    int16_t rx_[4], ry_[4];
};

// Settings -> Touch: calibration, test and every touch option.
class TouchSettingsScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    ListView list_;
};

// Picture turned by 180 degrees (saved), touch calibration mirrored with it. UI thread only.
void setScreenTurned(bool on);

// Settings -> Display: turn 180 degrees, brightness, screen off after, off now.
class DisplaySettingsScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;

private:
    ListView list_;
};

// Shows where the finger is (calibrated point, raw values, pressure).
class TouchTestScreen : public Screen
{
public:
    void enter() override { trail_.clear(); }
    void draw(Gfx &g) override;
    void tick() override;

private:
    struct Dot
    {
        int16_t x, y; // design space
    };
    std::vector<Dot> trail_;
    bool down_ = false;
    int16_t rx_ = 0, ry_ = 0;
    uint16_t pressure_ = 0;
    bool accepted_ = false; // passes the pressure / on settings
};

// ---- settings ------------------------------------------------------------------

class SettingsScreen : public Screen
{
public:
    void enter() override { machine.refreshSettings(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    ListView list_;
};

class SdScreen : public Screen
{
public:
    void enter() override;
    void draw(Gfx &g) override;
    void action(uint16_t id) override;

private:
    std::string type_, size_, error_;
    bool loading_ = false;
};

// ---- WiFi ----------------------------------------------------------------------

class WifiScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    uint16_t defaultFocus() const override { return 1; }
    uint8_t animRects(Rect *r) const override
    {
        r[0] = {0, 28, 320, 140}; // state, signal, address
        return 1;
    }
    uint16_t animMs() const override { return 1000; }
};

// Settings -> WiFi: scanned networks; tap one to join (keyboard for the
// password). The only way to set up WiFi besides WPS (no access point).
class WifiListScreen : public Screen
{
public:
    void enter() override;
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void tick() override;
    void drag(int dy) override { list_.dragBy(dy); }
    uint8_t animRects(Rect *r) const override
    {
        r[0] = {0, 25, 320, 40}; // connection state card
        return 1;
    }
    uint16_t animMs() const override { return 500; }

private:
    void join(const std::string &ssid, bool secure);
    ListView list_;
    bool scanning_ = false;
    std::string pending_, state_;
};

class WifiMoreScreen : public Screen
{
public:
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    ListView list_;
};

class WpsScreen : public Screen
{
public:
    void start();
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void tick() override;
    uint16_t defaultFocus() const override { return 1; }
    uint8_t animRects(Rect *r) const override
    {
        r[0] = {50, 150, 220, 36}; // countdown
        return 1;
    }
    uint16_t animMs() const override { return 500; }

private:
    uint32_t started_ = 0;
    std::string state_;
};

// ---- instances -----------------------------------------------------------------

extern BootScreen bootScreen;
extern OfflineScreen offlineScreen;
extern HomeScreen homeScreen;
extern MenuScreen menuScreen;
extern HomingScreen homingScreen;
extern AlarmScreen alarmScreen;
extern JobsScreen jobsScreen;
extern JobConfirmScreen jobConfirmScreen;
extern JobProblemsScreen jobProblemsScreen;
extern JobScreen jobScreen;
extern MoveScreen moveScreen;
extern TeachScreen teachScreen;
extern FeedersScreen feedersScreen;
extern FeederScreen feederScreen;
extern PocketsScreen pocketsScreen;
extern PocketScreen pocketScreen;
extern ScanScreen scanScreen;
extern PumpScreen pumpScreen;
extern TestingScreen testingScreen;
extern SensorsScreen sensorsScreen;
extern MotorTestScreen motorTestScreen;
extern HomeAxesScreen homeAxesScreen;
extern SgCalScreen sgCalScreen;
extern FindLengthScreen findLengthScreen;
extern TouchCalScreen touchCalScreen;
extern TouchSettingsScreen touchSettingsScreen;
extern TouchTestScreen touchTestScreen;
extern DisplaySettingsScreen displaySettingsScreen;
extern SettingsScreen settingsScreen;
extern SdScreen sdScreen;
extern WifiScreen wifiScreen;
extern WifiMoreScreen wifiMoreScreen;
extern WifiListScreen wifiListScreen;
extern WpsScreen wpsScreen;

// Helpers shared by the screens
std::string quote(const std::string &s);            // protocol word with quotes if needed
std::string baseName(const std::string &path);      // "/jobs/a.pap" -> "a.pap"
std::string fmtTime(long seconds);                  // 765 -> "12:45"
int wifiBars();                                     // 0..3 for the title bar
int rssiBars(int rssi);                             // 1..3

#include "screens_pi.h"
