// Cameras, calibration, vacuum sensor, joystick (Pi only), and the joystick jog of Move / Teach
#include "screens.h"
#include "../hw/input.h"
#include "../../vision/camera.h"

#include <cmath>

CamerasScreen camerasScreen;
CalibrationScreen calibrationScreen;
CamCalScreen camCalScreen;
VacuumScreen vacuumScreen;
JoystickScreen joystickScreen;

static const char *CAM_NAME[] = {"bottom", "top"};

// ---------------------------------------------------------------- joystick jog

// Velocity jog: while the stick is out, "JOGV x= y=" (mm/s) every 50 ms; the Octopus keeps moving at
// that speed and blends it into one smooth motion. Released: one "JOGV ... 0" stops it (and the board
// stops by itself 300 ms after the last JOGV). Speed grows with the deflection (squared: fine near the
// centre, joy_jog_speed at full deflection).
void stickJog(int axis)
{
    static uint32_t last = 0;
    static bool moving = false;
    static int movingAxis = 0;
    const JoyState j = input.stick();
    const bool xy = axis < 0;
    const bool out = fabsf(j.y) >= 0.01f || (xy && fabsf(j.x) >= 0.01f);
    if (!out)
    {
        if (moving)
        {
            moving = false;
            ui.send(movingAxis < 0 ? "JOGV x=0 y=0" : movingAxis == 2 ? "JOGV z=0" : "JOGV e1=0", nullptr, false, false);
        }
        return;
    }
    if (millis() - last < 50 || ctrl.queued() > 1)
        return;
    last = millis();
    const float vmax = std::max(0.1f, (float)atof(machine.setting("joy_jog_speed", "50").c_str()));
    auto curve = [](float v) { return v * fabsf(v); };
    char cmd[80];
    if (axis == 4)
    {
        // feeder motor: still short steps (E0 has no position limits to blend into)
        snprintf(cmd, sizeof(cmd), "E0 %.2f", curve(j.y) * 20);
        ui.send(cmd, nullptr, false, false);
        return;
    }
    if (xy)
    {
        // joy_jog_invert_x / _y: only the axis movement, the stick itself (menus) stays as it is
        const float sx = machine.setting("joy_jog_invert_x", "0") == "1" ? -1.0f : 1.0f;
        const float sy = machine.setting("joy_jog_invert_y", "0") == "1" ? -1.0f : 1.0f;
        snprintf(cmd, sizeof(cmd), "JOGV x=%.2f y=%.2f", sx * curve(j.x) * vmax, sy * curve(j.y) * vmax);
    }
    else
        snprintf(cmd, sizeof(cmd), "JOGV %s=%.2f", axis == 2 ? "z" : "e1", curve(j.y) * vmax * (axis == 2 ? 0.3f : 1));
    moving = true;
    movingAxis = xy ? -1 : axis;
    ui.send(cmd, nullptr, false, false);
}

// Fields of a record as "key value, key value" (calibration results)
static std::string fieldsText(const Record &r, size_t max = 4)
{
    std::string t;
    for (size_t i = 0; i < r.fields.size() && i < max; i++)
        t += (t.empty() ? "" : ", ") + r.fields[i].first + " " + r.fields[i].second;
    return t;
}

// ---------------------------------------------------------------- cameras

enum
{
    CAM_SWITCH = 1,
    CAM_LIGHT,
    CAM_MEASURE,
    CAM_SAVE
};

void CamerasScreen::open(int cam)
{
    cam_ = cam ? 1 : 0;
    ui.push(this);
}

void CamerasScreen::enter()
{
    result_.clear();
    // INFO opens the camera with the current settings (on the controller thread)
    ui.send(std::string("CAM ") + CAM_NAME[cam_] + " INFO", [this](const Reply &r) {
        if (r.ok && !r.records.empty() && !r.records[0].flag("ok"))
            result_ = "camera: " + r.records[0].str("error", "not available");
    });
}

bool CamerasScreen::press()
{
    action(CAM_SWITCH);
    return true;
}

