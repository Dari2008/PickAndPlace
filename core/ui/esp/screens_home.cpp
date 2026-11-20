// Start, controller offline, home, menu, homing, alarm
#include <cmath>
#include "../net/wifi_setup.h"
#include "screens.h"

BootScreen bootScreen;
OfflineScreen offlineScreen;
HomeScreen homeScreen;
MenuScreen menuScreen;
HomingScreen homingScreen;
AlarmScreen alarmScreen;

// ---------------------------------------------------------------- helpers

std::string quote(const std::string &s)
{
    if (!s.empty() && s.find_first_of(" \"=") == std::string::npos)
        return s;
    std::string q = "\"";
    for (char c : s)
        q += c == '"' ? '\'' : c;
    return q + "\"";
}

std::string baseName(const std::string &path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string fmtTime(long s)
{
    if (s < 0)
        return "-";
    char buf[16];
    if (s >= 3600)
        snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else
        snprintf(buf, sizeof(buf), "%ld:%02ld", s / 60, s % 60);
    return buf;
}

int rssiBars(int rssi)
{
    return rssi > -60 ? 3 : rssi > -72 ? 2 : 1;
}

int wifiBars()
{
    const WifiInfo w = wifiSetup.info();
    return w.state == "connected" ? rssiBars(w.rssi) : 0;
}

// "http://192.168.1.20", "connecting to HomeNet", "not set up"
static std::string wifiLine(const WifiInfo &w)
{
    if (!w.ip.empty())
        return "http://" + w.ip;
    if (w.ssid.empty() && w.pending.empty())
        return "WiFi not set up";
    return "WiFi: connecting to " + (w.pending.empty() ? w.ssid : w.pending) + " ...";
}

static void spinner(Gfx &g, int x, int y, int r, uint16_t c)
{
    const float a = fmodf(millis() * 0.36f, 360);
    g.arc(x, y, r - 2, r, a, a + 270, c);
}

// ---------------------------------------------------------------- boot

void BootScreen::tick()
{
    if (machine.status().connected && ui.top() == this)
        ui.replace(&homeScreen);
}

uint8_t BootScreen::animRects(Rect *r) const
{
    r[0] = {104, 176, 112, 12}; // moving bar
    r[1] = {0, 192, 320, 16};   // WiFi line
    return 2;
}

void BootScreen::draw(Gfx &g)
{
    g.background();
    g.rrect(160 - 26, 62, 52, 52, 14, th::ACCENT_BG);
    g.icon(Icon::Feeders, 160 - 7, 81, th::ACCENT);
    g.text("Pick & Place", 160, 136, Font::Big, th::TEXT, Align::Center);
    g.text("Connecting to the controller ...", 160, 160, Font::Body, th::DIM, Align::Center);
    // Back and forth, slowing down at both ends: 1.2 s per direction.
    const float phase = fmodf(millis() / 1200.0f, 2.0f); // 0..2
    const float t = (1.0f - cosf(phase * 3.14159265f)) / 2;
    g.rrect(110, 180, 100, 4, 2, th::PANEL2);
    g.rrect(110 + (int)(t * 70), 180, 30, 4, 2, th::ACCENT);
    const WifiInfo w = wifiSetup.info();
    g.text(wifiLine(w), 160, 200, Font::Small, w.ip.empty() ? th::DIM : th::ACCENT, Align::Center);
    g.button(1, "WiFi settings", 100, 212, 120, 24, Kind::Ghost, Icon::Wifi, Font::Small);
}

void BootScreen::action(uint16_t id)
{
    if (id == 1)
        ui.push(&wifiListScreen); // works before the controller answers
}

// ---------------------------------------------------------------- offline

void OfflineScreen::tick()
{
    if (machine.status().connected && ui.top() == this)
        ui.pop();
}

void OfflineScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Pick & Place", false);
    g.rrect(160 - 22, 50, 44, 44, 12, th::BAD_BG);
    g.icon(Icon::Warn, 160 - 7, 65, th::BAD);
    g.text("Controller not answering", 160, 114, Font::Big, th::TEXT, Align::Center);
    g.text("No status from the controller (pap-core) for 2 s.", 160, 136, Font::Small, th::DIM, Align::Center);
    g.text("It restarts by itself; log: journalctl -u pap-core", 160, 152, Font::Small, th::DIM, Align::Center);
    g.button(1, "WiFi", 24, 186, 128, 34, Kind::Normal, Icon::Wifi);
    g.button(2, "Retry", 168, 186, 128, 34, Kind::Primary, Icon::Refresh);
}

