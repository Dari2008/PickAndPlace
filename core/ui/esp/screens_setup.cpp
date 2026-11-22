// Menu -> Setup: walks through bringing the machine up, one step per page, in the order the steps depend
// on each other (touch, joystick, homing, axes, Z, cameras, offsets, positions, vacuum, nozzles, check).
// Each page opens the screen that does the step, or does it right here. "Done" goes on; the step reached
// is kept in the setting setup_step.
#include "screens.h"

#include <cmath>
#include "../hw/input.h"

SetupScreen setupScreen;

namespace
{
struct Step
{
    const char *title, *l1, *l2, *l3;
};

const Step STEPS[] = {
    {"Touch screen", "Tap the 4 crosses one after the other,", "so a touch hits what it should.", ""},
    {"Joystick", "Centre and range of the stick,", "then check the directions.", ""},
    {"Homing", "Home all. Stops early or rams the end:", "Sensorless homing (measure, threshold).", ""},
    {"Axis length", "Each axis runs to its far end:", "save the length as travel (soft limit).", ""},
    {"Steps per mm", "Moves 100 mm (towards the side with room):", "measure how far it went, set it, Correct.", ""},
    {"Paste height", "Paper on bed: Z+ until the paste tip grips", "it, save. Sprung nozzles: no height here.", ""},
    {"Bottom camera", "Bare nozzle over the bottom camera, at", "the height it sees parts. Then Calibrate.", ""},
    {"Bottom camera lens", "Checkerboard face down over the camera", "(sharp, fills the picture), or tip at 9 places.", ""},
    {"Rotation direction", "Pump on, hold a part on the nozzle over", "the bottom camera, then Measure.", ""},
    {"Top camera", "Checkerboard flat on the bed under the", "camera. Result 'moves' should be ~1.00.", ""},
    {"Head offsets", "Nozzle, paste tip and top camera on one", "point (the bottom camera). Visual homing on.", ""},
    {"Positions", "Bottom camera, load cell, nozzle slots", "and the reject point.", ""},
    {"Vacuum sensor", "Baseline (pump off), then pump on:", "nozzle open and nozzle blocked.", ""},
    {"Nozzle sensor order", "nozzle_count set first. Then trigger the", "slots in order, slot 1 first.", ""},
    {"Check", "Top camera: a mark lands on the cross.", "Bottom camera: the outline fits a part.", ""},
    {"Setup complete", "Everything is calibrated.", "Redo a camera + head offsets after moving it.", ""},
};
const int N = sizeof(STEPS) / sizeof(STEPS[0]);
const int LAST = N - 1; // the "complete" page

enum
{
    SU_A = 1,      // first row: SU_A + 0..3
    SU_B = 5,      // second row: SU_B + 0..3
    SU_BACK = 9,
    SU_SKIP,
    SU_NEXT
};

const char *const AXES[] = {"x", "y", "e1"};
const char *const AXIS_NAMES[] = {"X", "Y", "Gantry"};

std::string gridLabel()
{
    char t[24];
    snprintf(t, sizeof(t), "Squares %g mm", atof(machine.setting("cal_grid", "2").c_str()));
    return t;
}
} // namespace

int SetupScreen::steps() { return N; }

void SetupScreen::enter()
{
    step_ = std::max(0, std::min(LAST, atoi(machine.setting("setup_step", "0").c_str())));
    measured_ = 100;
}

void SetupScreen::go(int step)
{
    step_ = std::max(0, std::min(LAST, step));
    measured_ = 100;
    char line[40];
    snprintf(line, sizeof(line), "SET setup_step %d", step_);
    ui.send(line, [](const Reply &) { machine.refreshSettings(); }, false, false);
    ui.redraw();
}

std::vector<std::string> SetupScreen::row(int r) const
{
    const bool pump = machine.status().pump > 0;
    if (r == 0)
        switch (step_)
        {
        case 0: return {"Calibrate touch"};
        case 1: return {"Joystick"};
        case 2: return {"Home all", "Sensorless homing"};
        case 3: return {"Axis length"};
        case 4: return {std::string("Axis ") + AXIS_NAMES[axis_], "Move 100 mm", "Correct"};
        case 5: return {"Z -1", "Z -0.1", "Z +0.1", "Z +1"};
        case 6: return {"Live picture", "Calibrate"};
        case 7: return {"Checkerboard", "9 places", gridLabel()};
        case 8: return {pump ? "Pump off" : "Pump on", "Measure"};
        case 9: return {"Live picture", "Checkerboard", gridLabel()};
        case 10: return {"Head offsets"};
        case 11: return {"Positions"};
        case 12: return {"Vacuum sensor"};
        case 13: return {"Sensor order"};
        case 14: return {"Top camera", "Bottom camera"};
        default: return {"Start over"};
        }
    switch (step_)
    {
    case 4: return {"-1", "-0.1", "+0.1", "+1"};
    case 5: return {"Paste touches"};
    case 7: return {"Live picture"};
    case 8: return {"Live picture"};
    case 9: return {"Without board: on a mark"};
    default: return {};
    }
}