void CamerasScreen::action(uint16_t id)
{
    const MachineStatus s = machine.status();
    switch (id)
    {
    case CAM_SWITCH:
        cam_ ^= 1;
        enter();
        ui.redraw();
        break;
    case CAM_LIGHT:
    {
        const bool on = (cam_ ? s.lightTop : s.lightBottom) > 0;
        ui.send(std::string("LIGHT ") + CAM_NAME[cam_] + (on ? " OFF" : " ON"));
        break;
    }
    case CAM_MEASURE:
        result_ = "measuring ...";
        ui.send(cam_ ? "CAM top FIDUCIAL" : "CAM bottom MEASURE", [this](const Reply &r) {
            if (!r.ok || r.records.empty())
                return;
            const Record &m = r.records[0];
            char t[96];
            if (!m.flag("ok"))
                result_ = "not found: " + m.str("error", "?");
            else if (cam_)
                snprintf(t, sizeof(t), "mark at dx %.2f  dy %.2f mm", m.num("dx"), m.num("dy")), result_ = t;
            else
                snprintf(t, sizeof(t), "part dx %.2f  dy %.2f mm  %.1f°  %.1f x %.1f", m.num("dx"), m.num("dy"), m.num("angle"),
                         m.num("w"), m.num("h")),
                    result_ = t;
        });
        break;
    case CAM_SAVE:
    {
        const std::string path = std::string("/captures/") + CAM_NAME[cam_] + "-" + std::to_string(millis()) + ".jpg";
        ui.send(std::string("CAM ") + CAM_NAME[cam_] + " SAVE " + path, [this, path](const Reply &r) {
            if (r.ok)
                ui.toast("Saved " + path);
        });
        break;
    }
    }
}

void CamerasScreen::draw(Gfx &g)
{
    const MachineStatus s = machine.status();
    g.background();
    g.titleBar(cam_ ? "Top camera" : "Bottom camera");

    // Picture: newest frame, fitted into 212 x 160 design px, crosshair in the centre
    const int bx = 4, by = 28, bw = 212, bh = 160;
    g.rrect(bx, by, bw, bh, 6, th::PANEL);
    Frame f;
    if (cams.latest(cam_, f) && !f.gray.empty())
    {
        int pw = g.pw(bx, bw), ph = g.ph(by, bh);
        if ((long)f.gray.w * ph > (long)f.gray.h * pw)
            ph = pw * f.gray.h / f.gray.w;
        else
            pw = ph * f.gray.w / f.gray.h;
        std::vector<uint8_t> rgb = frameRgb(f, pw, ph);
        cams.drawOverlay(cam_, f, rgb, pw, ph); // green outline of what the camera just found
        px_.resize((size_t)pw * ph);
        for (size_t i = 0; i < px_.size(); i++)
            px_[i] = (uint16_t)(((rgb[i * 3] & 0xF8) << 8) | ((rgb[i * 3 + 1] & 0xFC) << 3) | (rgb[i * 3 + 2] >> 3));
        const int dw = (int)lroundf(pw / Gfx::KX), dh = (int)lroundf(ph / Gfx::KY);
        const int x0 = bx + (bw - dw) / 2, y0 = by + (bh - dh) / 2;
        g.image(px_.data(), pw, ph, x0, y0);
        const int cx = x0 + dw / 2, cy = y0 + dh / 2;
        g.line(cx - 14, cy, cx + 14, cy, th::BAD);
        g.line(cx, cy - 14, cx, cy + 14, th::BAD);
        g.circle(cx, cy, 8, th::BAD);
    }
    else
    {
        g.icon(Icon::Warn, bx + bw / 2 - 7, by + bh / 2 - 22, th::WARN);
        const std::string err = cams.error(cam_);
        g.text(err.empty() ? "no picture yet" : err, bx + bw / 2, by + bh / 2, Font::Small, th::DIM, Align::Center);
    }

    const bool light = (cam_ ? s.lightTop : s.lightBottom) > 0;
    g.button(CAM_SWITCH, cam_ ? "Bottom" : "Top", 222, 30, 94, 34, Kind::Normal, Icon::Refresh, Font::Small);
    g.button(CAM_LIGHT, light ? "Light on" : "Light off", 222, 70, 94, 34, light ? Kind::Primary : Kind::Normal, Icon::None,
             Font::Small);
    g.button(CAM_MEASURE, cam_ ? "Find mark" : "Measure", 222, 110, 94, 34, Kind::Normal, Icon::Check, Font::Small);
    g.button(CAM_SAVE, "Save", 222, 150, 94, 34, Kind::Normal, Icon::File, Font::Small);

    char pos[64];
    snprintf(pos, sizeof(pos), "X %.2f  Y %.2f  Z %.2f", s.x, s.y, s.z);
    g.text(pos, 8, 198, Font::Small, th::TEXT);
    if (!result_.empty())
        g.text(result_, 8, 214, Font::Small, th::ACCENT);
    g.hint("stick: move the head · press: other camera");
}