void OfflineScreen::action(uint16_t id)
{
    if (id == 1)
        ui.push(&wifiScreen);
    else if (id == 2)
        ctrl.send("REPORT 250", nullptr, 1000);
}

// ---------------------------------------------------------------- home

enum
{
    H_HOME = 5,
    H_JOBS = 10,
    H_FEEDERS,
    H_MOVE,
    H_MENU
};

void HomeScreen::enter()
{
    machine.refreshFeeders();
    layout_ = -1;
}

void HomeScreen::tick()
{
    // Homed, job running and alarm change the layout: then a full redraw.
    const MachineStatus s = machine.status();
    const int layout = (s.homed && s.e1Homed ? 1 : 0) | (machine.job().active() ? 2 : 0) | (s.state == "alarm" ? 4 : 0);
    if (layout != layout_)
    {
        if (layout_ >= 0)
            ui.redraw();
        layout_ = layout;
    }
}

uint8_t HomeScreen::liveRects(Rect *r) const
{
    r[0] = {0, 26, 320, 76}; // state chips and positions
    r[1] = {8, 100, 148, 66}; // Jobs tile (job progress)
    return 2;
}

static void stateChip(Gfx &g, int &x, int y, const MachineStatus &s, const JobStatus &j)
{
    struct
    {
        const char *t;
        uint16_t c, bg;
    } chip = {"IDLE", th::OK, th::OK_BG};
    if (s.state == "running" || s.state == "message")
        chip = {"RUNNING", th::ACCENT, th::ACCENT_BG};
    else if (s.state == "paused")
        chip = {"PAUSED", th::WARN, th::WARN_BG};
    else if (s.state == "scanning")
        chip = {"AUTO DETECT", th::ACCENT, th::ACCENT_BG};
    else if (s.state == "alarm")
        chip = {"ALARM", th::BAD, th::BAD_BG};
    else if (s.busy)
        chip = {"MOVING", th::ACCENT, th::ACCENT_BG};
    x += g.pill(chip.t, x, y, chip.c, chip.bg) + 6;
    if (j.active())
    {
        char t[16];
        snprintf(t, sizeof(t), "JOB %d%%", j.percent);
        x += g.pill(t, x, y, th::ACCENT, th::ACCENT_BG) + 6;
    }
}

void HomeScreen::draw(Gfx &g)
{
    const MachineStatus s = machine.status();
    const JobStatus j = machine.job();
    const WifiInfo w = wifiSetup.info();
    g.background();
    g.titleBar("Pick & Place", false, w.ip.c_str());
    const bool homed = s.homed && s.e1Homed;
    char v[4][16];
    snprintf(v[0], 16, "%.2f", s.x);
    snprintf(v[1], 16, "%.2f", s.y);
    snprintf(v[2], 16, "%.2f", s.z);
    snprintf(v[3], 16, "%.1f", s.e1);
    static const char *AX[] = {"X", "Y", "Z", "E1"};
    int tileY;
    if (homed)
    {
        int x = 8;
        stateChip(g, x, 32, s, j);
        x += g.pill("HOMED", x, 32, th::ACCENT, th::ACCENT_BG) + 6;
        char t[16];
        snprintf(t, sizeof(t), !s.pumpUsed ? "NO PUMP" : s.pump ? "PUMP %d%%" : "PUMP OFF", s.pump);
        x += g.pill(t, x, 32, s.pump ? th::ACCENT : th::DIM, s.pump ? th::ACCENT_BG : th::PANEL2) + 6;
        for (int i = 0; i < 4; i++)
            g.valueBox(ID_NONE, AX[i], v[i], 8 + i * 77, 54, 74, 40);
        tileY = 102;
    }
    else
    {
        g.rrect(8, 30, 304, 30, 8, th::WARN_BG);
        g.icon(Icon::Warn, 16, 38, th::WARN);
        g.text(s.state == "alarm" ? "Alarm - home again" : "Not homed yet", 36, 45, Font::Body, th::WARN);
        g.button(H_HOME, "Home", 320 - 78, 34, 64, 22, Kind::Primary, Icon::Home, Font::Small);
        for (int i = 0; i < 4; i++)
            g.valueBox(ID_NONE, AX[i], (i == 3 ? s.e1Homed : s.homed) ? v[i] : "?", 8 + i * 77, 66, 74, 34, th::DIM);
        tileY = 104;
    }
    char jobsSub[32] = "on the Pi", feedersSub[32];
    if (j.active())
        snprintf(jobsSub, sizeof(jobsSub), "%s %d%%", j.state == "paused" ? "paused" : "running", j.percent);
    else if (!s.sd)
        snprintf(jobsSub, sizeof(jobsSub), "data folder missing");
    const int nf = machine.feeders().size();
    snprintf(feedersSub, sizeof(feedersSub), "%d feeder%s", nf, nf == 1 ? "" : "s");
    const int h1 = homed ? 62 : 60;
    g.tile(H_JOBS, j.active() ? "Job" : "Jobs", jobsSub, Icon::Jobs, th::ACCENT, th::ACCENT_BG, 8, tileY, 148, h1);
    g.tile(H_FEEDERS, "Feeders", feedersSub, Icon::Feeders, th::PASTE, th::PASTE_BG, 164, tileY, 148, h1);
    g.tile(H_MOVE, "Move", "Jog, home", Icon::Move, th::VIOLET, th::VIOLET_BG, 8, 170, 148, 62);
    g.tile(H_MENU, "Menu", "Settings, WiFi ...", Icon::Menu, th::DIM, th::PANEL2, 164, 170, 148, 62);
}

