/**
 * screens_pi.h - screens only the Pi has: cameras, calibration, vacuum sensor, joystick
 * -----------------------------------------------------------------------------
 * Menu -> Cameras        live picture (bottom / top), lights, measure / find mark, save
 * Menu -> Calibration    camera scale + skew (CAL BOTTOM / TOP), top camera offset,
 *                        rotation direction, vacuum sensor, joystick
 * Included at the end of screens.h; drawn in the same 320 x 240 design space.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class CamerasScreen : public Screen
{
public:
    void open(int cam); // 0 bottom, 1 top
    void enter() override;
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool press() override;
    void tick() override { stickJog(-1); }
    bool usesStick() const override { return true; }
    uint16_t defaultFocus() const override { return 1; }
    uint8_t animRects(Rect *r) const override
    {
        r[0] = {0, 25, 216, 166}; // the picture
        return 1;
    }
    uint16_t animMs() const override { return 120; }
    uint8_t liveRects(Rect *r) const override
    {
        r[0] = {0, 190, 320, 36}; // result / position line
        r[1] = {220, 25, 100, 160}; // light button
        return 2;
    }

private:
    int cam_ = 1;
    std::string result_;
    std::vector<uint16_t> px_;
};

class CalibrationScreen : public Screen
{
public:
    void enter() override { list_.reset(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    ListView list_;
};

// Calibration -> Bottom / Top camera: checkerboard, on the tip / a mark, lens, offset or rotation direction
class CamCalScreen : public Screen
{
public:
    void open(int cam); // 0 bottom, 1 top
    void enter() override { list_.reset(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    int cam_ = 0;
    ListView list_;
};

// Runs a CAL command and shows what was saved
void calRun(const std::string &cmd, const std::string &what);
// cal_grid to the next common checkerboard square size
void cycleGridSize();

// Menu -> Setup: every bring-up step in order, one page each (screens_setup.cpp)
class SetupScreen : public Screen
{
public:
    void enter() override;
    void resume() override { ui.redraw(); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void tick() override { stickJog(-1); }
    bool usesStick() const override { return true; }
    uint8_t liveRects(Rect *r) const override;
    static int steps();

private:
    void go(int step);
    std::vector<std::string> row(int r) const;
    std::string liveLine() const;
    int step_ = 0, axis_ = 0;
    float measured_ = 100; // steps per mm: what the ruler says
};

// Calibration -> Nozzle sensor order: trigger the slots in order, each one's sensor bit is learned
class NozzleLearnScreen : public Screen
{
public:
    void enter() override;
    void draw(Gfx &g) override;
    void tick() override;
    bool back() override;

private:
    void apply(const Reply &r);
    uint32_t lastPoll_ = 0;
    bool waiting_ = false, active_ = false, done_ = false, two_ = false, failed_ = false;
    int next_ = 0, count_ = 0;
    std::string bits_;
};

class VacuumScreen : public Screen
{
public:
    void enter() override { lastPoll_ = 0; }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void tick() override;
    uint16_t defaultFocus() const override { return 1; }
    uint8_t animRects(Rect *r) const override
    {
        r[0] = {0, 25, 320, 96}; // readings (polled)
        return 1;
    }
    uint16_t animMs() const override { return 250; }

private:
    uint32_t lastPoll_ = 0;
    bool waiting_ = false, sensor_ = false, part_ = false;
    float hpa_ = 0, base_ = 0, drop_ = 0, pickMin_ = 0;
};

class JoystickScreen : public Screen
{
public:
    void enter() override { lastPoll_ = 0; }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void tick() override;
    bool usesStick() const override { return true; } // the stick is what we look at
    uint16_t defaultFocus() const override { return 1; }
    uint8_t animRects(Rect *r) const override
    {
        r[0] = {0, 25, 200, 165};
        return 1;
    }
    uint16_t animMs() const override { return 80; }

private:
    uint32_t lastPoll_ = 0;
    bool waiting_ = false, adc_ = false, button_ = false;
    float x_ = 0, y_ = 0;
    int rawX_ = 0, rawY_ = 0;
};

// Nozzle rack: slots with name / sensor / on the head; change, rename, offset, teach the rack
class NozzlesScreen : public Screen
{
public:
    struct Slot
    {
        int num = 0;
        std::string name;
        float dx = 0, dy = 0, dz = 0, x = 0, y = 0;
        int present = -1; // -1 = sensors not readable
        bool mounted = false;
    };
    void enter() override
    {
        list_.reset();
        lastPoll_ = 0;
    }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }
    void tick() override;

private:
    void poll();
    void change(int slot);
    void touch();
    ListView list_;
    std::vector<Slot> slots_;
    int mounted_ = 0;
    bool busy_ = false, waiting_ = false;
    uint32_t lastPoll_ = 0;
};

// Settings from the catalog: one group, or the results of a search
class SettingsGroupScreen : public Screen
{
public:
    void openGroup(const std::string &group);
    void openSearch(const std::string &text);
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    void drag(int dy) override { list_.dragBy(dy); }

private:
    std::vector<CatalogEntry> entries() const;
    std::string group_, search_;
    ListView list_;
};

// Pick one of a setting's choices
class ChoiceScreen : public Screen
{
public:
    void open(const CatalogEntry &e);
    void draw(Gfx &g) override;
    void action(uint16_t id) override;

private:
    CatalogEntry e_;
    std::vector<std::string> options_;
    ListView list_;
};

// Edit one catalog setting with the fitting editor (number, toggle, choice, text)
void editSetting(const CatalogEntry &e);

// The microSD card in the Octopus slot: browse it, copy a file into the Pi's /jobs
class OctoSdScreen : public Screen
{
public:
    void enter() override { open("/"); }
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool back() override; // a folder up first
    void drag(int dy) override { list_.dragBy(dy); }

private:
    struct Entry
    {
        std::string name;
        long size = 0;
        bool dir = false;
    };
    void open(const std::string &dir);
    void copy(const std::string &path);
    std::string dir_ = "/", error_;
    std::vector<Entry> entries_;
    bool loading_ = false;
    ListView list_;
};

extern OctoSdScreen octoSdScreen;
extern SettingsGroupScreen settingsGroupScreen;
extern ChoiceScreen choiceScreen;
extern NozzlesScreen nozzlesScreen;
extern CamerasScreen camerasScreen;
extern CalibrationScreen calibrationScreen;
extern CamCalScreen camCalScreen;
extern SetupScreen setupScreen;
extern VacuumScreen vacuumScreen;
extern NozzleLearnScreen nozzleLearnScreen;
extern JoystickScreen joystickScreen;