// ---------------------------------------------------------------- calibration

enum
{
    CAL_ROWS = 100
};

static std::string fmtSetting(const char *key, const char *fmt)
{
    const std::string v = machine.setting(key, "");
    if (v.empty())
        return "-";
    char t[32];
    snprintf(t, sizeof(t), fmt, atof(v.c_str()));
    return t;
}

void CalibrationScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Calibration");
    std::vector<Row> rows(13);
    rows[0].icon = Icon::Home, rows[0].label = "Sensorless homing", rows[0].sub = "measure, set the stall threshold, test";
    rows[1].icon = Icon::Move, rows[1].label = "Axis length", rows[1].sub = "how far X, Y or the gantry can travel";
    rows[2].icon = Icon::Move, rows[2].label = "Positions", rows[2].sub = "camera, load cell, nozzle slots, drop point";
    rows[3].icon = Icon::Sensor, rows[3].label = "Bottom camera", rows[3].sub = "checkerboard, nozzle tip, lens, rotation";
    rows[3].value = fmtSetting("cam_bottom_m00", "%.4f");
    rows[4].icon = Icon::Sensor, rows[4].label = "Top camera", rows[4].sub = "checkerboard, mark, lens, offset";
    rows[4].value = fmtSetting("cam_top_m00", "%.4f");
    rows[5].icon = Icon::Test, rows[5].label = "Head offsets", rows[5].sub = "nozzle, paste tip, camera on one point";
    rows[6].icon = Icon::Pump, rows[6].label = "Vacuum sensor", rows[6].sub = "baseline, pick threshold";
    rows[7].icon = Icon::Move, rows[7].label = "Joystick", rows[7].sub = "centre, range, test";
    rows[8].icon = Icon::Sensor, rows[8].label = "Nozzle sensor order", rows[8].sub = "trigger the slots in order";
    rows[9].icon = Icon::Touch, rows[9].label = "Touch screen", rows[9].sub = "tap 4 crosses";
    rows[10].icon = Icon::Sensor, rows[10].label = "Live cameras", rows[10].sub = "picture, lights, jog with the stick";
    rows[11].icon = Icon::Move, rows[11].label = "Move / teach", rows[11].sub = "jog the head with the stick";
    rows[12].icon = Icon::Gear, rows[12].label = "All values", rows[12].sub = "cam_*, vac_*, joy_*, vision_* in Settings";
    for (int i = 0; i < 12; i++)
        rows[i].chev = true;
    list_.draw(g, CAL_ROWS, rows);
}

// Runs a CAL command, then shows what was saved (lens / checkerboard: distortion and fit error)
void calRun(const std::string &cmd, const std::string &what)
{
    ui.toast(what + " ...", th::ACCENT);
    ui.send(cmd, [what](const Reply &r) {
        machine.refreshSettings();
        if (!r.ok)
            return; // error toast from ui.send
        std::string l1 = "Saved:", l2 = r.records.empty() ? std::string("done") : fieldsText(r.records.back());
        if (!r.records.empty())
        {
            const Record &l = r.records.back();
            const std::string w = l.str("what");
            if (w.find("_lens") != std::string::npos || w.find("_grid") != std::string::npos)
            {
                l1 = "k1 " + l.str("k1") + " · error " + l.str("rms_px_before") + " -> " + l.str("rms_px_after") + " px";
                l2 = l.str("mm_per_px") + " mm/px";
                if (w.find("_grid") != std::string::npos)
                    l2 += " · " + l.str("squares") + " squares";
                if (l.find("move_scale"))
                    l2 += " · moves " + l.str("move_scale");
            }
        }
        if (l2.size() > 44)
            l2 = l2.substr(0, 44);
        dialog.open(what, l1, l2, {{"OK", Kind::Primary, nullptr}}, Icon::Check, th::OK);
    });
}

void CalibrationScreen::action(uint16_t id)
{
    switch (id - CAL_ROWS)
    {
    case 0:
        ui.push(&sgCalScreen);
        break;
    case 1:
        ui.push(&findLengthScreen);
        break;
    case 2:
        ui.push(&positionsScreen);
        break;
    case 3:
        camCalScreen.open(0);
        break;
    case 4:
        camCalScreen.open(1);
        break;
    case 5:
        ui.push(&headOffsetsScreen);
        break;
    case 6:
        ui.push(&vacuumScreen);
        break;
    case 7:
        ui.push(&joystickScreen);
        break;
    case 8:
        ui.push(&nozzleLearnScreen);
        break;
    case 9:
        ui.push(&touchCalScreen);
        break;
    case 10:
        camerasScreen.open(1);
        break;
    case 11:
        ui.push(&moveScreen);
        break;
    case 12:
        ui.push(&settingsScreen);
        break;
    }
}