// Stick: X / Y; with the stick button held down: Z (stick up / down)
void SetupScreen::tick()
{
    const bool z = input.buttonDown();
    if (z && fabsf(input.stick().y) >= 0.01f)
        zJogged_ = true;
    stickJog(z ? 2 : -1);
}

// Button released after moving Z: no click on the focused button
bool SetupScreen::press()
{
    const bool used = zJogged_;
    zJogged_ = false;
    return used;
}

// Held to move Z (stick already out): no "back"
bool SetupScreen::hold()
{
    const bool used = zJogged_ || fabsf(input.stick().y) >= 0.01f;
    zJogged_ = false; // no press follows a hold
    return used;
}

std::string SetupScreen::liveLine() const
{
    char t[80];
    const MachineStatus s = machine.status();
    switch (step_)
    {
    case 4:
        snprintf(t, sizeof(t), "measured %.1f mm · steps_%s %s", (double)measured_, AXES[axis_],
                 machine.setting((std::string("steps_") + AXES[axis_]).c_str(), "-").c_str());
        return t;
    case 5:
        snprintf(t, sizeof(t), "Z %.2f · paste reach %s", (double)s.z, machine.setting("head1_reach", "-").c_str());
        return t;
    case 6:
    case 8:
    case 9:
    case 14:
        snprintf(t, sizeof(t), "head X %.2f  Y %.2f  Z %.2f · stick XY, held Z", (double)s.x, (double)s.y, (double)s.z);
        return t;
    case 7:
    case 13:
        return step_ == 7 ? "print: website /checkerboard.html" : "nozzle_count " + machine.setting("nozzle_count", "-");
    default:
        return STEPS[step_].l3;
    }
}

void SetupScreen::draw(Gfx &g)
{
    char t[24];
    snprintf(t, sizeof(t), "%d / %d", std::min(step_ + 1, LAST), LAST);
    g.background();
    g.titleBar("Setup", true, step_ < LAST ? t : "done", step_ < LAST ? th::DIM : th::OK);
    g.progress(16, 29, 288, 4, (float)step_ / LAST, th::ACCENT);
    g.card(16, 38, 288, 76);
    const Step &st = STEPS[step_];
    g.text(st.title, 28, 52, Font::BodyBold, step_ == LAST ? th::OK : th::TEXT);
    g.text(st.l1, 28, 72, Font::Small, th::DIM);
    g.text(st.l2, 28, 88, Font::Small, th::DIM);
    g.text(g.fit(liveLine(), Font::Small, 270), 28, 104, Font::Small, th::ACCENT);

    for (int r = 0; r < 2; r++)
    {
        const std::vector<std::string> b = row(r);
        const int n = (int)b.size();
        if (!n)
            continue;
        const int y = r ? 158 : 120, h = r ? 30 : 34, w = (288 - (n - 1) * 6) / n;
        for (int i = 0; i < n; i++)
            g.button((r ? SU_B : SU_A) + i, b[i].c_str(), 16 + i * (w + 6), y, w, h, r == 0 && i == n - 1 && n < 4 ? Kind::Primary : Kind::Normal,
                     Icon::None, n >= 4 || b[i].size() > 14 ? Font::Small : Font::BodyBold);
    }
    g.button(SU_BACK, "Back", 16, 194, 80, 30, Kind::Normal, Icon::Back, Font::Small);
    if (step_ < LAST)
    {
        g.button(SU_SKIP, "Skip", 102, 194, 80, 30, Kind::Normal, Icon::Skip, Font::Small);
        g.button(SU_NEXT, "Done, next", 188, 194, 116, 30, Kind::Primary, Icon::Check, Font::Small);
    }
}

uint8_t SetupScreen::liveRects(Rect *r) const
{
    r[0] = {16, 96, 288, 18}; // live line
    r[1] = {16, 118, 288, 38}; // pump button text
    return 2;
}

