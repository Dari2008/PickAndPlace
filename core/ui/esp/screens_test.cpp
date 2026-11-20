// Outputs (pump, valve, fans, lights), testing, sensors, motor test, touch calibration
#include <cstdlib>
#include "../hw/touch.h"
#include "screens.h"

PumpScreen pumpScreen;
TestingScreen testingScreen;
SensorsScreen sensorsScreen;
MotorTestScreen motorTestScreen;
TouchCalScreen touchCalScreen;
TouchSettingsScreen touchSettingsScreen;
TouchTestScreen touchTestScreen;

static NumberEditor::Spec percentSpec(const std::string &title, const std::string &what, float value)
{
    NumberEditor::Spec s;
    s.title = title;
    s.what = what;
    s.value = value;
    s.unit = "%";
    s.min = 0;
    s.max = 100;
    s.decimals = 0;
    s.steps = {5, 1, 25};
    return s;
}

static void allOff()
{
    ui.send("PUMP OFF");
    ui.send("VALVE OFF", [](const Reply &r) {
        if (r.ok)
            ui.toast("All outputs off");
    });
}

// ---------------------------------------------------------------- pump & fans

enum
{
    PU_ROWS = 10
};

void PumpScreen::draw(Gfx &g)
{
    const MachineStatus s = machine.status();
    const std::string power = machine.setting("pump_power", "100");
    g.background();
    g.titleBar("Pump, valve & fans");
    std::vector<Row> rows(10);
    rows[0].icon = Icon::Pump, rows[0].label = "Vacuum pump", rows[0].toggle = s.pump > 0;
    rows[0].sub = s.pumpUsed ? "Octopus FAN5" : "not connected: stays off";
    rows[1].label = "Pump power", rows[1].value = power + " %", rows[1].sub = "used when switched on";
    rows[2].label = "Pump connected", rows[2].toggle = s.pumpUsed;
    rows[2].sub = s.pumpUsed ? "off = no pump: jobs run without it" : "off: PUMP keeps FAN5 off";
    rows[3].label = "Valve", rows[3].sub = "Octopus HE1", rows[3].toggle = s.valve;
    rows[4].label = "Head camera fan", rows[4].value = machine.setting("cam_fan", "50") + " %", rows[4].sub = "Octopus FAN2 · always on";
    rows[5].label = "Housing fan", rows[5].value = machine.setting("box_fan", "100") + " %", rows[5].sub = "Octopus FAN3 · always on";
    rows[6].label = "Pi housing fan", rows[6].value = machine.setting("pi_fan", "100") + " %", rows[6].sub = "Octopus FAN4 · always on";
    rows[7].label = "Top camera light", rows[7].sub = "FAN0 · always on, dark while the bottom light is on";
    rows[7].toggle = s.lightTop > 0;
    rows[8].label = "Bottom camera light", rows[8].sub = "NeoPixel ring on the RGB header", rows[8].toggle = s.lightBottom > 0;
    rows[9].label = "All off", rows[9].icon = Icon::Power, rows[9].iconColor = th::BAD;
    list_.draw(g, PU_ROWS, rows, 25, 35);
}

void PumpScreen::action(uint16_t id)
{
    const MachineStatus s = machine.status();
    switch (id - PU_ROWS)
    {
    case 0:
        ui.send(s.pump > 0 ? "PUMP OFF" : "PUMP ON");
        break;
    case 1:
        numberEditor.open(percentSpec("Pump power", "% when the pump is switched on", atof(machine.setting("pump_power", "100").c_str())),
                          [](float v) {
                              ui.send("SET pump_power " + std::to_string((int)v), [](const Reply &r) {
                                  machine.refreshSettings(); // active even if saving failed
                                  if (r.ok)
                                      ui.toast("Saved to the SD card");
                              });
                          });
        break;
    case 2:
        ui.send(std::string("SET pump_used ") + (s.pumpUsed ? "0" : "1"), [](const Reply &r) {
            machine.refreshSettings();
            if (r.ok)
                ui.toast("Saved to the SD card");
        });
        break;
    case 3:
        ui.send(s.valve ? "VALVE OFF" : "VALVE ON");
        break;
    case 4:
    case 5:
    case 6:
    {
        static const char *const KEYS[] = {"cam_fan", "box_fan", "pi_fan"}, *const NAMES[] = {"Head camera fan", "Housing fan", "Pi housing fan"};
        const int k = id - PU_ROWS - 4;
        const char *key = KEYS[k];
        const char *def = k == 0 ? "50" : "100";
        numberEditor.open(percentSpec(NAMES[k], "always on, saved",
                                      atof(machine.setting(key, def).c_str())),
                          [key](float v) {
                              ui.send(std::string("SET ") + key + " " + std::to_string((int)v), [](const Reply &r) {
                                  machine.refreshSettings();
                                  if (r.ok)
                                      ui.toast("Saved to the SD card");
                              });
                          });
        break;
    }
    case 7:
        ui.send(s.lightTop > 0 ? "LIGHT top OFF" : "LIGHT top IDLE");
        break;
    case 8:
        ui.send(s.lightBottom > 0 ? "LIGHT bottom OFF" : "LIGHT bottom ON");
        break;
    case 9:
        allOff();
        break;
    }
}