// ---------------------------------------------------------------- one camera

enum
{
    CC_ROWS = 100
};

static const float GRID_SIZES[] = {1, 1.5f, 2, 2.5f, 3, 4, 5, 10};

void cycleGridSize()
{
    const float now = (float)atof(machine.setting("cal_grid", "2").c_str());
    float next = GRID_SIZES[0];
    for (float s : GRID_SIZES)
        if (s > now + 0.01f)
        {
            next = s;
            break;
        }
    char line[48];
    snprintf(line, sizeof(line), "SET cal_grid %g", (double)next);
    ui.send(line, [](const Reply &) {
        machine.refreshSettings();
        ui.redraw();
    });
}

void CamCalScreen::open(int cam)
{
    cam_ = cam;
    ui.push(this);
}

void CamCalScreen::draw(Gfx &g)
{
    const bool top = cam_ == 1;
    g.background();
    g.titleBar(top ? "Top camera" : "Bottom camera");
    std::vector<Row> rows(top ? 8 : 7);
    const std::string pre = top ? "cam_top_" : "cam_bottom_";
    int i = 0;
    if (!top)
    {
        // The checkerboard keeps the rotation of the nozzle tip calibration, so that one first
        rows[i].icon = Icon::Sensor, rows[i].label = "1. Nozzle tip", rows[i].sub = "tip over the camera: scale, rotation, centre";
        rows[i++].value = fmtSetting("cam_bottom_m00", "%.4f");
        rows[i].icon = Icon::Sensor, rows[i].label = "2. Checkerboard", rows[i].sub = "printed grid over the camera: lens, scale";
        rows[i++].value = "k1 " + fmtSetting("cam_bottom_k1", "%.3f");
    }
    else
    {
        rows[i].icon = Icon::Sensor, rows[i].label = "Checkerboard", rows[i].sub = "printed grid under the camera: lens, scale, rotation";
        rows[i++].value = "k1 " + fmtSetting("cam_top_k1", "%.3f");
        rows[i].icon = Icon::Sensor, rows[i].label = "On a mark", rows[i].sub = "camera centred above a mark: scale, rotation";
        rows[i++].value = fmtSetting("cam_top_m00", "%.4f");
    }
    rows[i].label = "Square size", rows[i].value = fmtSetting("cal_grid", "%g") + " mm", rows[i++].sub = "of the printed checkerboard · tap: next size";
    rows[i].icon = Icon::Sensor, rows[i].label = "Lens without checkerboard";
    rows[i++].sub = top ? "mark at 9 places in the picture" : "nozzle tip at 9 places in the picture";
    if (top)
    {
        rows[i].icon = Icon::Move, rows[i].label = "Offset 1/2", rows[i++].sub = "nozzle tip exactly on a mark";
        rows[i].icon = Icon::Move, rows[i].label = "Offset 2/2", rows[i].sub = "camera roughly above that mark";
        rows[i++].value = fmtSetting("cam_top_off_x", "%.2f") + " / " + fmtSetting("cam_top_off_y", "%.2f");
    }
    else
    {
        rows[i].icon = Icon::Refresh, rows[i].label = "Rotation direction", rows[i].sub = "part on the nozzle over the camera";
        rows[i++].value = fmtSetting("cam_bottom_rot_sign", "%+.0f");
    }
    rows[i].icon = Icon::Sensor, rows[i].label = "Live picture", rows[i].chev = true, rows[i++].sub = "lights, jog with the stick";
    rows[i].icon = Icon::File, rows[i].label = "Printable checkerboard", rows[i++].sub = "website: /checkerboard.html";
    rows.resize(i);
    list_.draw(g, CC_ROWS, rows);
}

