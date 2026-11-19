// WiFi & website, more, WPS
#include <cmath>
#include "../config.h"
#include "../net/wifi_setup.h"
#include "screens.h"

WifiScreen wifiScreen;
WifiMoreScreen wifiMoreScreen;
WpsScreen wpsScreen;
WifiListScreen wifiListScreen;

static const uint32_t WPS_SECONDS = 120;

static void qr(Gfx &g, const std::string &text, int x, int y, int size)
{
    g.rrect(x - 4, y - 4, size + 8, size + 8, 4, th::WHITE);
    g.qrcode(text.c_str(), x, y, size);
}

// ---------------------------------------------------------------- WiFi

enum
{
    W_NETS = 1,
    W_WPS,
    W_MORE
};

void WifiScreen::draw(Gfx &g)
{
    const WifiInfo w = wifiSetup.info();
    g.background();
    g.titleBar("WiFi & website");
    if (w.state == "connected")
    {
        g.card(8, 32, 196, 132);
        g.pill("CONNECTED", 18, 42, th::OK, th::OK_BG);
        char t[64];
        snprintf(t, sizeof(t), "%s · %d dBm", w.ssid.c_str(), w.rssi);
        g.text(t, 18, 72, Font::Body, th::TEXT);
        g.text("http://" + w.ip, 18, 92, Font::Body, th::ACCENT);
        g.text("http://" MDNS_NAME ".local", 18, 110, Font::Small, th::DIM);
        g.text("Website password", 18, 134, Font::Small, th::DIM);
        g.text(wifiSetup.webPassword(), 18, 151, Font::Big, th::TEXT);
        g.card(212, 32, 100, 132);
        qr(g, "http://" + w.ip, 222, 46, 80);
        g.text("scan to open", 262, 148, Font::Small, th::DIM, Align::Center);
    }
    else
    {
        g.card(8, 32, 304, 132);
        if (w.ssid.empty() && w.pending.empty() && w.state != "wps")
            g.pill("NOT CONNECTED", 18, 42, th::WARN, th::WARN_BG);
        else
            g.pill(w.state == "wps" ? "WPS RUNNING" : "CONNECTING", 18, 42, th::ACCENT, th::ACCENT_BG);
        g.text(w.ssid.empty() ? "No network saved." : w.ssid, 18, 72, Font::BodyBold, th::TEXT);
        g.text(w.ssid.empty() ? "Tap Networks to choose one." : w.message, 18, 92, Font::Small, th::DIM);
        g.text("Website password: " + wifiSetup.webPassword(), 18, 134, Font::Small, th::DIM);
    }
    g.button(W_NETS, "Networks", 8, 174, 112, 38, Kind::Primary, Icon::Wifi);
    g.button(W_WPS, "WPS", 126, 174, 88, 38);
    g.button(W_MORE, "More ...", 220, 174, 92, 38);
}

void WifiScreen::action(uint16_t id)
{
    if (id == W_NETS)
        ui.push(&wifiListScreen);
    else if (id == W_WPS)
        wpsScreen.start();
    else if (id == W_MORE)
        ui.push(&wifiMoreScreen);
}

// ---------------------------------------------------------------- network list

enum
{
    WL_ROWS = 10 // networks, then "Other network", "WPS"
};

void WifiListScreen::enter()
{
    list_.reset();
    wifiSetup.startScan();
    scanning_ = true;
    pending_.clear();
}

void WifiListScreen::tick()
{
    const WifiInfo w = wifiSetup.info();
    if (scanning_ && !wifiSetup.scanning())
    {
        scanning_ = false;
        ui.redraw(); // the list changed
    }
    // Result of a network chosen here
    if (!pending_.empty() && w.pending.empty())
    {
        if (w.state == "connected" && w.ssid == pending_)
            ui.toast("Connected to " + pending_);
        else
            ui.toast("Couldn't connect to " + pending_, th::BAD);
        pending_.clear();
        ui.redraw();
    }
    const std::string state = w.state + "/" + w.ssid + "/" + w.pending;
    if (state != state_)
    {
        state_ = state; // card text and CONNECTED pill change
        ui.redraw();
    }
}