void SetupScreen::action(uint16_t id)
{
    if (id == SU_BACK)
        return step_ > 0 ? go(step_ - 1) : ui.pop();
    if (id == SU_SKIP || id == SU_NEXT)
        return go(step_ + 1);
    const int a = id >= SU_B ? -1 : id - SU_A, b = id >= SU_B ? id - SU_B : -1;
    char line[64];
    switch (step_)
    {
    case 0:
        ui.push(&touchCalScreen);
        break;
    case 1:
        ui.push(&joystickScreen);
        break;
    case 2:
        if (a == 0)
            homingScreen.start();
        else
            ui.push(&sgCalScreen);
        break;
    case 3:
        ui.push(&findLengthScreen);
        break;
    case 4:
        if (a == 0)
            axis_ = (axis_ + 1) % 3, measured_ = 100;
        else if (a == 1)
        {
            // Only homed, and only towards the side with 100 mm of room (it must never run into an end)
            const MachineStatus s = machine.status();
            if (!(axis_ == 2 ? s.e1Homed : s.homed))
            {
                ui.toast(axis_ == 2 ? "Home the gantry first" : "Home X + Y first (Homing step)", th::BAD);
                break;
            }
            const float pos = axis_ == 0 ? s.x : axis_ == 1 ? s.y : s.e1;
            const float travel = (float)atof(machine.setting((std::string("travel_") + AXES[axis_]).c_str(), "0").c_str());
            const float margin = 2; // stay clear of the ends
            float d = 0;
            if (travel > 0 && pos + 100 <= travel - margin)
                d = 100;
            else if (travel > 0 && pos - 100 >= margin)
                d = -100;
            if (d == 0)
            {
                snprintf(line, sizeof(line), "No room for 100 mm (at %.0f, travel %.0f)", (double)pos, (double)travel);
                ui.toast(line, th::BAD);
                break;
            }
            snprintf(line, sizeof(line), "JOG %s=%.0f speed=20", AXES[axis_], (double)d);
            ui.send(line);
            measured_ = 100;
        }
        else if (a == 2)
        {
            const std::string key = std::string("steps_") + AXES[axis_];
            const float old = (float)atof(machine.setting(key.c_str(), "0").c_str());
            if (old <= 0 || measured_ <= 0)
            {
                ui.toast("no steps value", th::BAD);
                break;
            }
            snprintf(line, sizeof(line), "SET %s %.3f", key.c_str(), (double)(old * 100.0f / measured_));
            ui.send(line, [](const Reply &r) {
                machine.refreshSettings();
                if (r.ok)
                    ui.toast("Corrected - home again, then move again to check");
            });
            measured_ = 100;
        }
        else if (b >= 0)
        {
            static const float D[4] = {-1, -0.1f, 0.1f, 1};
            measured_ = std::max(1.0f, measured_ + D[b]);
        }
        break;
    case 5:
        if (a >= 0)
        {
            static const char *const J[4] = {"JOG z=-1", "JOG z=-0.1", "JOG z=0.1", "JOG z=1"};
            ui.send(J[a]);
        }
        else
        {
            // only the paste tip: the vacuum nozzles are sprung, their reach comes from the load cell touch-off
            const float z = machine.status().z;
            if (z <= 0)
            {
                ui.toast("Z must be above 0. Paste down at Z<0? Set invert_z 1", th::BAD);
                break;
            }
            snprintf(line, sizeof(line), "SET head1_reach %.3f", (double)z);
            ui.send(line, [](const Reply &r) {
                machine.refreshSettings();
                if (r.ok)
                    ui.toast("Saved");
            });
        }
        break;
    case 6:
        if (a == 0)
            camerasScreen.open(0);
        else
            calRun("CAL BOTTOM", "Bottom camera");
        break;
    case 7:
        if (b == 0)
            camerasScreen.open(0);
        else if (a == 0)
            calRun("CAL BOTTOM GRID", "Bottom camera checkerboard");
        else if (a == 1)
            calRun("CAL BOTTOM LENS", "Bottom camera lens");
        else
            cycleGridSize();
        break;
    case 8:
        if (b == 0)
            camerasScreen.open(0);
        else if (a == 0)
            ui.send(machine.status().pump > 0 ? "PUMP OFF" : "PUMP ON");
        else
            calRun("CAL ROTSIGN", "Rotation direction");
        break;
    case 9:
        if (a == 0)
            camerasScreen.open(1);
        else if (a == 1)
            calRun("CAL TOP GRID", "Top camera checkerboard");
        else if (a == 2)
            cycleGridSize();
        else
            calRun("CAL TOP", "Top camera");
        break;
    case 10:
        ui.push(&headOffsetsScreen);
        break;
    case 11:
        ui.push(&positionsScreen);
        break;
    case 12:
        ui.push(&vacuumScreen);
        break;
    case 13:
        ui.push(&nozzleLearnScreen);
        break;
    case 14:
        camerasScreen.open(a == 0 ? 1 : 0);
        break;
    default:
        go(0);
        break;
    }
    ui.redraw();
}
