// Cameras, calibration, vacuum sensor, joystick (Pi only), and the joystick jog of Move / Teach
#include "screens.h"
#include "../hw/input.h"
#include "../../vision/camera.h"

#include <cmath>

CamerasScreen camerasScreen;
CalibrationScreen calibrationScreen;
VacuumScreen vacuumScreen;
JoystickScreen joystickScreen;

static const char *CAM_NAME[] = {"bottom", "top"};

// ---------------------------------------------------------------- joystick jog

void stickJog(int axis)
{
    static uint32_t last = 0;
    const JoyState j = input.stick();
    const bool xy = axis < 0;
    if (fabsf(j.y) < 0.01f && (!xy || fabsf(j.x) < 0.01f))
        return;
    // One short move every ~100 ms, never more than one waiting: stops at once on release
    if (millis() - last < 100 || ctrl.queued() > 1)
        return;
    last = millis();
    const float vmax = std::max(0.1f, (float)atof(machine.setting("joy_jog_speed", "50").c_str()));
    auto curve = [](float v) { return v * fabsf(v); }; // fine control near the centre
    const float dt = 0.12f;
    char cmd[80];
    if (xy)
    {
        const float vx = curve(j.x) * vmax, vy = curve(j.y) * vmax;
        snprintf(cmd, sizeof(cmd), "JOG x=%.3f y=%.3f speed=%.1f", vx * dt, vy * dt, std::max(0.5f, hypotf(vx, vy)));
    }
    else if (axis == 4)
        snprintf(cmd, sizeof(cmd), "E0 %.2f", curve(j.y) * 20);
    else
    {
        const float v = curve(j.y) * vmax * (axis == 2 ? 0.3f : 1);
        snprintf(cmd, sizeof(cmd), "JOG %s=%.3f speed=%.1f", axis == 2 ? "z" : "e1", v * dt, std::max(0.5f, fabsf(v)));
    }
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
    std::vector<Row> rows(12);
    rows[0].icon = Icon::Sensor, rows[0].label = "Bottom camera", rows[0].sub = "mm/px · nozzle tip over the camera";
    rows[0].value = fmtSetting("cam_bottom_m00", "%.4f");
    rows[1].icon = Icon::Sensor, rows[1].label = "Top camera", rows[1].sub = "mm/px · camera centred above a mark";
    rows[1].value = fmtSetting("cam_top_m00", "%.4f");
    rows[2].icon = Icon::Move, rows[2].label = "Top camera offset 1/2", rows[2].sub = "nozzle tip exactly on a mark";
    rows[3].icon = Icon::Move, rows[3].label = "Top camera offset 2/2", rows[3].sub = "camera roughly above that mark";
    rows[3].value = fmtSetting("cam_top_off_x", "%.2f") + " / " + fmtSetting("cam_top_off_y", "%.2f");
    rows[4].icon = Icon::Refresh, rows[4].label = "Rotation direction", rows[4].sub = "part on the nozzle over the bottom camera";
    rows[4].value = fmtSetting("cam_bottom_rot_sign", "%+.0f");
    rows[5].icon = Icon::Pump, rows[5].label = "Vacuum sensor", rows[5].sub = "baseline, pick threshold", rows[5].chev = true;
    rows[6].icon = Icon::Move, rows[6].label = "Joystick", rows[6].sub = "centre, range, test", rows[6].chev = true;
    rows[7].icon = Icon::Sensor, rows[7].label = "Cameras", rows[7].sub = "live picture, lights", rows[7].chev = true;
    rows[8].icon = Icon::Move, rows[8].label = "Move / teach", rows[8].sub = "jog the head with the stick", rows[8].chev = true;
    rows[9].icon = Icon::Gear, rows[9].label = "All values", rows[9].sub = "cam_*, vac_*, joy_*, vision_* in Settings";
    rows[10].icon = Icon::Sensor, rows[10].label = "Nozzle sensor order", rows[10].sub = "trigger the slots in order", rows[10].chev = true;
    rows[11].icon = Icon::Home, rows[11].label = "Sensorless homing", rows[11].sub = "measure, set the stall threshold, test";
    rows[11].chev = true;
    list_.draw(g, CAL_ROWS, rows);
}

void CalibrationScreen::run(const std::string &cmd, const std::string &what)
{
    ui.toast(what + " ...", th::ACCENT);
    ui.send(cmd, [what](const Reply &r) {
        machine.refreshSettings();
        if (!r.ok)
            return; // error toast from ui.send
        std::string res = r.records.empty() ? std::string("done") : fieldsText(r.records.back());
        dialog.open(what, "Saved:", res.size() > 44 ? res.substr(0, 44) : res, {{"OK", Kind::Primary, nullptr}}, Icon::Check, th::OK);
    });
}

void CalibrationScreen::action(uint16_t id)
{
    switch (id - CAL_ROWS)
    {
    case 0:
        dialog.open("Calibrate the bottom camera?", "The head moves around the camera;", "the nozzle tip must be in the picture.",
                    {{"No", Kind::Normal, nullptr}, {"Start", Kind::Primary, [this] { run("CAL BOTTOM", "Bottom camera"); }}},
                    Icon::Sensor, th::ACCENT, 1);
        break;
    case 1:
        dialog.open("Calibrate the top camera?", "The head moves around the mark;", "keep the mark in the picture.",
                    {{"No", Kind::Normal, nullptr}, {"Start", Kind::Primary, [this] { run("CAL TOP", "Top camera"); }}},
                    Icon::Sensor, th::ACCENT, 1);
        break;
    case 2:
        run("CAL TOPOFFSET MARK", "Nozzle position");
        break;
    case 3:
        run("CAL TOPOFFSET", "Top camera offset");
        break;
    case 4:
        run("CAL ROTSIGN", "Rotation direction");
        break;
    case 5:
        ui.push(&vacuumScreen);
        break;
    case 6:
        ui.push(&joystickScreen);
        break;
    case 7:
        camerasScreen.open(1);
        break;
    case 8:
        ui.push(&moveScreen);
        break;
    case 9:
        ui.push(&settingsScreen);
        break;
    case 10:
        ui.push(&nozzleLearnScreen);
        break;
    case 11:
        ui.push(&sgCalScreen);
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