void CamCalScreen::action(uint16_t id)
{
    const bool top = cam_ == 1;
    const std::string cam = top ? "TOP" : "BOTTOM", name = top ? "Top camera" : "Bottom camera";
    int row = id - CC_ROWS;
    auto checkerboard = [&] {
        dialog.open("Checkerboard?", top ? "Printed grid flat under the camera," : "Printed grid over the camera, face down,",
                    "picture full of sharp squares.",
                    {{"No", Kind::Normal, nullptr}, {"Start", Kind::Primary, [cam, name] { calRun("CAL " + cam + " GRID", name + " checkerboard"); }}},
                    Icon::Sensor, th::ACCENT, 1);
    };
    auto direct = [&] {
        if (top)
            dialog.open("Calibrate on a mark?", "The head moves around the mark;", "keep the mark in the picture.",
                        {{"No", Kind::Normal, nullptr}, {"Start", Kind::Primary, [] { calRun("CAL TOP", "Top camera"); }}},
                        Icon::Sensor, th::ACCENT, 1);
        else
            dialog.open("Calibrate on the nozzle tip?", "The head moves around the camera;", "the nozzle tip must be in the picture.",
                        {{"No", Kind::Normal, nullptr}, {"Start", Kind::Primary, [] { calRun("CAL BOTTOM", "Bottom camera"); }}},
                        Icon::Sensor, th::ACCENT, 1);
    };
    switch (row)
    {
    case 0:
        top ? checkerboard() : direct();
        break;
    case 1:
        top ? direct() : checkerboard();
        break;
    case 2:
        cycleGridSize();
        break;
    case 3:
        dialog.open("Lens without checkerboard?", top ? "Mark in the picture centre;" : "Nozzle tip in the picture centre;",
                    top ? "it moves the camera to 9 places." : "it moves the tip to 9 places.",
                    {{"No", Kind::Normal, nullptr}, {"Start", Kind::Primary, [cam, name] { calRun("CAL " + cam + " LENS", name + " lens"); }}},
                    Icon::Sensor, th::ACCENT, 1);
        break;
    case 4:
        if (top)
            calRun("CAL TOPOFFSET MARK", "Nozzle position");
        else
            calRun("CAL ROTSIGN", "Rotation direction");
        break;
    default:
        if (top && row == 5)
            calRun("CAL TOPOFFSET", "Top camera offset");
        else if (row == (top ? 6 : 5))
            camerasScreen.open(cam_);
        else
            ui.toast("Print it from the website: /checkerboard.html", th::ACCENT);
        break;
    }
}

// ---------------------------------------------------------------- vacuum

enum
{
    VAC_PUMP = 1,
    VAC_BASE,
    VAC_OPEN,
    VAC_BLOCKED
};

void VacuumScreen::tick()
{
    if (waiting_ || millis() - lastPoll_ < 250)
        return;
    lastPoll_ = millis();
    waiting_ = true;
    ui.send(
        "VACUUM",
        [this](const Reply &r) {
            waiting_ = false;
            if (!r.ok || r.records.empty())
                return;
            const Record &v = r.records[0];
            sensor_ = v.flag("sensor");
            hpa_ = v.num("hpa"), base_ = v.num("baseline"), drop_ = v.num("drop"), pickMin_ = v.num("pick_min");
            part_ = v.flag("part");
        },
        false, false);
}

void VacuumScreen::draw(Gfx &g)
{
    const MachineStatus s = machine.status();
    g.background();
    g.titleBar("Vacuum sensor");
    g.card(8, 30, 304, 88);
    char t[48];
    if (!sensor_)
        g.text("no BMP280 found (I2C 0x76 / 0x77)", 160, 66, Font::Body, th::WARN, Align::Center);
    else
    {
        snprintf(t, sizeof(t), "%.1f hPa", drop_);
        g.text(t, 20, 40, Font::Big, part_ ? th::OK : th::TEXT);
        g.text("below the baseline", 20, 74, Font::Small, th::DIM);
        g.pill(part_ ? "part on the nozzle" : "no part", 200, 46, part_ ? th::OK : th::DIM, part_ ? th::ACCENT_BG : th::PANEL2);
        snprintf(t, sizeof(t), "now %.1f · baseline %.1f · pick %.0f hPa", hpa_, base_, pickMin_);
        g.text(t, 20, 96, Font::Small, th::DIM);
    }
    g.button(VAC_PUMP, s.pump > 0 ? "Pump off" : "Pump on", 8, 126, 148, 36, s.pump > 0 ? Kind::Primary : Kind::Normal, Icon::Pump);
    g.button(VAC_BASE, "Baseline", 164, 126, 148, 36, Kind::Normal, Icon::Check);
    g.button(VAC_OPEN, "Nozzle open", 8, 168, 148, 36, Kind::Normal, Icon::Check);
    g.button(VAC_BLOCKED, "Nozzle blocked", 164, 168, 148, 36, Kind::Normal, Icon::Check);
    g.hint("baseline: pump off · then pump on, open / blocked");
}