void WifiListScreen::join(const std::string &ssid, bool secure)
{
    auto go = [ssid](const std::string &pass) {
        wifiSetup.connectTo(ssid, pass);
        wifiListScreen.pending_ = ssid;
    };
    if (secure)
        textInput.open("Password · " + ssid, "", true, 63, go);
    else
        go("");
}

void WifiListScreen::draw(Gfx &g)
{
    const WifiInfo w = wifiSetup.info();
    const auto &nets = wifiSetup.networks();
    g.background();
    g.titleBar("WiFi", true, nullptr, th::DIM, Icon::Refresh);

    // Connection state
    g.rrect(8, 29, 304, 32, 8, th::PANEL);
    if (!w.pending.empty())
    {
        const float a = fmodf(millis() * 0.36f, 360);
        g.arc(24, 45, 5, 7, a, a + 270, th::ACCENT);
        g.text("Connecting to " + w.pending + " ...", 38, 45, Font::Body, th::TEXT);
    }
    else if (w.state == "connected")
    {
        g.icon(Icon::Wifi, 17, 38, th::OK, rssiBars(w.rssi));
        g.text(w.ssid, 38, 45, Font::BodyBold, th::TEXT);
        g.text(w.ip, 302, 45, Font::Body, th::ACCENT, Align::Right);
    }
    else
    {
        g.icon(Icon::Wifi, 17, 38, th::DIM, 0);
        g.text(w.ssid.empty() ? "Not connected - choose a network" : w.message, 38, 45, Font::Small, th::DIM);
    }

    std::vector<Row> rows;
    for (const auto &n : nets)
    {
        Row r;
        r.label = n.ssid;
        r.icon = Icon::Wifi, r.iconArg = rssiBars(n.rssi);
        char sub[32];
        snprintf(sub, sizeof(sub), "%s · %d dBm", n.secure ? "secured" : "open", n.rssi);
        r.sub = sub;
        if (w.state == "connected" && n.ssid == w.ssid)
        {
            r.pill = "CONNECTED", r.pillColor = th::OK, r.pillBg = th::OK_BG;
            r.iconColor = th::OK;
        }
        rows.push_back(r);
    }
    Row other;
    other.label = "Other network ...", other.sub = "hidden: type its name", other.icon = Icon::Plus;
    rows.push_back(other);
    Row wps;
    wps.label = "WPS", wps.sub = "press the router's WPS button", wps.icon = Icon::Wifi, wps.chev = true;
    rows.push_back(wps);
    list_.draw(g, WL_ROWS, rows, 66, 34);
    if (nets.empty())
    {
        const char *t = scanning_ ? "Searching for networks ..."
                        : wifiSetup.scanFailed() ? "Scan failed - tap refresh"
                                                 : "No networks found";
        g.text(t, 160, 66 + 2 * 34 + 20, Font::Small, th::DIM, Align::Center);
    }
}

void WifiListScreen::action(uint16_t id)
{
    if (id == ID_TITLE_ACTION)
    {
        wifiSetup.startScan();
        scanning_ = true;
        return;
    }
    const auto &nets = wifiSetup.networks();
    const int i = (int)id - WL_ROWS;
    if (i < 0)
        return;
    if (i < (int)nets.size())
        join(nets[i].ssid, nets[i].secure);
    else if (i == (int)nets.size())
        textInput.open("Network name", "", false, 32, [](const std::string &ssid) {
            if (!ssid.empty())
                wifiListScreen.join(ssid, true); // leave the password empty for an open network
        });
    else if (i == (int)nets.size() + 1)
        wpsScreen.start();
}

// ---------------------------------------------------------------- more

enum
{
    WM_ROWS = 10
};

