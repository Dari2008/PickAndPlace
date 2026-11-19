// Settings: search, the catalog groups (screens_settings_cat.cpp), display / touch / network / data
#include <cstdlib>
#include <utility>
#include <cstring>
#include "../hw/backlight.h"
#include "../hw/display.h"
#include "../hw/touch.h"
#include "../net/wifi_setup.h"
#include "screens.h"

SettingsScreen settingsScreen;
SdScreen sdScreen;
DisplaySettingsScreen displaySettingsScreen;

// ---------------------------------------------------------------- settings

enum
{
    ST_ROWS = 10
};

// Groups in the catalog's order, with how many settings each has
static std::vector<std::pair<std::string, int>> catalogGroups()
{
    std::vector<std::pair<std::string, int>> out;
    for (auto &e : machine.catalog())
    {
        if (out.empty() || out.back().first != e.group)
        {
            bool known = false;
            for (auto &g : out)
                if (g.first == e.group)
                    known = true, g.second++;
            if (known)
                continue;
            out.push_back({e.group, 1});
            continue;
        }
        out.back().second++;
    }
    return out;
}

static Icon groupIcon(const std::string &g)
{
    auto has = [&](const char *w) { return g.find(w) != std::string::npos; };
    if (has("Axis"))
        return Icon::Motor;
    if (has("Job"))
        return Icon::Jobs;
    if (has("Vacuum") || has("Paste"))
        return Icon::Pump;
    if (has("Nozzle"))
        return Icon::Check;
    if (has("Feeder"))
        return Icon::Feeders;
    if (has("camera") || has("Vision") || has("Load cell"))
        return Icon::Sensor;
    if (has("Homing"))
        return Icon::Home;
    if (has("Machine") || has("Joystick"))
        return Icon::Move;
    if (has("Safety"))
        return Icon::Warn;
    return Icon::Gear;
}

void SettingsScreen::draw(Gfx &g)
{
    const MachineStatus s = machine.status();
    const WifiInfo w = wifiSetup.info();
    g.background();
    g.titleBar("Settings");
    std::vector<Row> rows;
    Row search;
    search.icon = Icon::Menu, search.iconColor = th::ACCENT, search.label = "Search settings";
    search.sub = "by name, key or description", search.chev = true;
    rows.push_back(search);
    const auto groups = catalogGroups();
    for (auto &gr : groups)
    {
        Row r;
        r.icon = groupIcon(gr.first);
        r.label = gr.first;
        r.value = std::to_string(gr.second);
        r.chev = true;
        rows.push_back(r);
    }
    if (groups.empty())
    {
        Row r;
        r.icon = Icon::Warn, r.label = "Loading the settings ...";
        rows.push_back(r);
    }
    Row disp, tch, net, sd, octoSd, reload;
    disp.icon = Icon::Gear, disp.label = "Display", disp.chev = true;
    disp.sub = std::string(display.rotation() == 3 ? "turned 180, " : "turn 180, ") +
               (backlight.offMinutes() ? "off after " + std::to_string(backlight.offMinutes()) + " min" : "always on");
    tch.icon = Icon::Touch, tch.label = "Touch", tch.chev = true;
    tch.sub = touch.options().enabled ? "calibrate, test, sensitivity" : "off (joystick only)";
    net.icon = Icon::Wifi, net.label = "Network & website", net.chev = true;
    net.iconArg = w.state == "connected" ? rssiBars(w.rssi) : 0;
    net.sub = w.state == "connected" ? "connected to " + w.ssid : w.ssid.empty() ? "not set up" : "connecting to " + w.ssid;
    sd.icon = Icon::Sd, sd.label = "Data folder", sd.sub = s.sd ? "jobs, feeders, settings" : "not available", sd.chev = true;
    octoSd.icon = Icon::Sd, octoSd.label = "Motion board SD card", octoSd.sub = "copy jobs from the Octopus card to the Pi";
    octoSd.chev = true;
    reload.icon = Icon::Refresh, reload.label = "Reload settings", reload.sub = "after editing the files on a PC";
    rows.push_back(disp);
    rows.push_back(tch);
    rows.push_back(net);
    rows.push_back(sd);
    rows.push_back(octoSd);
    rows.push_back(reload);
    list_.draw(g, ST_ROWS, rows, 25, 35);
}

void SettingsScreen::action(uint16_t id)
{
    const int i = id - ST_ROWS;
    if (i == 0)
    {
        textInput.open("Search settings", "", false, 30, [](const std::string &t) {
            if (!t.empty())
                ui.post([t] { settingsGroupScreen.openSearch(t); });
        });
        return;
    }
    const auto groups = catalogGroups();
    const int n = groups.empty() ? 1 : (int)groups.size();
    if (i >= 1 && i <= n)
    {
        if (!groups.empty())
            settingsGroupScreen.openGroup(groups[i - 1].first);
        return;
    }
    switch (i - 1 - n)
    {
    case 0:
        ui.push(&displaySettingsScreen);
        break;
    case 1:
        ui.push(&touchSettingsScreen);
        break;
    case 2:
        ui.push(&wifiScreen);
        break;
    case 3:
        ui.push(&sdScreen);
        break;
    case 4:
        ui.push(&octoSdScreen);
        break;
    case 5:
        ui.send("RELOAD", [](const Reply &r) {
            if (!r.ok)
                return;
            machine.refreshSettings();
            machine.refreshFeeders();
            ui.toast("Reloaded");
        });
        break;
    }
}

