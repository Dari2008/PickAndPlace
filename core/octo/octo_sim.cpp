#include "octo_sim.h"
#include "../link/record.h"
#include "../ctrl/nozzles.h"
#include "../ctrl/settings/general_settings.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <strings.h>
#include <thread>
#include <filesystem>
#include <fstream>
#include <map>

namespace
{
    std::vector<std::string> words(const std::string &line)
    {
        std::vector<std::string> w;
        std::istringstream is(line);
        std::string s;
        while (is >> s)
            w.push_back(s);
        return w;
    }
    bool kv(const std::vector<std::string> &w, const char *key, float &v)
    {
        const size_t n = strlen(key);
        for (size_t i = 1; i < w.size(); i++)
            if (!strncasecmp(w[i].c_str(), key, n) && w[i].size() > n && w[i][n] == '=')
            {
                v = strtof(w[i].c_str() + n + 1, nullptr);
                return true;
            }
        return false;
    }
    bool is(const std::vector<std::string> &w, size_t i, const char *s) { return i < w.size() && !strcasecmp(w[i].c_str(), s); }
    float num(const std::vector<std::string> &w, size_t i, float def) { return i < w.size() ? strtof(w[i].c_str(), nullptr) : def; }
}

double SimTransport::now() const
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool SimTransport::open()
{
    out_.clear();
    segs_.clear();
    feeders_.clear();
    const char *env = getenv("PAP_SIM_FEEDERS");
    std::string list = env ? env : "40,110,180";
    std::istringstream is(list);
    std::string item;
    while (std::getline(is, item, ','))
        if (!item.empty())
            feeders_.push_back(strtof(item.c_str(), nullptr));
    out("!ready firmware=pap-motion version=sim board=simulator");
    return true;
}

void SimTransport::advance()
{
    const double t = now();
    while (!segs_.empty() && segs_.front().t1 <= t)
    {
        memcpy(pos_, segs_.front().to, sizeof(pos_));
        segs_.erase(segs_.begin());
    }
}

void SimTransport::position(float *p) const
{
    if (segs_.empty())
    {
        memcpy(p, pos_, sizeof(pos_));
        return;
    }
    const Seg &s = segs_.front();
    const double t = now();
    const double f = t <= s.t0 ? 0.0 : (t >= s.t1 ? 1.0 : (t - s.t0) / (s.t1 - s.t0));
    for (int a = 0; a < OCTO_AXES; a++)
        p[a] = (float)(s.from[a] + (s.to[a] - s.from[a]) * f);
}

void SimTransport::target(float *t) const
{
    if (segs_.empty())
        memcpy(t, pos_, sizeof(pos_));
    else
        memcpy(t, segs_.back().to, sizeof(pos_));
}

bool SimTransport::busy() const { return !segs_.empty() || homeDoneAt_ > 0 || probeDoneAt_ > 0; }

void SimTransport::queueMove(const float *to, float speed)
{
    Seg s;
    target(s.from);
    memcpy(s.to, to, sizeof(s.to));
    double d2 = 0;
    for (int a = 0; a < OCTO_AXES; a++)
        d2 += (double)(s.to[a] - s.from[a]) * (s.to[a] - s.from[a]);
    static const double speedup = getenv("PAP_SIM_SPEED") ? std::max(0.01, atof(getenv("PAP_SIM_SPEED"))) : 1.0;
    const double dur = speed > 0 ? std::sqrt(d2) / speed / speedup : 0;
    s.t0 = segs_.empty() ? now() : segs_.back().t1;
    s.t1 = s.t0 + dur;
    segs_.push_back(s);
    motors_ = true;
}

