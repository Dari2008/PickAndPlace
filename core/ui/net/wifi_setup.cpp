#include "wifi_setup.h"
#include <Arduino.h>
#include "../../compat/SdFat.h"

#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

WifiSetup wifiSetup;

// Run a command, return its output (stdout); empty on failure
static std::string run(const std::string &cmd, int *status = nullptr)
{
    std::string out;
    FILE *p = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!p)
    {
        if (status)
            *status = -1;
        return out;
    }
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0)
        out.append(buf, n);
    const int st = pclose(p);
    if (status)
        *status = st;
    return out;
}

// Single-quote for the shell
static std::string q(const std::string &s)
{
    std::string r = "'";
    for (char c : s)
        r += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return r + "'";
}

// nmcli -t output: fields separated by ':', '\:' inside a field
static std::vector<std::string> fields(const std::string &line)
{
    std::vector<std::string> f(1);
    for (size_t i = 0; i < line.size(); i++)
    {
        if (line[i] == '\\' && i + 1 < line.size())
            f.back() += line[++i];
        else if (line[i] == ':')
            f.emplace_back();
        else
            f.back() += line[i];
    }
    return f;
}

void WifiSetup::begin(bool simulate)
{
    sim_ = simulate;
    loadWeb();
    poll();
}

void WifiSetup::update()
{
    if (millis() - lastPoll_ > 5000 && !busy_)
    {
        lastPoll_ = millis();
        busy_ = true;
        std::thread([this] {
            poll();
            busy_ = false;
        }).detach();
    }
}

void WifiSetup::poll()
{
    WifiInfo w;
    if (sim_)
    {
        w.state = "connected", w.ssid = "Simulation", w.ip = "127.0.0.1", w.rssi = -50;
    }
    else
    {
        // The address the website is reachable at: first non-loopback IPv4
        std::istringstream ips(run("hostname -I"));
        ips >> w.ip;
        std::istringstream dev(run("nmcli -t -f TYPE,STATE,CONNECTION device"));
        std::string line;
        bool wired = false;
        while (std::getline(dev, line))
        {
            auto f = fields(line);
            if (f.size() < 3)
                continue;
            if (f[0] == "wifi" && f[1] == "connected")
                w.ssid = f[2];
            if (f[0] == "ethernet" && f[1] == "connected")
                wired = true;
            if (f[0] == "wifi" && f[1].rfind("connecting", 0) == 0)
                w.state = "connecting";
        }
        if (!w.ssid.empty())
        {
            std::istringstream ap(run("nmcli -t -f ACTIVE,SIGNAL device wifi list --rescan no"));
            while (std::getline(ap, line))
            {
                auto f = fields(line);
                if (f.size() >= 2 && f[0] == "yes")
                    w.rssi = -100 + atoi(f[1].c_str()) / 2; // nmcli: 0..100 %
            }
        }
        else if (wired)
            w.ssid = "Ethernet", w.rssi = -40;
        if (w.state.empty())
            w.state = w.ip.empty() ? "off" : "connected";
    }
    std::lock_guard<std::mutex> l(m_);
    w.message = info_.message;
    w.pending = info_.pending;
    if (!w.pending.empty() && w.ssid == w.pending)
        w.pending.clear();
    info_ = w;
}

WifiInfo WifiSetup::info()
{
    std::lock_guard<std::mutex> l(m_);
    return info_;
}

void WifiSetup::startScan()
{
    if (scanning_)
        return;
    scanning_ = true;
    scanFailed_ = false;
    std::thread([this] {
        std::vector<WifiNet> list;
        int st = 0;
        if (sim_)
            list = {{"Workshop", -48, true}, {"Guest", -71, false}};
        else
        {
            std::istringstream s(run("nmcli -t -f SSID,SIGNAL,SECURITY device wifi list --rescan yes", &st));
            std::string line;
            while (std::getline(s, line))
            {
                auto f = fields(line);
                if (f.size() < 3 || f[0].empty())
                    continue;
                bool dup = false;
                for (auto &n : list)
                    dup |= n.ssid == f[0];
                if (!dup)
                    list.push_back({f[0], -100 + atoi(f[1].c_str()) / 2, !f[2].empty() && f[2] != "--"});
            }
        }
        {
            std::lock_guard<std::mutex> l(m_);
            found_ = list;
        }
        scanFailed_ = st != 0;
        scanning_ = false;
    }).detach();
}

