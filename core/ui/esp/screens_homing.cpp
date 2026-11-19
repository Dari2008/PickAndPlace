// Testing -> Homing (one axis at a time) and Calibration -> Sensorless homing (StallGuard threshold)
#include "screens.h"
#include "widgets.h"
#include "../machine/machine.h"

#include <cstdio>
#include <cstdlib>

HomeAxesScreen homeAxesScreen;
SgCalScreen sgCalScreen;

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
    const std::string hint = running_.empty() ? "One axis at a time: see how each one homes" : running_ + " ...";
    g.hint(hint.c_str());
}

void HomeAxesScreen::action(uint16_t id)
{
    const size_t i = id - HA_ROWS, n = sizeof(HOME_ITEMS) / sizeof(HOME_ITEMS[0]);
    if (i == n)
    {
        ui.send("HALT", nullptr, false);
        running_.clear();
        return;
    }
    if (i > n || !running_.empty())
        return;
    running_ = HOME_ITEMS[i].label;
    ui.toast(running_ + " ...", th::ACCENT);
    const std::string what = HOME_ITEMS[i].label;
    ui.send(HOME_ITEMS[i].cmd, [this, what](const Reply &r) {
        running_.clear();
        if (r.ok)
            ui.toast(what + ": done", th::OK);
    });
}

// ---------------------------------------------------------------- Calibration -> Sensorless homing

enum
{
    SG_ROWS = 10
};

static const char *const SG_AXES[] = {"x", "y", "e1"};
static const char *const SG_NAMES[] = {"X", "Y", "Gantry (E1)"};

std::string SgCalScreen::key(const char *what, int which) const
{
    return std::string(what) + (which == 1 ? "x2" : SG_AXES[axis_]);
}

void SgCalScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Sensorless homing");
    const bool x = axis_ == 0;
    std::vector<Row> rows(x ? 9 : 8);
    int i = 0;
    rows[i].icon = Icon::Motor, rows[i].label = "Axis", rows[i].value = SG_NAMES[axis_], rows[i].sub = "tap: next axis";
    i++;
    rows[i].icon = Icon::Move, rows[i].label = "Jog -10 mm", rows[i].sub = x || axis_ == 1 ? "towards home" : "gantry";
    i++;
    rows[i].icon = Icon::Move, rows[i].label = "Jog +10 mm", rows[i].sub = "away from home";
    i++;
    rows[i].icon = Icon::Test, rows[i].label = busy_ ? "Measuring ..." : "Measure";
    rows[i].sub = result_.empty() ? "runs 50 mm away from home and back" : result_;
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
    rows[i].value = suggest_[0] < 0 ? "-" : x ? std::to_string(suggest_[0]) + " / " + std::to_string(suggest_[1]) : std::to_string(suggest_[0]);
    rows[i].sub = "35 % of the lowest value while running free";
    i++;
    rows[i].label = "Homing current", rows[i].value = machine.setting("home_cur", "-") + " %";
    rows[i].sub = "home_cur, % of the run current (all axes)";
    i++;
    rows[i].icon = Icon::Home, rows[i].label = "Test: home this axis", rows[i].sub = "with the saved threshold";
    list_.draw(g, SG_ROWS, rows, 25, 35);
    g.hint("Measure in free travel, not against the end");
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
    const bool x = axis_ == 0;
    int row = (int)id - SG_ROWS;
    if (!x && row >= 5)
        row++; // no "Threshold X2" row: the rows below move up
    if (busy_ && row != 0)
        return;
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
        result_.clear();
        ui.send(std::string("SGTEST ") + SG_AXES[axis_], [this](const Reply &r) {
            busy_ = false;
            if (!r.ok)
            {
                result_ = "failed: " + r.error;
                return;
            }
            result_.clear();
            for (size_t k = 0; k < r.records.size() && k < 2; k++)
            {
                const Record &rec = r.records[k];
                char t[64];
                snprintf(t, sizeof(t), "%s%s %s/%s/%s", k ? " · " : "", rec.str("axis").c_str(), rec.str("min").c_str(),
                         rec.str("avg").c_str(), rec.str("max").c_str());
                result_ += t;
                suggest_[k] = rec.integer("suggest", -1);
            }
            result_ += " (min/avg/max)";
        });
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
        if (suggest_[0] < 0)
        {
            ui.toast("Measure first", th::WARN);
            break;
        }
        save(key("tmc_sg_"), (float)suggest_[0]);
        if (x && suggest_[1] >= 0)
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
    case 8:
    {
        static const char *const CMD[] = {"HOME X", "HOME Y", "HOME E1"};
        busy_ = true;
        ui.toast(std::string("Homing ") + SG_NAMES[axis_] + " ...", th::ACCENT);
        ui.send(CMD[axis_], [this](const Reply &r) {
            busy_ = false;
            if (r.ok)
                ui.toast(std::string(SG_NAMES[axis_]) + " homed", th::OK);
        });
        break;
    }
    }
}
