// Testing -> Homing (one axis at a time) and Calibration -> Sensorless homing (StallGuard threshold)
#include "screens.h"
#include "widgets.h"
#include "../machine/machine.h"

#include <cstdio>
#include <cstdlib>

HomeAxesScreen homeAxesScreen;
SgCalScreen sgCalScreen;
FindLengthScreen findLengthScreen;
PositionsScreen positionsScreen;
TeachPosScreen teachPosScreen;
HeadOffsetsScreen headOffsetsScreen;

namespace commands
{
void urgentLine(const std::string &line); // ctrl/protocol: HALT straight to the Octopus
}

enum
{
    CANCEL_ID = 5
};

// While an axis homes / measures: what runs, and one big Cancel button instead of the list
static void drawRunning(Gfx &g, const std::string &what)
{
    g.card(16, 40, 288, 64);
    g.icon(Icon::Home, 30, 65, th::ACCENT);
    g.text(g.fit(what + " ...", Font::BodyBold, 240), 52, 62, Font::BodyBold, th::TEXT);
    g.text("the motor stops at once on Cancel", 52, 84, Font::Small, th::DIM);
    g.button(CANCEL_ID, "Cancel", 40, 124, 240, 64, Kind::Danger, Icon::Stop);
}

// The UI link and the command loop are both busy with the HOME: go past them
static void cancelNow()
{
    commands::urgentLine("HALT");
    ui.toast("Stopping ...", th::WARN);
}

// ---------------------------------------------------------------- Testing -> Homing

enum
{
    HA_ROWS = 10
};

struct HomeItem
{
    const char *cmd, *label, *sub;
};
static const HomeItem HOME_ITEMS[] = {
    {"HOME X", "Home X", "both X motors, each stops on its own stall"},
    {"HOME Y", "Home Y", "Y on its own"},
    {"HOME Z", "Home Z", "seesaw flag edge"},
    {"HOME E1", "Home gantry (E1)", "feeder gantry"},
    {"HOME", "Home everything", "Z, X, Y, gantry - like before a job"},
};

void HomeAxesScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Homing");
    if (!running_.empty())
    {
        drawRunning(g, cancelled_ ? "Stopping " + running_ : running_);
        g.hint("Cancel if it runs the wrong way or doesn't stop at the end");
        return;
    }
    std::vector<Row> rows;
    for (const HomeItem &h : HOME_ITEMS)
    {
        Row r;
        r.icon = Icon::Home, r.label = h.label, r.sub = h.sub;
        rows.push_back(r);
    }
    Row stop;
    stop.icon = Icon::Stop, stop.iconColor = th::BAD, stop.label = "Stop", stop.sub = "halts the motors at once";
    rows.push_back(stop);
    list_.draw(g, HA_ROWS, rows, 25, 35);
    g.hint("One axis at a time: see how each one homes");
}

void HomeAxesScreen::action(uint16_t id)
{
    if (!running_.empty())
    {
        if (id == CANCEL_ID && !cancelled_)
        {
            cancelled_ = true;
            cancelNow();
        }
        return;
    }
    if (id < HA_ROWS)
        return;
    const size_t i = id - HA_ROWS, n = sizeof(HOME_ITEMS) / sizeof(HOME_ITEMS[0]);
    if (i == n)
    {
        ui.send("HALT", nullptr, false);
        return;
    }
    if (i > n)
        return;
    running_ = HOME_ITEMS[i].label;
    cancelled_ = false;
    const std::string what = HOME_ITEMS[i].label;
    ui.send(
        HOME_ITEMS[i].cmd,
        [this, what](const Reply &r) {
            running_.clear();
            if (r.ok)
                ui.toast(what + ": done", th::OK);
            else if (cancelled_)
                ui.toast(what + ": cancelled", th::WARN);
            else
                ui.toast(what + ": " + r.error, th::BAD);
            cancelled_ = false;
        },
        false);
}

// ---------------------------------------------------------------- Calibration -> Sensorless homing

enum
{
    SG_ROWS = 10
};