// ---------------------------------------------------------------- testing

enum
{
    T_ROWS = 10
};

void TestingScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Testing");
    std::vector<Row> rows(7);
    rows[0].icon = Icon::Pump, rows[0].label = "Outputs", rows[0].sub = "pump, valve, fans", rows[0].chev = true;
    rows[1].icon = Icon::Sensor, rows[1].label = "Sensors", rows[1].sub = "switches, stall, vacuum, nozzles, joystick", rows[1].chev = true;
    rows[2].icon = Icon::Sensor, rows[2].label = "Read a QR code", rows[2].sub = "top camera, where it looks now";
    rows[3].icon = Icon::Motor, rows[3].label = "Test motors", rows[3].sub = "X Y Z E0 E1 ROT PASTE by degrees", rows[3].chev = true;
    rows[4].icon = Icon::Home, rows[4].label = "Homing", rows[4].sub = "home X, Y, Z or the gantry on its own", rows[4].chev = true;
    rows[5].icon = Icon::Move, rows[5].label = "Find length", rows[5].sub = "how far X, Y or the gantry can travel", rows[5].chev = true;
    rows[6].icon = Icon::Power, rows[6].iconColor = th::BAD, rows[6].label = "All off", rows[6].sub = "outputs off, motion stopped";
    list_.draw(g, T_ROWS, rows, 25, 35);
}

void TestingScreen::action(uint16_t id)
{
    switch (id - T_ROWS)
    {
    case 0:
        ui.push(&pumpScreen);
        break;
    case 1:
        ui.push(&sensorsScreen);
        break;
    case 2:
        ui.toast("Reading ...", th::ACCENT);
        ui.send("CAM top QR", [](const Reply &r) {
            if (r.ok && !r.records.empty())
                ui.toast("QR: " + r.records[0].str("text"));
        });
        break;
    case 3:
        ui.push(&motorTestScreen);
        break;
    case 4:
        ui.push(&homeAxesScreen);
        break;
    case 5:
        ui.push(&findLengthScreen);
        break;
    case 6:
        ui.send("HALT", nullptr, false);
        allOff();
        break;
    }
}

// ---------------------------------------------------------------- sensors

// Octopus inputs (SENSORS): real switches, then the sensorless stall signals (DIAG jumpers)
struct SensorInput
{
    const char *key, *name, *pin;
};
static const SensorInput SWITCHES[] = {
    {"z_flag", "Z flag (seesaw)", "DIAG2"}, {"estop", "E-stop button", "DIAG5"}, {"feeder", "Feeder sensor", "DIAG4"}};
static const SensorInput STALLS[] = {
    {"x_min", "X stall", "DIAG0"}, {"x2_min", "X2 stall", "DIAG1"}, {"e1_home", "Gantry stall", "DIAG6"}, {"y_min", "Y stall", "DIAG7"}};

void SensorsScreen::tick()
{
    if (waiting_ || millis() - lastPoll_ < 300)
        return;
    lastPoll_ = millis();
    waiting_ = 4;
    auto first = [](const Reply &r, const char *type) {
        for (auto &rec : r.records)
            if (rec.type == type)
                return rec;
        return Record();
    };
    ui.send("SENSORS", [this, first](const Reply &r) { waiting_--; if (r.ok) sensors_ = first(r, "sensors"); }, false, false);
    ui.send("VACUUM", [this, first](const Reply &r) { waiting_--; if (r.ok) vacuum_ = first(r, "vacuum"); }, false, false);
    ui.send("JOY", [this, first](const Reply &r) { waiting_--; if (r.ok) joy_ = first(r, "joy"); }, false, false);
    ui.send("NOZZLES", [this](const Reply &r) {
        waiting_--;
        if (!r.ok)
            return;
        nozzles_.clear();
        for (auto &rec : r.records)
            if (rec.type == "nozzle")
                nozzles_.push_back(rec);
    }, false, false);
}

void SensorsScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Sensors", true, "live");
    std::vector<Row> rows;
    auto header = [&](const char *t) {
        Row r;
        r.label = t, r.bold = true, r.labelColor = th::ACCENT;
        rows.push_back(r);
    };
    auto input = [&](const SensorInput &in, const char *hitText, const char *idleText) {
        Row r;
        const bool known = sensors_.find(in.key) != nullptr;
        const bool hit = known && sensors_.flag(in.key);
        r.icon = hit ? Icon::Warn : Icon::Check;
        r.iconColor = !known ? th::DIM : hit ? th::BAD : th::OK;
        r.label = in.name, r.sub = in.pin;
        r.value = !known ? "-" : hit ? hitText : idleText;
        r.valueColor = hit ? th::BAD : th::DIM;
        rows.push_back(r);
    };
    char t[48];

    header("Switches");
    for (auto &in : SWITCHES)
        input(in, "HIT", "open");
    header("Sensorless homing (StallGuard)");
    for (auto &in : STALLS)
        input(in, "STALL", "free");

    if (sensors_.find("lc"))
    {
        header("Load cell");
        Row r;
        r.icon = Icon::Sensor, r.label = "Force on the pad", r.sub = "HX711 on EXP1";
        if (!sensors_.flag("lc"))
            r.value = "not found", r.valueColor = th::WARN;
        else
        {
            snprintf(t, sizeof(t), "%.1f g", sensors_.num("load"));
            r.value = t, r.valueColor = th::TEXT;
        }
        rows.push_back(r);
    }

    header("Vacuum");
    {
        Row r;
        r.icon = Icon::Pump, r.label = "Vacuum sensor (BMP280)";
        if (!vacuum_.flag("sensor"))
            r.value = "not found", r.valueColor = th::WARN;
        else
        {
            snprintf(t, sizeof(t), "%.1f hPa", vacuum_.num("hpa"));
            r.value = t, r.valueColor = th::TEXT;
            snprintf(t, sizeof(t), "drop %.1f hPa · baseline %.1f", vacuum_.num("drop"), vacuum_.num("baseline"));
            r.sub = t;
        }
        rows.push_back(r);
        Row part;
        part.label = "Part on the nozzle";
        part.value = !vacuum_.flag("sensor") ? "-" : vacuum_.flag("part") ? "yes" : "no";
        part.valueColor = vacuum_.flag("part") ? th::OK : th::DIM;
        rows.push_back(part);
    }

    header("Nozzle rack");
    if (nozzles_.empty())
    {
        Row r;
        r.label = "No rack set up", r.sub = "Settings > Nozzle changer";
        rows.push_back(r);
    }
    for (auto &n : nozzles_)
    {
        Row r;
        const long present = n.integer("present", -1);
        r.icon = present == 1 ? Icon::Check : Icon::None;
        r.iconColor = th::OK;
        snprintf(t, sizeof(t), "Slot %ld", n.integer("num"));
        r.label = t;
        r.sub = n.str("name").empty() ? "slot " + std::to_string(n.integer("num")) : n.str("name");
        r.value = n.flag("mounted") ? "on the head" : present == 1 ? "nozzle" : present == 0 ? "empty" : "-";
        r.valueColor = present == 1 ? th::OK : th::DIM;
        rows.push_back(r);
    }

    header("Joystick");
    {
        Row r;
        r.icon = Icon::Move, r.label = "Stick";
        snprintf(t, sizeof(t), "x %+.2f  y %+.2f", joy_.num("x"), joy_.num("y"));
        r.value = t, r.valueColor = th::TEXT;
        snprintf(t, sizeof(t), "raw %ld / %ld%s", joy_.integer("raw_x"), joy_.integer("raw_y"), joy_.flag("adc") ? "" : " · no ADS1115");
        r.sub = t;
        rows.push_back(r);
        Row b;
        b.label = "Stick button", b.sub = "GPIO17";
        b.value = joy_.flag("button") ? "pressed" : "released";
        b.valueColor = joy_.flag("button") ? th::OK : th::DIM;
        rows.push_back(b);
    }
    list_.draw(g, 10, rows, 25, 32);
}