// ---------------------------------------------------------------- SD card

void SdScreen::enter()
{
    loading_ = true;
    error_.clear();
    ui.send("SD", [this](const Reply &r) {
        loading_ = false;
        if (!r.ok || r.records.empty())
        {
            error_ = r.ok ? "no answer" : r.error;
            return;
        }
        type_ = r.records[0].str("type");
        const long mb = r.records[0].integer("size_mb");
        char t[24];
        snprintf(t, sizeof(t), "%.1f GB", mb / 1024.0f);
        size_ = t;
    }, false);
}

void SdScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("SD card");
    g.card(8, 32, 304, 112);
    if (loading_)
        g.text("Reading ...", 160, 88, Font::Body, th::DIM, Align::Center);
    else if (!error_.empty())
    {
        g.icon(Icon::Sd, 20, 44, th::WARN);
        g.text("No SD card", 42, 51, Font::Title, th::WARN);
        g.text(error_, 20, 78, Font::Small, th::DIM);
        g.text("The data folder (~/pap-data) is not readable.", 20, 96, Font::Small, th::DIM);
    }
    else
    {
        g.icon(Icon::Sd, 20, 44, th::OK);
        g.text("Card mounted", 42, 51, Font::Title, th::TEXT);
        g.text(type_ + " · " + size_, 20, 76, Font::Body, th::TEXT);
        const int n = machine.feeders().size();
        g.text(std::to_string(n) + " feeder files in /feeders", 20, 96, Font::Body, th::DIM);
        g.text("/settings.txt · jobs anywhere on the card", 20, 114, Font::Small, th::DIM);
    }
    g.button(1, "Mount again", 8, 154, 148, 38, Kind::Normal, Icon::Refresh);
    g.button(2, "Reload files", 164, 154, 148, 38, Kind::Primary);
}

void SdScreen::action(uint16_t id)
{
    if (id == 1)
        ui.send("SD MOUNT", [this](const Reply &) { enter(); });
    else if (id == 2)
        ui.send("RELOAD", [](const Reply &r) {
            if (!r.ok)
                return;
            machine.refreshSettings();
            machine.refreshFeeders();
            ui.toast("Reloaded");
        });
}

// ---------------------------------------------------------------- display (backlight)

enum
{
    DS_ROWS = 10 // turn 180, brightness, off after, off now
};

static const uint16_t OFF_CHOICES[] = {0, 1, 2, 5, 10, 15, 30, 60}; // minutes, 0 = never

// Picture turned by 180 degrees; the touch calibration is mirrored with it (min <-> max on both
// axes), so taps keep landing where the finger is without calibrating again.
void setScreenTurned(bool on)
{
    if ((display.rotation() == 3) == on)
        return;
    display.setRotation(on ? 3 : 1);
    TouchCal c = touch.cal();
    std::swap(c.xMin, c.xMax);
    std::swap(c.yMin, c.yMax);
    touch.setCal(c);
}

static void turnScreen() { setScreenTurned(display.rotation() != 3); }

void DisplaySettingsScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Display");
    const bool bl = Backlight::available();
    std::vector<Row> rows(bl ? 4 : 1);
    rows[0].icon = Icon::Refresh, rows[0].label = "Turn screen 180 deg", rows[0].toggle = display.rotation() == 3;
    rows[0].sub = "touch turns with it (saved)";
    if (bl)
    {
        rows[1].label = "Brightness", rows[1].value = std::to_string(backlight.brightness()) + " %";
        rows[1].sub = "backlight while on";
        const uint16_t m = backlight.offMinutes();
        rows[2].label = "Screen off after", rows[2].value = m ? std::to_string(m) + " min" : "never";
        rows[2].sub = "without touch or joystick; tap to change";
        rows[3].label = "Screen off now", rows[3].icon = Icon::Power, rows[3].sub = "touch or move the joystick to wake it";
    }
    list_.draw(g, DS_ROWS, rows, 25, 38);
    g.hint(bl ? "The waking touch / stick move does nothing else; alarms wake it too"
              : "Backlight can't be switched: the LED pin is on 3V3 (wire it to TFT_BL)");
}

void DisplaySettingsScreen::action(uint16_t id)
{
    if (id == DS_ROWS)
    {
        turnScreen();
        return;
    }
    if (!Backlight::available())
        return;
    switch (id - DS_ROWS - 1)
    {
    case 0:
    {
        NumberEditor::Spec s;
        s.title = "Brightness";
        s.what = "backlight while the screen is on";
        s.value = backlight.brightness();
        s.unit = "%";
        s.min = 5;
        s.max = 100;
        s.decimals = 0;
        s.steps = {5, 1, 25};
        numberEditor.open(s, [](float v) { backlight.setBrightness((uint8_t)v); });
        break;
    }
    case 1:
    {
        // Next choice: never, 1, 2, 5, 10, 15, 30, 60 min
        const uint16_t m = backlight.offMinutes();
        const size_t n = sizeof(OFF_CHOICES) / sizeof(OFF_CHOICES[0]);
        size_t i = 0;
        while (i < n && OFF_CHOICES[i] <= m)
            i++;
        backlight.setOffMinutes(OFF_CHOICES[i % n]);
        break;
    }
    case 2:
        backlight.sleep();
        break;
    }
}