static const char *const SG_AXES[] = {"x", "y", "e1"};
static const char *const SG_NAMES[] = {"X", "Y", "Gantry (E1)"};
static const char *const SG_DIR_KEYS[] = {"home_dir_x", "home_dir_y", "e1_home_dir"};

static bool homesAtMax(int axis) { return atof(machine.setting(SG_DIR_KEYS[axis], "-1").c_str()) > 0; }

std::string SgCalScreen::key(const char *what, int which) const
{
    return std::string(what) + (which == 1 ? "x2" : SG_AXES[axis_]);
}

void SgCalScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Sensorless homing");
    if (busy_ && measuring_)
    {
        // Live load values while it runs towards the end
        const SgLive sg = machine.sgLive();
        if (sg.seq != liveSeq_ && sg.v >= 0)
        {
            liveSeq_ = sg.seq;
            const int v[2] = {sg.v, sg.v2};
            for (int k = 0; k < 2; k++)
                if (v[k] >= 0)
                {
                    if (liveMin_[k] < 0 || v[k] < liveMin_[k])
                        liveMin_[k] = v[k];
                    if (v[k] > liveMax_[k])
                        liveMax_[k] = v[k];
                }
        }
        g.card(16, 40, 288, 70);
        g.text(g.fit((cancelled_ ? "Stopping: " : "") + busyWhat_ + " ...", Font::BodyBold, 270), 30, 52, Font::BodyBold, th::TEXT);
        char t[64];
        if (sg.seq == 0 || sg.v < 0)
            snprintf(t, sizeof(t), "waiting for values ...");
        else if (sg.v2 >= 0)
            snprintf(t, sizeof(t), "X %d   X2 %d   %.0f mm", sg.v, sg.v2, (double)sg.mm);
        else
            snprintf(t, sizeof(t), "%d   %.0f mm", sg.v, (double)sg.mm);
        g.text(t, 30, 74, Font::Title, th::ACCENT);
        if (liveMin_[0] >= 0)
        {
            if (liveMin_[1] >= 0)
                snprintf(t, sizeof(t), "min-max  X %d-%d   X2 %d-%d", liveMin_[0], liveMax_[0], liveMin_[1], liveMax_[1]);
            else
                snprintf(t, sizeof(t), "min-max  %d-%d", liveMin_[0], liveMax_[0]);
            g.text(t, 30, 96, Font::Small, th::DIM);
        }
        g.button(CANCEL_ID, "Cancel", 40, 124, 240, 64, Kind::Danger, Icon::Stop);
        g.hint("the value drops when it hits the end");
        return;
    }
    if (busy_)
    {
        drawRunning(g, cancelled_ ? "Stopping: " + busyWhat_ : busyWhat_);
        g.hint("Cancel stops the motor at once");
        return;
    }
    const bool x = axis_ == 0;
    std::vector<Row> rows(x ? 10 : 9);
    int i = 0;
    rows[i].icon = Icon::Motor, rows[i].label = "Axis", rows[i].value = SG_NAMES[axis_], rows[i].sub = "tap: next axis";
    i++;
    rows[i].icon = Icon::Move, rows[i].label = "Jog -10 mm";
    i++;
    rows[i].icon = Icon::Move, rows[i].label = "Jog +10 mm";
    i++;
    rows[i].icon = Icon::Test, rows[i].label = busy_ ? "Measuring ..." : "Measure";
    rows[i].sub = result_.empty() ? "runs 50 mm towards home, into the end" : result_;
    i++;
    rows[i].label = x ? "Threshold X" : "Threshold", rows[i].value = machine.setting(key("tmc_sg_").c_str(), "-");
    rows[i].sub = "tmc_sg: higher = stops sooner";
    i++;
    if (x)
    {
        rows[i].label = "Threshold X2", rows[i].value = machine.setting("tmc_sg_x2", "-");
        rows[i].sub = "second X motor";
        i++;
    }
    rows[i].icon = Icon::Check, rows[i].label = "Use the suggestion";
    auto sug = [](int v) { return v < 0 ? std::string("-") : std::to_string(v); };
    rows[i].value = x ? sug(suggest_[0]) + " / " + sug(suggest_[1]) : sug(suggest_[0]);
    rows[i].sub = "centre between min (end) and max (free)";
    i++;
    rows[i].label = "Homing current", rows[i].value = machine.setting("home_cur", "-") + " %";
    rows[i].sub = "home_cur, % of the run current (all axes)";
    i++;
    rows[i].icon = Icon::Home, rows[i].label = "Test: home this axis", rows[i].sub = "with the saved threshold";
    i++;
    rows[i].icon = Icon::Move, rows[i].label = "Home side", rows[i].value = homesAtMax(axis_) ? "max" : "min";
    rows[i].sub = "tap: switch (min = position 0, max = travel)";
    list_.draw(g, SG_ROWS, rows, 25, 35);
    g.hint("Measure: start less than 50 mm from the home end");
}