// ---------------------------------------------------------------- motor test

// Every Octopus driver slot except the spare; speed from test_speed_<axis>
static const char *MT_AXES[] = {"x", "y", "z", "e0", "e1", "rot", "paste"};
static const char *MT_NAMES[] = {"X", "Y", "Z", "E0", "E1", "ROT", "PST"};
static const int MT_COUNT = 7;
static const int MT_DEG[] = {-90, -10, -1, 1, 10, 90};

enum
{
    MT_AXIS = 1, // + MT_COUNT
    MT_DEG0 = 10, // + 6
    MT_SPEED = 20,
    MT_OFF
};

void MotorTestScreen::move(float deg)
{
    turned_ += deg;
    char c[32];
    snprintf(c, sizeof(c), "MOTOR %s %.0f", MT_AXES[axis_], deg);
    ui.send(c);
}

bool MotorTestScreen::turn(int8_t steps)
{
    move(steps * 10.0f);
    return true;
}

void MotorTestScreen::draw(Gfx &g)
{
    const bool tool = axis_ >= 5; // no speed setting
    const std::string speed = tool ? "180" : machine.setting(("test_speed_" + std::string(MT_AXES[axis_])).c_str(), "180");
    g.background();
    g.titleBar("Test motor", true, (speed + " °/s").c_str());
    for (int i = 0; i < MT_COUNT; i++)
        g.button(MT_AXIS + i, MT_NAMES[i], 6 + i * 44, 32, 40, 30, i == axis_ ? Kind::Primary : Kind::Normal,
                 Icon::None, Font::Small);
    g.card(8, 72, 304, 64);
    g.text("turned", 20, 88, Font::Small, th::DIM);
    char t[24];
    snprintf(t, sizeof(t), "%+.0f°", turned_);
    g.text(t, 20, 114, Font::Huge, th::TEXT);
    g.text("stick up / down: 10° per step", 300, 114, Font::Small, th::DIM, Align::Right);
    for (int i = 0; i < 6; i++)
    {
        snprintf(t, sizeof(t), "%+d°", MT_DEG[i]);
        g.button(MT_DEG0 + i, t, 8 + i * 52, 146, 46, 36, Kind::Normal, Icon::None, Font::Small);
    }
    if (!tool)
        g.button(MT_SPEED, ("Speed " + speed + " °/s").c_str(), 8, 192, 148, 36);
    else
        g.text(axis_ == 5 ? "nozzle rotation driver" : "paste extruder driver", 82, 210, Font::Small, th::DIM, Align::Center);
    g.button(MT_OFF, "Motors off", 164, 192, 148, 36, Kind::Normal, Icon::Power);
}

void MotorTestScreen::action(uint16_t id)
{
    if (id >= MT_AXIS && id < MT_AXIS + MT_COUNT)
    {
        axis_ = id - MT_AXIS;
        turned_ = 0;
    }
    else if (id >= MT_DEG0 && id < MT_DEG0 + 6)
        move(MT_DEG[id - MT_DEG0]);
    else if (id == MT_OFF)
        ui.send("MOTORS OFF", [](const Reply &r) {
            if (r.ok)
                ui.toast("Motors released");
        });
    else if (id == MT_SPEED)
    {
        const std::string key = "test_speed_" + std::string(MT_AXES[axis_]);
        NumberEditor::Spec s;
        s.title = "Test speed " + std::string(MT_NAMES[axis_]);
        s.what = "motor shaft degrees per second";
        s.value = atof(machine.setting(key.c_str(), "180").c_str());
        s.unit = "°/s";
        s.min = 1;
        s.max = 65000;
        s.decimals = 0;
        s.steps = {10, 100, 1000};
        numberEditor.open(s, [key](float v) {
            ui.send("SET " + key + " " + std::to_string((int)v), [](const Reply &) {
                machine.refreshSettings(); // active even if saving failed
            });
        });
    }
}

// ---------------------------------------------------------------- touch calibration

static const int16_t CAL_X[] = {20, 300, 300, 20};
static const int16_t CAL_Y[] = {20, 20, 220, 220};

// From the website (SCREEN CALIBRATE): open it, or start over when it is already open
void openTouchCalibration()
{
    if (ui.top() == &touchCalScreen)
        touchCalScreen.enter();
    else
        ui.push(&touchCalScreen);
    ui.redraw();
}