void VacuumScreen::action(uint16_t id)
{
    auto cal = [](const char *what, const char *title) {
        ui.send(std::string("CAL VACUUM ") + what, [title](const Reply &r) {
            machine.refreshSettings();
            if (r.ok)
                ui.toast(std::string(title) + (r.records.empty() ? "" : ": " + fieldsText(r.records.back(), 2)));
        });
    };
    switch (id)
    {
    case VAC_PUMP:
        ui.send(machine.status().pump > 0 ? "PUMP OFF" : "PUMP ON");
        break;
    case VAC_BASE:
        cal("BASE", "Baseline");
        break;
    case VAC_OPEN:
        cal("OPEN", "Open nozzle");
        break;
    case VAC_BLOCKED:
        cal("BLOCKED", "Blocked nozzle");
        break;
    }
}

// ---------------------------------------------------------------- joystick

enum
{
    JOY_CENTER = 1,
    JOY_RANGE,
    JOY_INVX,
    JOY_INVY
};

void JoystickScreen::tick()
{
    // The UI's own reading (input.stick()) is live; JOY adds the raw ADC values
    if (waiting_ || millis() - lastPoll_ < 100)
        return;
    lastPoll_ = millis();
    waiting_ = true;
    ui.send(
        "JOY",
        [this](const Reply &r) {
            waiting_ = false;
            if (!r.ok || r.records.empty())
                return;
            const Record &j = r.records[0];
            x_ = j.num("x"), y_ = j.num("y"), rawX_ = (int)j.integer("raw_x"), rawY_ = (int)j.integer("raw_y");
            button_ = j.flag("button"), adc_ = j.flag("adc");
        },
        false, false);
}

void JoystickScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Joystick");
    const int cx = 96, cy = 104, r = 66;
    g.disc(cx, cy, r, th::PANEL);
    g.ring(cx, cy, r, th::LINE);
    const float dz = atof(machine.setting("joy_deadzone", "8").c_str()) / 100.0f;
    g.ring(cx, cy, std::max(2, (int)lroundf(r * dz)), th::DIM);
    g.hline(cx - r, cy, 2 * r, th::LINE);
    g.line(cx, cy - r, cx, cy + r, th::LINE);
    g.disc(cx + (int)lroundf(x_ * r), cy - (int)lroundf(y_ * r), 7, button_ ? th::OK : th::ACCENT);
    char t[48];
    snprintf(t, sizeof(t), "x %+.2f  y %+.2f", x_, y_);
    g.text(t, cx, 180, Font::Small, th::TEXT, Align::Center);
    snprintf(t, sizeof(t), adc_ ? "raw %d / %d" : "no ADS1115 (I2C 0x48)", rawX_, rawY_);
    g.text(t, cx, 194, Font::Small, adc_ ? th::DIM : th::WARN, Align::Center);

    g.button(JOY_CENTER, "Centre", 196, 32, 118, 36, Kind::Normal, Icon::Check);
    g.button(JOY_RANGE, "Range (5 s)", 196, 74, 118, 36, Kind::Normal, Icon::Refresh);
    const bool ix = machine.setting("joy_invert_x", "0") == "1", iy = machine.setting("joy_invert_y", "0") == "1";
    g.button(JOY_INVX, ix ? "X inverted" : "X normal", 196, 116, 118, 36, ix ? Kind::Primary : Kind::Normal);
    g.button(JOY_INVY, iy ? "Y inverted" : "Y normal", 196, 158, 118, 36, iy ? Kind::Primary : Kind::Normal);
    g.hint("centre: let go · range: circle the stick to its edges");
}

void JoystickScreen::action(uint16_t id)
{
    switch (id)
    {
    case JOY_CENTER:
        ui.send("CAL JOY CENTER", [](const Reply &r) {
            machine.refreshSettings();
            if (r.ok)
                ui.toast("Centre saved");
        });
        break;
    case JOY_RANGE:
        ui.toast("Circle the stick to its edges ...", th::ACCENT);
        ui.send("CAL JOY RANGE ms=5000", [](const Reply &r) {
            machine.refreshSettings();
            if (r.ok)
                ui.toast("Range saved");
        });
        break;
    case JOY_INVX:
    case JOY_INVY:
    {
        const char *key = id == JOY_INVX ? "joy_invert_x" : "joy_invert_y";
        ui.send(std::string("SET ") + key + (machine.setting(key, "0") == "1" ? " 0" : " 1"),
                [](const Reply &) { machine.refreshSettings(); });
        break;
    }
    }
}