void SgCalScreen::save(const std::string &k, float v)
{
    char line[64];
    snprintf(line, sizeof(line), "SET %s %d", k.c_str(), (int)(v + 0.5f));
    ui.send(line, [k](const Reply &r) {
        machine.refreshSettings();
        if (r.ok)
            ui.toast(k + " saved", th::OK);
    });
}

void SgCalScreen::action(uint16_t id)
{
    if (busy_)
    {
        if (id == CANCEL_ID && !cancelled_)
        {
            cancelled_ = true;
            cancelNow();
        }
        return;
    }
    if (id < SG_ROWS)
        return;
    const bool x = axis_ == 0;
    int row = (int)id - SG_ROWS;
    if (!x && row >= 5)
        row++; // no "Threshold X2" row: the rows below move up
    switch (row)
    {
    case 0:
        axis_ = (axis_ + 1) % 3;
        result_.clear();
        suggest_[0] = suggest_[1] = -1;
        list_.reset();
        break;
    case 1:
    case 2:
    {
        char line[48];
        snprintf(line, sizeof(line), "JOG %s=%d", SG_AXES[axis_], row == 1 ? -10 : 10);
        ui.send(line);
        break;
    }
    case 3:
        busy_ = true;
        cancelled_ = false;
        busyWhat_ = std::string("Measuring ") + SG_NAMES[axis_];
        measuring_ = true;
        liveSeq_ = machine.sgLive().seq;
        liveMin_[0] = liveMin_[1] = liveMax_[0] = liveMax_[1] = -1;
        result_.clear();
        ui.send(std::string("SGTEST ") + SG_AXES[axis_], [this](const Reply &r) {
            busy_ = false;
            measuring_ = false;
            ui.redraw();
            if (!r.ok)
            {
                result_ = cancelled_ ? "cancelled" : "failed: " + r.error;
                cancelled_ = false;
                return;
            }
            result_.clear();
            for (size_t k = 0; k < r.records.size() && k < 2; k++)
            {
                const Record &rec = r.records[k];
                char t[64];
                // free-running minimum -> value stalled at the end, or "no stall"
                snprintf(t, sizeof(t), "%s%s %s-%s%s", k ? " · " : "", rec.str("axis").c_str(), rec.str("min").c_str(),
                         rec.str("max").c_str(), rec.integer("stall", 0) ? "" : " NO stall");
                result_ += t;
                suggest_[k] = rec.integer("suggest", -1);
            }
            result_ += " (min-max)";
        }, false);
        break;
    case 4:
    case 5:
    {
        const int which = row == 5 ? 1 : 0;
        const std::string k = key("tmc_sg_", which);
        NumberEditor::Spec s;
        s.title = "Threshold " + std::string(which ? "X2" : SG_NAMES[axis_]);
        s.what = "tmc_sg: stall when the load value drops below 2 x this";
        s.value = (float)atof(machine.setting(k.c_str(), "80").c_str());
        s.min = 0, s.max = 255, s.decimals = 0;
        s.steps = {1, 5, 20};
        numberEditor.open(s, [this, k](float v) { save(k, v); });
        break;
    }
    case 6:
        if (result_.empty() || result_ == "cancelled" || result_.compare(0, 7, "failed:") == 0)
        {
            ui.toast("Measure first", th::WARN);
            break;
        }
        save(key("tmc_sg_"), (float)suggest_[0]);
        if (x)
            save("tmc_sg_x2", (float)suggest_[1]);
        break;
    case 7:
    {
        NumberEditor::Spec s;
        s.title = "Homing current";
        s.what = "% of the run current while homing sensorless";
        s.value = (float)atof(machine.setting("home_cur", "60").c_str());
        s.unit = "%";
        s.min = 10, s.max = 100, s.decimals = 0;
        s.steps = {5, 1, 20};
        numberEditor.open(s, [this](float v) { save("home_cur", v); });
        break;
    }
    case 9:
    {
        char line[48];
        snprintf(line, sizeof(line), "SET %s %d", SG_DIR_KEYS[axis_], homesAtMax(axis_) ? -1 : 1);
        ui.send(line, [](const Reply &r) {
            machine.refreshSettings();
            if (r.ok)
                ui.toast("Home side saved", th::OK);
        });
        break;
    }
    case 8:
    {
        static const char *const CMD[] = {"HOME X", "HOME Y", "HOME E1"};
        busy_ = true;
        cancelled_ = false;
        busyWhat_ = std::string("Homing ") + SG_NAMES[axis_];
        ui.send(
            CMD[axis_],
            [this](const Reply &r) {
                busy_ = false;
                if (r.ok)
                    ui.toast(std::string(SG_NAMES[axis_]) + " homed", th::OK);
                else if (cancelled_)
                    ui.toast(std::string(SG_NAMES[axis_]) + ": cancelled", th::WARN);
                else
                    ui.toast(std::string(SG_NAMES[axis_]) + ": " + r.error, th::BAD);
                cancelled_ = false;
            },
            false);
        break;
    }
    }
}