void HomeScreen::action(uint16_t id)
{
    switch (id)
    {
    case H_HOME:
        homingScreen.start();
        break;
    case H_JOBS:
        if (machine.job().active())
            ui.push(&jobScreen);
        else
            jobsScreen.open("/");
        break;
    case H_FEEDERS:
        ui.push(&feedersScreen);
        break;
    case H_MOVE:
        ui.push(&moveScreen);
        break;
    case H_MENU:
        ui.push(&menuScreen);
        break;
    }
}

// ---------------------------------------------------------------- menu

enum
{
    M_ROWS = 1
};

void MenuScreen::draw(Gfx &g)
{
    const WifiInfo w = wifiSetup.info();
    g.background();
    g.titleBar("Menu");
    std::vector<Row> rows(9);
    rows[0].icon = Icon::Home, rows[0].label = "Home all", rows[0].sub = "X + Y, then Z, then E1";
    rows[1].icon = Icon::Sensor, rows[1].label = "Cameras", rows[1].sub = "live picture, measure, lights", rows[1].chev = true;
    rows[2].icon = Icon::Check, rows[2].label = "Nozzles", rows[2].sub = "rack, change, names", rows[2].chev = true;
    rows[3].icon = Icon::Gear, rows[3].label = "Calibration", rows[3].sub = "cameras, vacuum, joystick", rows[3].chev = true;
    rows[4].icon = Icon::Pump, rows[4].label = "Pump, valve & fans", rows[4].chev = true;
    rows[5].icon = Icon::Test, rows[5].label = "Testing", rows[5].sub = "Outputs, sensors, motors", rows[5].chev = true;
    rows[6].icon = Icon::Gear, rows[6].label = "Settings", rows[6].chev = true;
    rows[7].icon = Icon::Wifi, rows[7].label = "Network & website", rows[7].chev = true;
    rows[7].value = w.state == "connected" ? "connected" : w.ssid.empty() ? "not set up" : w.state;
    rows[7].valueColor = w.state == "connected" ? th::OK : th::WARN;
    rows[8].icon = Icon::Power, rows[8].label = "Motors off", rows[8].sub = "releases all motors (unhomes)";
    list_.draw(g, M_ROWS, rows);
}

void MenuScreen::action(uint16_t id)
{
    switch (id - M_ROWS)
    {
    case 0:
        homingScreen.start();
        break;
    case 1:
        camerasScreen.open(1);
        break;
    case 2:
        ui.push(&nozzlesScreen);
        break;
    case 3:
        ui.push(&calibrationScreen);
        break;
    case 4:
        ui.push(&pumpScreen);
        break;
    case 5:
        ui.push(&testingScreen);
        break;
    case 6:
        ui.push(&settingsScreen);
        break;
    case 7:
        ui.push(&wifiScreen);
        break;
    case 8:
        ui.send("MOTORS OFF", [](const Reply &r) {
            if (r.ok)
                ui.toast("Motors released");
        });
        break;
    }
}

// ---------------------------------------------------------------- homing

void HomingScreen::start(bool e1Only, std::function<void()> then)
{
    done_ = failed_ = closePending_ = false;
    error_.clear();
    e1Only_ = e1Only;
    then_ = then;
    started_ = millis();
    ui.push(this);
    ui.send(e1Only ? "HOME E1" : "HOME", [this](const Reply &r) {
        done_ = true;
        failed_ = !r.ok;
        error_ = r.error;
        if (r.ok)
        {
            closePending_ = true;
            if (ui.top() == this)
                close();
            // else another screen (e.g. offline) covers it: tick() closes it once it is on top again
        }
    }, false);
}

void HomingScreen::close()
{
    closePending_ = false;
    ui.pop();
    ui.toast("Homed", th::OK);
    if (then_)
        then_();
}