void WifiMoreScreen::draw(Gfx &g)
{
    const WifiInfo w = wifiSetup.info();
    g.background();
    g.titleBar("WiFi: more");
    std::vector<Row> rows(4);
    rows[0].label = "Network", rows[0].value = w.ssid.empty() ? "none saved" : w.ssid;
    rows[1].label = "Forget network", rows[1].icon = Icon::Trash, rows[1].sub = "offline until a new one is chosen";
    rows[2].label = "New website password", rows[2].icon = Icon::Refresh, rows[2].sub = "logs every browser out";
    rows[2].value = wifiSetup.webPassword();
    rows[0].chev = true;
    rows[3].label = "Restart the Pi", rows[3].icon = Icon::Power;
    list_.draw(g, WM_ROWS, rows, 25, 38);
}

void WifiMoreScreen::action(uint16_t id)
{
    switch (id - WM_ROWS)
    {
    case 0:
        ui.push(&wifiListScreen);
        break;
    case 1:
        dialog.open("Forget the network?", "The website is offline until a", "network is chosen: Settings -> WiFi.",
                    {{"No", Kind::Normal, nullptr}, {"Forget", Kind::Danger, [] { wifiSetup.forget(); }}}, Icon::Wifi, th::WARN, 0);
        break;
    case 2:
        dialog.open("New website password?", "Browsers have to log in again", "with the new password.",
                    {{"No", Kind::Normal, nullptr},
                     {"New password", Kind::Primary, [] { ui.toast("Password: " + wifiSetup.newWebPassword()); }}},
                    Icon::Refresh, th::ACCENT, 1);
        break;
    case 3:
        dialog.open("Restart the Pi?", "A running job is stopped;", "the machine has to home again.",
                    {{"No", Kind::Normal, nullptr}, {"Restart", Kind::Primary, [] { (void)!system("sudo -n systemctl reboot"); }}},
                    Icon::Power, th::ACCENT, 0);
        break;
    }
}

// ---------------------------------------------------------------- WPS

void WpsScreen::start()
{
    started_ = millis();
    wifiSetup.startWps();
    ui.push(this);
}

void WpsScreen::tick()
{
    const WifiInfo w = wifiSetup.info();
    if (w.state != state_)
    {
        state_ = w.state; // connecting / failed change the text: full redraw
        ui.redraw();
    }
    if (w.state == "connected" && ui.top() == this)
    {
        ui.pop();
        ui.toast("Connected to " + w.ssid);
    }
}

void WpsScreen::draw(Gfx &g)
{
    const WifiInfo w = wifiSetup.info();
    const bool failed = w.state == "wps_failed";
    const uint32_t elapsed = (millis() - started_) / 1000;
    g.background();
    g.titleBar("WPS", false);
    g.disc(160, 68, 30, failed ? th::BAD_BG : th::ACCENT_BG);
    g.icon(failed ? Icon::Warn : Icon::Wifi, 153, 60, failed ? th::BAD : th::ACCENT, 3);
    if (failed)
    {
        g.text("WPS didn't work", 160, 118, Font::Big, th::TEXT, Align::Center);
        g.text(w.message, 160, 138, Font::Body, th::DIM, Align::Center);
        g.button(1, "OK", 96, 194, 128, 34, Kind::Primary);
        return;
    }
    if (w.state == "connecting")
    {
        g.text("Connecting ...", 160, 118, Font::Big, th::TEXT, Align::Center);
        g.text(w.message, 160, 138, Font::Body, th::DIM, Align::Center);
    }
    else
    {
        g.text("Press the WPS button", 160, 118, Font::Big, th::TEXT, Align::Center);
        g.text("on your router within 2 minutes.", 160, 138, Font::Body, th::DIM, Align::Center);
    }
    const uint32_t left = elapsed < WPS_SECONDS ? WPS_SECONDS - elapsed : 0;
    g.progress(60, 158, 200, 6, (float)left / WPS_SECONDS);
    g.text(fmtTime(left) + " left", 160, 176, Font::Small, th::DIM, Align::Center);
    g.button(1, "Close", 96, 194, 128, 34);
}

void WpsScreen::action(uint16_t id)
{
    if (id == 1)
        ui.pop();
}