bool TouchCalScreen::press()
{
    ui.pop(); // cancel
    return true;
}

void TouchCalScreen::rawTouch(int16_t rx, int16_t ry)
{
    rx_[point_] = rx;
    ry_[point_] = ry;
    if (++point_ < 4)
        return;
    // Which raw axis follows the screen's x? Compare the move from cross 0 to 1.
    const bool swap = abs(ry_[1] - ry_[0]) > abs(rx_[1] - rx_[0]);
    int a[4], b[4];
    for (int i = 0; i < 4; i++)
    {
        a[i] = swap ? ry_[i] : rx_[i];
        b[i] = swap ? rx_[i] : ry_[i];
    }
    const float ax20 = (a[0] + a[3]) / 2.0f, ax300 = (a[1] + a[2]) / 2.0f;
    const float by20 = (b[0] + b[1]) / 2.0f, by220 = (b[2] + b[3]) / 2.0f;
    // Raw per display pixel, from the crosses at design x 20 / 300 and y 20 / 220.
    const float sx = (ax300 - ax20) / (280.0f * Gfx::KX), sy = (by220 - by20) / (200.0f * Gfx::KY);
    if (fabsf(sx) < 0.3f || fabsf(sy) < 0.3f)
    {
        ui.toast("Didn't work - try again", th::BAD);
        point_ = 0;
        return;
    }
    TouchCal c;
    c.swapXY = swap;
    c.xMin = (int16_t)(ax20 - 20 * Gfx::KX * sx);
    c.xMax = (int16_t)(ax20 + (SCREEN_W - 1 - 20 * Gfx::KX) * sx);
    c.yMin = (int16_t)(by20 - 20 * Gfx::KY * sy);
    c.yMax = (int16_t)(by20 + (SCREEN_H - 1 - 20 * Gfx::KY) * sy);
    touch.setCal(c);
    ui.pop();
    ui.toast("Touch calibrated");
}

void TouchCalScreen::draw(Gfx &g)
{
    g.background();
    g.text("Touch calibration", 160, 96, Font::Big, th::TEXT, Align::Center);
    char t[48];
    snprintf(t, sizeof(t), "Tap the centre of each cross (%d of 4)", point_ + 1);
    g.text(t, 160, 118, Font::Body, th::DIM, Align::Center);
    g.text("press the joystick to cancel", 160, 138, Font::Small, th::DIM, Align::Center);
    for (int i = 0; i < 4; i++)
    {
        const uint16_t c = i == point_ ? th::ACCENT : i < point_ ? th::OK : th::DIM;
        g.line(CAL_X[i] - 12, CAL_Y[i], CAL_X[i] + 12, CAL_Y[i], c, 2);
        g.line(CAL_X[i], CAL_Y[i] - 12, CAL_X[i], CAL_Y[i] + 12, c, 2);
        g.ring(CAL_X[i], CAL_Y[i], 6, c, 1.5f);
    }
}

// ---------------------------------------------------------------- touch settings

enum
{
    TS_ROWS = 10 // on, calibrate, test, pressure, scroll, press time, reset
};

void TouchSettingsScreen::draw(Gfx &g)
{
    const TouchOptions o = touch.options();
    g.background();
    g.titleBar("Touch");
    std::vector<Row> rows(7);
    rows[0].label = "Touch input", rows[0].toggle = o.enabled;
    rows[0].sub = o.enabled ? "off: the joystick still works" : "off - turn on with the joystick";
    rows[1].icon = Icon::Touch, rows[1].label = "Calibrate", rows[1].sub = "tap 4 crosses", rows[1].chev = true;
    rows[2].icon = Icon::Test, rows[2].label = "Touch test", rows[2].sub = "where it sees the finger, pressure", rows[2].chev = true;
    rows[3].label = "Minimum pressure", rows[3].value = std::to_string(o.minPressure);
    rows[3].sub = "lighter touches are ignored (see Touch test)";
    rows[4].label = "Scroll threshold", rows[4].value = std::to_string(o.dragStart) + " px";
    rows[4].sub = "finger movement before a touch scrolls";
    rows[5].label = "Minimum press time", rows[5].value = std::to_string(o.minPressMs) + " ms";
    rows[5].sub = "shorter taps are ignored (ghost touches)";
    rows[6].icon = Icon::Refresh, rows[6].label = "Reset touch settings", rows[6].sub = "calibration and options back to default";
    list_.draw(g, TS_ROWS, rows, 25, 35);
}