void HomingScreen::tick()
{
    if (closePending_ && ui.top() == this)
        close();
}

uint8_t HomingScreen::animRects(Rect *r) const
{
    if (done_)
        return 0;
    r[0] = {24, 44, 30, 134}; // spinners
    r[1] = {0, 186, 320, 20}; // seconds
    return 2;
}

void HomingScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Home all", done_);
    static const char *STEPS[] = {"X + Y", "Z", "E1 carriage"};
    const int first = e1Only_ ? 2 : 0;
    // The board homes Z, then X + Y, then E1: from its message, which step failed
    int failRow = 0;
    if (error_.find("seesaw") != std::string::npos || error_.find("Z ") != std::string::npos)
        failRow = 1;
    else if (error_.find("gantry") != std::string::npos || error_.find("e1_home") != std::string::npos)
        failRow = 2;
    static const int ORDER[3] = {1, 0, 2}; // position of each row in the board's order
    for (int i = first; i < 3; i++)
    {
        const int y = 44 + (i - first) * 44;
        g.card(16, y, 288, 36);
        const bool bad = failed_ && i == failRow, ok = failed_ && ORDER[i] < ORDER[failRow];
        if (bad)
            g.icon(Icon::Warn, 29, y + 11, th::BAD);
        else if (ok)
            g.icon(Icon::Check, 29, y + 11, th::OK);
        else if (!failed_)
            spinner(g, 36, y + 18, 8, th::ACCENT);
        g.text(STEPS[i], 54, y + 18, Font::Title, failed_ && !bad && !ok ? th::DIM : th::TEXT);
        const char *state = !failed_ ? "homing ..." : bad ? "failed" : ok ? "done" : "not started";
        g.text(state, 304 - 12, y + 18, Font::Body, bad ? th::BAD : ok ? th::OK : failed_ ? th::DIM : th::ACCENT, Align::Right);
    }
    if (failed_)
    {
        // The board's reason, word-wrapped onto two lines
        std::string msg = error_.empty() ? "homing failed" : error_;
        if (msg.compare(0, 15, "homing failed: ") == 0)
            msg = msg.substr(15);
        std::string lines[2];
        size_t pos = 0;
        for (int l = 0; l < 2 && pos < msg.size(); l++)
        {
            size_t best = msg.size();
            if (l == 0 && g.width(msg.c_str(), Font::Small) > 300)
            {
                best = std::string::npos;
                for (size_t sp = msg.find(' '); sp != std::string::npos; sp = msg.find(' ', sp + 1))
                {
                    if (g.width(msg.substr(0, sp).c_str(), Font::Small) > 300)
                        break;
                    best = sp;
                }
                if (best == std::string::npos)
                    best = msg.size();
            }
            lines[l] = g.fit(msg.substr(pos, best - pos), Font::Small, 300);
            pos = best + 1;
        }
        g.text(lines[0], 160, 184, Font::Small, th::BAD, Align::Center);
        g.text(lines[1], 160, 200, Font::Small, th::BAD, Align::Center);
        g.hint("tap back or hold the button");
    }
    else
    {
        char t[64];
        snprintf(t, sizeof(t), "%us · can't be cancelled - the switches stop it", (unsigned)((millis() - started_) / 1000));
        g.text(t, 160, 196, Font::Small, th::DIM, Align::Center);
    }
}

// ---------------------------------------------------------------- alarm

void AlarmScreen::tick()
{
    if (machine.status().state != "alarm" && ui.top() == this)
        ui.pop();
}

void AlarmScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Pick & Place", false, "ALARM", th::BAD);
    g.rrect(8, 32, 304, 120, 10, th::BAD_BG);
    g.rframe(8, 32, 304, 120, 10, th::BAD);
    g.icon(Icon::Warn, 20, 44, th::BAD);
    g.text("Motion alarm", 42, 51, Font::Big, th::BAD);
    g.text("Moves are refused until it is cleared.", 20, 78, Font::Body, th::TEXT);
    g.text("The machine must be homed again.", 20, 96, Font::Body, th::DIM);
    g.text(machine.lastAlarm(), 20, 122, Font::Small, th::DIM);
    g.button(1, "Clear alarm", 8, 164, 148, 40, Kind::Primary);
    g.button(2, "Home all", 164, 164, 148, 40, Kind::Normal, Icon::Home);
}

void AlarmScreen::action(uint16_t id)
{
    if (id == 1)
        ui.send("CLEAR");
    else if (id == 2)
        ui.send("CLEAR", [](const Reply &r) {
            if (r.ok)
            {
                ui.goHome();
                homingScreen.start();
            }
        });
}