const std::vector<WifiNet> &WifiSetup::networks()
{
    std::lock_guard<std::mutex> l(m_);
    networks_ = found_;
    return networks_;
}

void WifiSetup::connectTo(const std::string &ssid, const std::string &pass)
{
    {
        std::lock_guard<std::mutex> l(m_);
        info_.pending = ssid;
        info_.message = "Connecting to " + ssid + " ...";
    }
    std::thread([this, ssid, pass] {
        int st = 0;
        if (!sim_)
            run("nmcli device wifi connect " + q(ssid) + (pass.empty() ? "" : " password " + q(pass)), &st);
        std::lock_guard<std::mutex> l(m_);
        info_.message = st == 0 ? "Connected to " + ssid : "Could not connect to " + ssid;
        info_.pending.clear();
        lastPoll_ = 0;
    }).detach();
}

void WifiSetup::forget()
{
    const std::string ssid = info().ssid;
    if (ssid.empty() || ssid == "Ethernet" || sim_)
        return;
    std::thread([this, ssid] {
        run("nmcli connection delete id " + q(ssid));
        lastPoll_ = 0;
    }).detach();
}

void WifiSetup::startWps()
{
    std::lock_guard<std::mutex> l(m_);
    info_.message = "WPS is not available on the Pi: choose a network instead";
}

// ---------------------------------------------------------------- website password

void WifiSetup::loadWeb()
{
    // Read every time: pap-web changes the password in the same file
    webLoaded_ = true;
    webPass_.clear();
    secret_.clear();
    std::ifstream f(sdfs_path("/web.txt"));
    std::string line;
    while (std::getline(f, line))
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto trim = [](std::string &s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r'))
                s.pop_back();
            while (!s.empty() && s[0] == ' ')
                s.erase(0, 1);
        };
        trim(k), trim(v);
        if (k == "password")
            webPass_ = v;
        else if (k == "secret")
            secret_ = v;
    }
    if (webPass_.empty() || secret_.empty())
    {
        std::random_device rd;
        if (secret_.empty())
            for (int i = 0; i < 32; i++)
                secret_ += "0123456789abcdef"[rd() % 16];
        if (webPass_.empty())
            for (int i = 0; i < 8; i++)
                webPass_ += (char)('a' + rd() % 26);
        saveWeb();
    }
}

void WifiSetup::saveWeb()
{
    const std::string path = sdfs_path("/web.txt");
    {
        std::ofstream f(path + ".tmp");
        f << "# website login (pap-web reads this file)\npassword = " << webPass_ << "\nsecret = " << secret_ << "\n";
    }
    std::rename((path + ".tmp").c_str(), path.c_str());
}

std::string WifiSetup::webPassword()
{
    std::lock_guard<std::mutex> l(m_);
    loadWeb();
    return webPass_;
}

void WifiSetup::setWebPassword(const std::string &pass)
{
    std::lock_guard<std::mutex> l(m_);
    loadWeb();
    webPass_ = pass;
    // A new secret logs every browser out
    std::random_device rd;
    secret_.clear();
    for (int i = 0; i < 32; i++)
        secret_ += "0123456789abcdef"[rd() % 16];
    saveWeb();
}

std::string WifiSetup::newWebPassword()
{
    std::random_device rd;
    std::string p;
    for (int i = 0; i < 8; i++)
        p += (char)('a' + rd() % 26);
    setWebPassword(p);
    return p;
}

std::string WifiSetup::secret()
{
    std::lock_guard<std::mutex> l(m_);
    loadWeb();
    return secret_;
}