// ---------------------------------------------------------------- Testing -> Find length

enum
{
    FL_ROWS = 10
};

static const char *const FL_TRAVEL[] = {"travel_x", "travel_y", "travel_e1"};

static bool flHomed(int axis)
{
    const MachineStatus st = machine.status();
    return axis == 2 ? st.e1Homed : st.homed;
}

void FindLengthScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Find length");
    if (busy_)
    {
        drawRunning(g, cancelled_ ? "Stopping: " + busyWhat_ : busyWhat_);
        g.hint("Cancel stops the motor at once");
        return;
    }
    const bool homed = flHomed(axis_);
    std::vector<Row> rows(4);
    rows[0].icon = Icon::Motor, rows[0].label = "Axis", rows[0].value = SG_NAMES[axis_], rows[0].sub = "tap: next axis";
    if (homed)
    {
        rows[1].icon = Icon::Move, rows[1].label = "Find length";
        rows[1].sub = "runs from home to the far end until it stalls";
    }
    else
    {
        rows[1].icon = Icon::Home, rows[1].label = axis_ == 2 ? "Home the gantry first" : "Home X + Y first";
        rows[1].sub = "the length is measured from home";
    }
    rows[2].icon = Icon::Test, rows[2].label = "Length", rows[2].value = result_.empty() ? "-" : result_;
    rows[2].sub = std::string(FL_TRAVEL[axis_]) + " now " + machine.setting(FL_TRAVEL[axis_], "-") + " mm";
    rows[3].icon = Icon::Check, rows[3].label = "Save as travel";
    rows[3].value = suggest_ < 0 ? "-" : std::to_string(suggest_) + " mm";
    rows[3].sub = "length minus the pull-off, so it never hits the end";
    list_.draw(g, FL_ROWS, rows, 25, 35);
    g.hint(homed ? "uses the sensorless homing settings" : "not homed: home first");
}