std::string SimTransport::statusLine(const char *type) const
{
    float p[OCTO_AXES];
    position(p);
    char b[512];
    snprintf(b, sizeof(b),
             "%s state=%s x=%.3f y=%.3f z=%.3f e0=%.2f e1=%.3f rot=%.2f paste=%.3f x2=%.2f homed=%d e1_homed=%d busy=%d "
             "alarm=%d motors=%d pump=%d pump_used=%d valve=%d cam_fan=%d box_fan=%d pi_fan=%d light_top=%d light_bottom=%d in=0",
             type, alarm_ ? "alarm" : (homeDoneAt_ > 0 ? "homing" : (busy() ? "moving" : "idle")), p[OX], p[OY], p[OZ],
             p[OE0], p[OE1], p[OROT], p[OPASTE], p[OS], homed_, e1Homed_, busy(), alarm_, motors_,
             outputs_.count("pump") ? outputs_.at("pump") : 0, pumpUsed() ? 1 : 0, outputs_.count("valve") ? outputs_.at("valve") : 0,
             set_.count("cam_fan") ? atoi(set_.at("cam_fan").c_str()) : 50,
             set_.count("box_fan") ? atoi(set_.at("box_fan").c_str()) : 100,
             set_.count("pi_fan") ? atoi(set_.at("pi_fan").c_str()) : 100,
             outputs_.count("light_bottom") && outputs_.at("light_bottom") > 0
                 ? 0
                 : (outputs_.count("light_top_cmd") ? outputs_.at("light_top_cmd") : 100), // always on, dark with the bottom light
             outputs_.count("light_bottom") ? outputs_.at("light_bottom") : 0);
    return b;
}

bool SimTransport::write(const std::string &line)
{
    advance();
    handle(line);
    return true;
}