static NumberEditor::Spec touchSpec(const char *title, const char *what, float value, const char *unit, float min,
                                    float max, std::vector<float> steps)
{
    NumberEditor::Spec s;
    s.title = title;
    s.what = what;
    s.value = value;
    s.unit = unit;
    s.min = min;
    s.max = max;
    s.decimals = 0;
    s.steps = steps;
    return s;
}

void TouchSettingsScreen::action(uint16_t id)
{
    TouchOptions o = touch.options();
    switch (id - TS_ROWS)
    {
    case 0:
        o.enabled = !o.enabled;
        touch.setOptions(o);
        break;
    case 1:
        ui.push(&touchCalScreen);
        break;
    case 2:
        ui.push(&touchTestScreen);
        break;
    case 3:
        numberEditor.open(touchSpec("Minimum pressure", "touches below this are ignored", o.minPressure, "", 0, 4095, {1, 10, 100}),
                          [](float v) {
                              TouchOptions o = touch.options();
                              o.minPressure = (uint16_t)v;
                              touch.setOptions(o);
                          });
        break;
    case 4:
        numberEditor.open(touchSpec("Scroll threshold", "movement before a touch scrolls", o.dragStart, "px", 2, 60, {1, 5}),
                          [](float v) {
                              TouchOptions o = touch.options();
                              o.dragStart = (uint8_t)v;
                              touch.setOptions(o);
                          });
        break;
    case 5:
        numberEditor.open(touchSpec("Minimum press time", "shorter taps are ignored", o.minPressMs, "ms", 0, 500, {10, 50}),
                          [](float v) {
                              TouchOptions o = touch.options();
                              o.minPressMs = (uint16_t)v;
                              touch.setOptions(o);
                          });
        break;
    case 6:
        dialog.open("Reset touch settings?", "Calibration and options go back", "to the defaults.",
                    {{"No", Kind::Normal, nullptr},
                     {"Reset", Kind::Danger, [] {
                          touch.setCal(TouchCal());
                          touch.setOptions(TouchOptions());
                          ui.toast("Touch reset - calibrate it again");
                      }}},
                    Icon::Refresh, th::WARN, 0);
        break;
    }
}

// ---------------------------------------------------------------- touch test

static const size_t TRAIL_MAX = 80;

void TouchTestScreen::tick()
{
    static uint32_t lastRead = 0;
    if (millis() - lastRead < 25)
        return;
    lastRead = millis();
    int16_t rx, ry;
    uint16_t pressure;
    const bool down = touch.readRaw(rx, ry, pressure);
    TouchPoint p;
    const bool ok = down && touch.read(p);
    if (down)
    {
        rx_ = rx, ry_ = ry, pressure_ = pressure;
        accepted_ = ok;
        if (ok)
        {
            trail_.push_back({(int16_t)(p.x / Gfx::KX), (int16_t)(p.y / Gfx::KY)});
            if (trail_.size() > TRAIL_MAX)
                trail_.erase(trail_.begin());
        }
    }
    if (down || down != down_)
        ui.redraw();
    down_ = down;
}

void TouchTestScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Touch test");
    for (int x = 40; x < 320; x += 40)
        g.rect(x, 25, 1, 215, th::PANEL);
    for (int y = 65; y < 240; y += 40)
        g.rect(0, y, 320, 1, th::PANEL);
    for (size_t i = 0; i < trail_.size(); i++)
        g.disc(trail_[i].x, trail_[i].y, 2, i + 1 == trail_.size() ? th::ACCENT : th::ACCENT_BG);
    if (down_ && !trail_.empty() && accepted_)
    {
        const Dot &d = trail_.back();
        g.line(d.x - 14, d.y, d.x + 14, d.y, th::ACCENT, 2);
        g.line(d.x, d.y - 14, d.x, d.y + 14, th::ACCENT, 2);
        g.ring(d.x, d.y, 8, th::ACCENT, 1.5f);
    }
    char t[96];
    if (down_)
        snprintf(t, sizeof(t), "raw %d / %d  ·  pressure %u  ·  %s", rx_, ry_, (unsigned)pressure_,
                 accepted_ ? "accepted" : "ignored (too light or touch off)");
    else
        snprintf(t, sizeof(t), "Touch anywhere: the cross should sit under the finger");
    g.text(t, 160, 230, Font::Small, down_ && !accepted_ ? th::WARN : th::DIM, Align::Center);
}