void FindLengthScreen::action(uint16_t id)
{
    if (busy_)
    {
        if (id == CANCEL_ID && !cancelled_)
        {
            cancelled_ = true;
            cancelNow();
        }
        return;
    }
    if (id < FL_ROWS)
        return;
    switch (id - FL_ROWS)
    {
    case 0:
        axis_ = (axis_ + 1) % 3;
        result_.clear();
        suggest_ = -1;
        break;
    case 1:
    {
        const bool homed = flHomed(axis_);
        static const char *const HOME_CMD[] = {"HOME XY", "HOME XY", "HOME E1"};
        static const char *const FIND_CMD[] = {"FINDLEN x", "FINDLEN y", "FINDLEN e1"};
        busy_ = true;
        cancelled_ = false;
        busyWhat_ = homed ? std::string("Finding the length of ") + SG_NAMES[axis_] : axis_ == 2 ? "Homing the gantry" : "Homing X + Y";
        ui.send(
            homed ? FIND_CMD[axis_] : HOME_CMD[axis_],
            [this, homed](const Reply &r) {
                busy_ = false;
                if (!r.ok)
                    ui.toast(cancelled_ ? std::string("Cancelled") : r.error, cancelled_ ? th::WARN : th::BAD);
                else if (!homed)
                    ui.toast("Homed - now find the length", th::OK);
                else if (!r.records.empty())
                {
                    char t[24];
                    snprintf(t, sizeof(t), "%.1f mm", atof(r.records[0].str("mm").c_str()));
                    result_ = t;
                    suggest_ = (int)r.records[0].integer("suggest", -1);
                    ui.toast(std::string("Length ") + t, th::OK);
                }
                cancelled_ = false;
                ui.redraw();
            },
            false);
        break;
    }
    case 2:
        break;
    case 3:
    {
        if (suggest_ < 0)
        {
            ui.toast("Find the length first", th::WARN);
            break;
        }
        char line[48];
        snprintf(line, sizeof(line), "SET %s %d", FL_TRAVEL[axis_], suggest_);
        ui.send(line, [this](const Reply &r) {
            machine.refreshSettings();
            if (r.ok)
                ui.toast(std::string(FL_TRAVEL[axis_]) + " saved - home again", th::OK);
        });
        break;
    }
    }
}

// ---------------------------------------------------------------- Calibration -> Positions

static const TeachPos TEACH_POS[] = {
    {"cam_bottom", "Bottom camera", "nozzle tip centred over the bottom camera"},
    {"loadcell", "Load cell pad", "vacuum nozzle over the middle of the pad"},
    {"nozzle_first", "Nozzle slot 1", "nozzle centred over the first slot"},
    {"nozzle_last", "Last nozzle slot", "nozzle centred over the last slot"},
    {"reject", "Drop point", "where skipped parts are dropped"},
};
static const size_t N_TEACH = sizeof(TEACH_POS) / sizeof(TEACH_POS[0]);

enum
{
    PS_ROWS = 10,
    TP_GO = 1,
    TP_SAVE,
    TP_HOME
};

static std::string posText(const TeachPos &p)
{
    const std::string x = machine.setting((std::string(p.key) + "_x").c_str(), "-");
    const std::string y = machine.setting((std::string(p.key) + "_y").c_str(), "-");
    return x + " / " + y;
}

void PositionsScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Positions");
    std::vector<Row> rows(N_TEACH);
    for (size_t i = 0; i < N_TEACH; i++)
    {
        rows[i].icon = Icon::Move, rows[i].label = TEACH_POS[i].label, rows[i].value = posText(TEACH_POS[i]);
        rows[i].sub = TEACH_POS[i].what, rows[i].chev = true;
    }
    list_.draw(g, PS_ROWS, rows, 25, 35);
    g.hint("pick one, jog there with the joystick, save");
}

void PositionsScreen::action(uint16_t id)
{
    if (id >= PS_ROWS && id < PS_ROWS + N_TEACH)
        teachPosScreen.open(TEACH_POS[id - PS_ROWS]);
}

void TeachPosScreen::open(const TeachPos &p)
{
    pos_ = p;
    busy_ = false;
    ui.push(this);
}

void TeachPosScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar(pos_.label);
    const MachineStatus st = machine.status();
    g.card(16, 32, 288, 72);
    g.text(g.fit(pos_.what, Font::Small, 270), 28, 46, Font::Small, th::DIM);
    char t[64];
    snprintf(t, sizeof(t), "head  X %.2f   Y %.2f", (double)st.x, (double)st.y);
    g.text(t, 28, 74, Font::Title, th::ACCENT);
    g.text(("saved  " + posText(pos_)).c_str(), 28, 96, Font::Small, th::DIM);
    if (!st.homed)
    {
        // Positions only mean something from home
        g.text("Not homed: home X + Y first", 160, 124, Font::Body, th::WARN, Align::Center);
        g.button(TP_HOME, busy_ ? "Homing ..." : "Home X + Y", 60, 146, 200, 44, Kind::Primary, Icon::Home);
        g.hint("then jog there with the joystick");
        return;
    }
    g.button(TP_GO, "Go to saved", 16, 120, 140, 44, Kind::Normal, Icon::Move);
    g.button(TP_SAVE, "Save here", 164, 120, 140, 44, Kind::Primary, Icon::Check);
    g.hint("joystick: move the head  ·  Save here stores X / Y");
}

void TeachPosScreen::action(uint16_t id)
{
    if (busy_)
        return;
    const MachineStatus st = machine.status();
    const std::string kx = std::string(pos_.key) + "_x", ky = std::string(pos_.key) + "_y";
    char line[96];
    switch (id)
    {
    case TP_HOME:
        busy_ = true;
        ui.send("HOME XY", [this](const Reply &r) {
            busy_ = false;
            if (r.ok)
                ui.toast("Homed", th::OK);
            ui.redraw();
        });
        ui.redraw();
        break;
    case TP_GO:
    {
        const float x = (float)atof(machine.setting(kx.c_str(), "-1").c_str());
        const float y = (float)atof(machine.setting(ky.c_str(), "-1").c_str());
        if (x < 0 || y < 0)
        {
            ui.toast("Not set yet", th::WARN);
            break;
        }
        snprintf(line, sizeof(line), "MOVE x=%.3f y=%.3f", x, y);
        ui.send(line);
        break;
    }
    case TP_SAVE:
        snprintf(line, sizeof(line), "SET %s %.3f", kx.c_str(), (double)st.x);
        ui.send(line, nullptr);
        snprintf(line, sizeof(line), "SET %s %.3f", ky.c_str(), (double)st.y);
        ui.send(line, [this](const Reply &r) {
            machine.refreshSettings();
            if (r.ok)
                ui.toast(std::string(pos_.label) + " saved", th::OK);
        });
        break;
    }
}

// ---------------------------------------------------------------- Calibration -> Head offsets

enum
{
    HO_SAVE = 1, // by eye: tip on the mark / mark on the cross
    HO_FIND,     // camera: bottom camera finds the tip, top camera finds the lens
    HO_GOTO,     // move over the bottom camera
    HO_ZDOWN,
    HO_ZUP,
    HO_BACK,
    HO_VHOME
};

static const char *const HO_TITLE[] = {"1/3  Nozzle", "2/3  Paste tip", "3/3  Top camera", "Done"};
static const char *const HO_WHAT[] = {
    "over the bottom camera: Find tip  -  or on a mark: Save by eye",
    "same way: over the bottom camera (Z: paste side down) or on the mark",
    "above the bottom camera lens: Find lens  -  or mark on the cross",
    "paste tip and top camera are now relative to the nozzle"};

void HeadOffsetsScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Head offsets");
    const MachineStatus st = machine.status();
    g.card(16, 30, 288, 80);
    g.text(HO_TITLE[step_], 28, 44, Font::BodyBold, th::TEXT);
    char t[64];
    snprintf(t, sizeof(t), "head  X %.2f   Y %.2f   Z %.1f", (double)st.x, (double)st.y, (double)st.z);
    g.text(t, 28, 72, Font::Body, th::ACCENT);
    g.text(g.fit(result_.empty() ? HO_WHAT[step_] : result_, Font::Small, 270), 28, 96, Font::Small, th::DIM);
    if (step_ >= 3)
    {
        const bool on = machine.setting("vhome", "0") == "1";
        g.button(HO_VHOME, on ? "Visual homing: ON" : "Visual homing: off", 16, 122, 288, 40, on ? Kind::Primary : Kind::Normal,
                 Icon::Home);
        g.button(HO_BACK, "Again from step 1", 16, 170, 288, 40, Kind::Normal, Icon::Refresh);
        g.hint(camRef_ ? "reference for visual homing saved" : "visual homing needs the bottom camera way");
        return;
    }
    g.button(HO_ZDOWN, "Z -1", 16, 120, 60, 40);
    g.button(HO_ZUP, "Z +1", 82, 120, 60, 40);
    g.button(HO_GOTO, "Go to camera", 148, 120, 156, 40, Kind::Normal, Icon::Move);
    g.button(HO_FIND, busy_ ? "..." : step_ == 2 ? "Find lens" : "Find tip", 16, 168, 140, 44, Kind::Primary, Icon::Sensor);
    g.button(HO_SAVE, busy_ ? "..." : "Save by eye", 164, 168, 140, 44, Kind::Normal, Icon::Check);
    g.hint("joystick: move X / Y");
}

void HeadOffsetsScreen::save(const char *cmd, int nextStep)
{
    busy_ = true;
    ui.send(cmd, [this, nextStep](const Reply &r) {
        busy_ = false;
        if (r.ok && !r.records.empty())
        {
            const Record &rec = r.records[0];
            char t[80];
            if (nextStep == 1)
            {
                refX_ = (float)atof(rec.str("x").c_str()), refY_ = (float)atof(rec.str("y").c_str());
                haveRef_ = true;
                snprintf(t, sizeof(t), "reference %s / %s mm", rec.str("x").c_str(), rec.str("y").c_str());
            }
            else
            {
                snprintf(t, sizeof(t), "%s  %s / %s mm", nextStep == 2 ? "paste offset" : "camera offset", rec.str("x").c_str(),
                         rec.str("y").c_str());
                if (nextStep == 3)
                    camRef_ = rec.integer("vhome_ref", 0) != 0;
            }
            result_ = t;
            step_ = nextStep;
        }
        machine.refreshSettings();
        ui.redraw();
    });
    ui.redraw();
}

void HeadOffsetsScreen::action(uint16_t id)
{
    if (busy_)
        return;
    char line[96];
    switch (id)
    {
    case HO_ZDOWN:
    case HO_ZUP:
        ui.send(id == HO_ZDOWN ? "JOG z=-1" : "JOG z=1");
        break;
    case HO_GOTO:
    {
        // 1: nozzle over the camera (its calibrated position); 2: paste tip over it (reference - paste
        // offset so far); 3: top camera over it (reference - camera offset so far)
        auto num = [](const char *k) { return (float)atof(machine.setting(k, "0").c_str()); };
        if (step_ == 0)
            snprintf(line, sizeof(line), "MOVE x=%.3f y=%.3f", num("cam_bottom_x"), num("cam_bottom_y"));
        else if (!haveRef_)
        {
            ui.toast("Do step 1 first", th::WARN);
            break;
        }
        else if (step_ == 1)
            snprintf(line, sizeof(line), "MOVE x=%.3f y=%.3f", refX_ - num("head1_offset_x"), refY_ - num("head1_offset_y"));
        else
            snprintf(line, sizeof(line), "MOVE x=%.3f y=%.3f", refX_ - num("cam_top_off_x"), refY_ - num("cam_top_off_y"));
        ui.send(line);
        break;
    }
    case HO_FIND:
        save(step_ == 0 ? "CAL HEADS NOZZLE CAM" : step_ == 1 ? "CAL HEADS PASTE CAM" : "CAL HEADS CAMERA", step_ + 1);
        break;
    case HO_SAVE:
        save(step_ == 0 ? "CAL HEADS NOZZLE" : step_ == 1 ? "CAL HEADS PASTE" : "CAL HEADS CAMERA MANUAL", step_ + 1);
        break;
    case HO_BACK:
        step_ = 0;
        result_.clear();
        break;
    case HO_VHOME:
        ui.send(machine.setting("vhome", "0") == "1" ? "SET vhome 0" : "SET vhome 1", [](const Reply &) {
            machine.refreshSettings();
            ui.redraw();
        });
        break;
    }
}