void SimTransport::handle(const std::string &line)
{
    const std::vector<std::string> w = words(line);
    if (w.empty() || w[0][0] == '#')
        return;
    const std::string &c = w[0];
    auto cmd = [&](const char *n) { return !strcasecmp(c.c_str(), n); };
    float t[OCTO_AXES], v;
    target(t);

    if (cmd("INFO"))
    {
        out("info firmware=pap-motion version=sim board=simulator axes=8");
        return ok();
    }
    if (cmd("PING") || cmd("LOG"))
        return ok();
    if (cmd("STATUS"))
    {
        out(statusLine("status"));
        return ok();
    }
    if (cmd("REPORT"))
    {
        reportMs_ = (int)num(w, 1, 0);
        return ok();
    }
    if (cmd("WAIT"))
    {
        waitPending_ = true;
        return;
    }
    if (alarm_ && (cmd("MOVE") || cmd("JOG") || cmd("HOME") || cmd("E1") || cmd("E0") || cmd("ROTATE") ||
                   cmd("PASTE") || cmd("MOTOR") || cmd("PROBE")))
        return error("alarm: (CLEAR first)");
    if (cmd("MOVE") || cmd("JOG"))
    {
        const bool rel = cmd("JOG");
        bool any = false;
        for (int a = 0; a < OCTO_AXES; a++)
            if (kv(w, OCTO_AXIS_NAME[a], v))
            {
                t[a] = rel ? t[a] + v : v;
                any = true;
            }
        float speed = 30;
        kv(w, "speed", speed);
        if (!any)
            return error("usage: MOVE x= y= z= ...");
        queueMove(t, speed);
        return ok();
    }
    if (cmd("E1") || cmd("E0") || cmd("ROTATE") || cmd("PASTE"))
    {
        float speed = cmd("E1") ? 30 : 180;
        kv(w, "speed", speed);
        const float a = num(w, 1, 0);
        if (cmd("E1"))
            t[OE1] = a;
        else if (cmd("E0"))
            t[OE0] += a;
        else if (cmd("ROTATE"))
            t[OROT] = a;
        else
        {
            t[OPASTE] += a / 78.54f; // 10 mm syringe
            speed = 0.05f;
        }
        queueMove(t, speed);
        return ok();
    }
    if (cmd("MOTOR"))
        return ok();
    if (cmd("SGTEST"))
    {
        // Made-up StallGuard values in the range of a free-running TMC2209 at homing current
        const std::string ax = w.size() > 1 ? w[1] : "";
        if (ax != "x" && ax != "X" && ax != "y" && ax != "Y" && ax != "e1" && ax != "E1")
            return error("usage: SGTEST <x|y|e1> [mm] [speed=] [cur=]");
        auto rec = [&](const char *name, int mn) {
            char l[160];
            snprintf(l, sizeof(l), "sgtest axis=%s n=120 min=%d avg=%d max=%d suggest=%d cur=60 speed=40.0", name, mn, mn + 60,
                     mn + 130, (int)lroundf(mn * 0.35f));
            out(l);
        };
        const bool isX = ax == "x" || ax == "X";
        rec(isX ? "x" : ax == "y" || ax == "Y" ? "y" : "e1", 180);
        if (isX)
            rec("x2", 170);
        return ok();
    }
    if (cmd("HOME"))
    {
        // Blocks like the firmware: the answer comes after "homing"
        homeDoneAt_ = now() + 0.5;
        homed_ = true;
        if (w.size() == 1 || is(w, 1, "E1") || is(w, 2, "E1") || is(w, 1, "ALL"))
            e1Homed_ = true;
        pos_[OX] = pos_[OY] = 0;
        pos_[OE1] = 0;
        segs_.clear();
        return;
    }
    if (cmd("PROBE"))
    {
        float to = 0, speed = 5;
        kv(w, "speed", speed);
        if (kv(w, "z", to))
        {
            // Load cell: contact where the nozzle tip meets the pad (only over the pad)
            const float reach = settings.headReach[1] + Nozzles::simTipDz(nozzles.mounted());
            const float contact = settings.loadcellHeight - reach;
            const bool over = fabsf(t[OX] - settings.loadcellPos[0]) < 3 && fabsf(t[OY] - settings.loadcellPos[1]) < 3;
            const float from = t[OZ];
            probeAxis_ = OZ;
            probeFound_ = over && settings.loadcell && contact < from && contact >= to;
            // Stops a little past the contact at speed (one sample at 80 /s), like the real cell
            probeHit_ = probeFound_ ? contact - speed * 0.004f : to;
            t[OZ] = probeHit_;
            queueMove(t, speed * 20);
            probeDoneAt_ = segs_.back().t1;
            return;
        }
        kv(w, "e1", to);
        probeAxis_ = OE1;
        const float from = t[OE1];
        probeFound_ = false;
        probeHit_ = to;
        for (float f : feeders_)
            if (f > from + 0.5f && f <= to)
            {
                probeHit_ = f;
                probeFound_ = true;
                break;
            }
        t[OE1] = probeHit_;
        queueMove(t, speed * 20); // the simulation runs 20 x faster than real scanning
        probeDoneAt_ = segs_.back().t1;
        return;
    }
    if (cmd("SETPOS"))
    {
        advance();
        for (int a = 0; a < OCTO_AXES; a++)
            if (kv(w, OCTO_AXIS_NAME[a], v))
                pos_[a] = v;
        return ok();
    }
    if (cmd("POS"))
    {
        float p[OCTO_AXES];
        position(p);
        char b[256];
        snprintf(b, sizeof(b), "pos x=%.3f y=%.3f z=%.3f rot=%.3f paste=%.3f e0=%.3f e1=%.3f x2=%.3f", p[0], p[1], p[2],
                 p[3], p[4], p[5], p[6], p[7]);
        out(b);
        return ok();
    }
    if (cmd("MOTORS"))
    {
        motors_ = is(w, 1, "ON");
        if (!motors_)
            homed_ = e1Homed_ = false;
        return ok();
    }
    if (cmd("HALT") || cmd("ESTOP"))
    {
        float p[OCTO_AXES];
        position(p);
        memcpy(pos_, p, sizeof(pos_));
        segs_.clear();
        probeDoneAt_ = -1;
        if (cmd("ESTOP"))
        {
            homed_ = e1Homed_ = false;
            outputs_["pump"] = outputs_["valve"] = 0;
        }
        return ok();
    }
    if (cmd("CLEAR"))
    {
        alarm_ = 0;
        homed_ = e1Homed_ = false;
        return ok();
    }
    if (cmd("SENSORS"))
    {
        out(std::string("sensors x_min=0 x2_min=0 z_flag=0 z_max=0 feeder=0 estop=0 e1_home=0 y_min=0 noz_ok=1") +
            (settings.loadcell ? " lc=1 load=0.0" : " lc=0"));
        return ok();
    }
    if (cmd("LOADCELL"))
    {
        if (!settings.loadcell)
            return error("no load cell (HX711 on EXP1)");
        if (is(w, 1, "CAL"))
            set_["lc_scale"] = "420.000";
        out("loadcell ok=1 g=0.0 raw=80000 tare=80000 scale=" + (set_.count("lc_scale") ? set_["lc_scale"] : std::string("420.000")) + " rate=80");
        return ok();
    }
    if (cmd("PUMP"))
    {
        std::string k = c;
        for (char &ch : k)
            ch = (char)tolower((unsigned char)ch);
        outputs_[k] = !pumpUsed() || is(w, 1, "OFF") ? 0 : (is(w, 1, "ON") ? 100 : (int)num(w, 1, 0));
        return ok(); // pump_used 0: no pump connected, the output stays off (as the firmware)
    }
    if (cmd("VALVE"))
    {
        outputs_["valve"] = is(w, 1, "ON") ? 1 : 0;
        return ok();
    }
    if (cmd("LIGHT"))
    {
        const bool top = is(w, 1, "TOP");
        const int v = is(w, 2, "ON") || (top && is(w, 2, "IDLE")) ? 100 : (is(w, 2, "OFF") ? 0 : (int)num(w, 2, 0));
        outputs_[top ? "light_top_cmd" : "light_bottom"] = v;
        return ok();
    }
    // microSD in the Octopus slot: PAP_SIM_OCTOSD = a folder on the PC, else two built-in files
    if (cmd("SDLS") || cmd("SDREAD"))
    {
        static const std::map<std::string, std::string> BUILTIN = {
            {"/demo.pap", "[job]\nname = demo from the Octopus card\n\n[board]\norigin = 10, 10\n"},
            {"/jobs/blinky.pap", "[job]\nname = blinky\n"},
        };
        const char *dirEnv = getenv("PAP_SIM_OCTOSD");
        const std::string arg = w.size() > 1 ? w[1] : "/";
        if (cmd("SDLS"))
        {
            if (dirEnv)
            {
                std::error_code ec;
                for (auto &e : std::filesystem::directory_iterator(std::string(dirEnv) + arg, ec))
                    out("file name=" + e.path().filename().string() + " size=" +
                        std::to_string(e.is_directory() ? 0 : (long)e.file_size()) + " dir=" + (e.is_directory() ? "1" : "0"));
                if (ec)
                    return error("no such folder");
                return ok();
            }
            std::string dir = arg.back() == '/' ? arg : arg + "/";
            std::map<std::string, bool> seen;
            for (auto &f : BUILTIN)
                if (f.first.compare(0, dir.size(), dir) == 0)
                {
                    const std::string rest = f.first.substr(dir.size());
                    const size_t slash = rest.find('/');
                    const std::string name = slash == std::string::npos ? rest : rest.substr(0, slash);
                    if (seen[name])
                        continue;
                    seen[name] = true;
                    out("file name=" + name + " size=" + std::to_string(slash == std::string::npos ? f.second.size() : 0) +
                        " dir=" + (slash == std::string::npos ? "0" : "1"));
                }
            return ok();
        }
        std::string text;
        if (dirEnv)
        {
            std::ifstream f(std::string(dirEnv) + arg, std::ios::binary);
            if (!f)
                return error("no such file");
            text.assign(std::istreambuf_iterator<char>(f), {});
        }
        else
        {
            auto it = BUILTIN.find(arg);
            if (it == BUILTIN.end())
                return error("no such file");
            text = it->second;
        }
        size_t pos = w.size() > 2 ? (size_t)strtoul(w[2].c_str(), nullptr, 10) : 0;
        size_t sent = 0;
        while (pos < text.size() && sent < 1024)
        {
            size_t nl = text.find('\n', pos);
            const size_t end = nl == std::string::npos ? text.size() : nl;
            std::string l = text.substr(pos, end - pos);
            if (!l.empty() && l.back() == '\r')
                l.pop_back();
            out("|" + l);
            sent += end - pos + 1;
            pos = nl == std::string::npos ? text.size() : nl + 1;
        }
        out("sdread offset=" + std::to_string(pos) + " size=" + std::to_string(text.size()) + " eof=" +
            (pos >= text.size() ? "1" : "0"));
        return ok();
    }
    if (cmd("OUTPUTS"))
    {
        out("outputs pump=0 valve=0 light_bottom=0 light_top=0 cam_fan=50 box_fan=100 pi_fan=100");
        return ok();
    }
    if (cmd("GET"))
    {
        if (w.size() >= 2)
        {
            auto it = set_.find(w[1]);
            if (it == set_.end())
                return error("unknown key " + w[1]);
            out("set key=" + it->first + " value=" + it->second);
            return ok();
        }
        for (auto &kvp : set_)
            out("set key=" + kvp.first + " value=" + kvp.second);
        return ok();
    }
    if (cmd("SET"))
    {
        for (size_t i = 1; i < w.size(); i++)
        {
            const size_t eq = w[i].find('=');
            if (eq != std::string::npos)
                set_[w[i].substr(0, eq)] = w[i].substr(eq + 1);
            else if (i + 1 < w.size())
            {
                set_[w[i]] = w[i + 1];
                i++;
            }
        }
        if (!pumpUsed())
            outputs_["pump"] = 0; // pump_used 0: off at once
        return ok();
    }
    if (cmd("DEFAULTS"))
    {
        set_.clear();
        return ok();
    }
    if (cmd("TMC"))
    {
        for (int a = 0; a < OCTO_AXES; a++)
            out(std::string("tmc axis=") + OCTO_AXIS_NAME[a] + " ok=1 configured=1 version=33 cur=600 ms=16 sg=200");
        return ok();
    }
    error("unknown command " + c);
}

bool SimTransport::readLine(std::string &line, int timeoutMs, bool &broken)
{
    broken = false;
    advance();
    const double t = now();
    if (homeDoneAt_ > 0 && t >= homeDoneAt_)
    {
        homeDoneAt_ = -1;
        out("ok");
    }
    if (probeDoneAt_ > 0 && t >= probeDoneAt_)
    {
        probeDoneAt_ = -1;
        advance();
        char b[64];
        if (probeAxis_ == OZ)
            snprintf(b, sizeof(b), "probe hit=%d z=%.3f g=%.1f", probeFound_ ? 1 : 0, pos_[OZ], probeFound_ ? 35.0 : 0.0);
        else
            snprintf(b, sizeof(b), "probe hit=%d e1=%.3f", probeFound_ ? 1 : 0, pos_[OE1]);
        out(b);
        out("ok");
    }
    if (waitPending_ && !busy())
    {
        waitPending_ = false;
        out("ok");
    }
    if (reportMs_ > 0 && (t - lastReport_) * 1000.0 >= reportMs_)
    {
        lastReport_ = t;
        out(statusLine("!status"));
    }
    if (!out_.empty())
    {
        line = out_.front();
        out_.pop_front();
        return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs > 5 ? 5 : timeoutMs));
    return false;
}
