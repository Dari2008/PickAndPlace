#include "nozzles.h"
#include "settings/general_settings.h"
#include "../octo/octopus.h"
#include "../vision/vision.h"
#include "mosfets.h"
#include <motion.h>
#include "../link/record.h"
#include <SdFat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

Nozzles nozzles;

int Nozzles::count() const { return std::max(0, std::min((int)settings.nozzleCount, MAX_NOZZLES)); }

bool Nozzles::slotXY(int n, float &x, float &y) const
{
    const int c = count();
    if (n < 1 || n > c)
        return false;
    const float t = c > 1 ? (float)(n - 1) / (float)(c - 1) : 0.0f;
    x = settings.nozzleFirst[0] + t * (settings.nozzleLast[0] - settings.nozzleFirst[0]);
    y = settings.nozzleFirst[1] + t * (settings.nozzleLast[1] - settings.nozzleFirst[1]);
    return true;
}

int Nozzles::find(const std::string &key) const
{
    if (key.empty())
        return 0;
    char *end = nullptr;
    const long n = strtol(key.c_str(), &end, 10);
    if (end && !*end)
        return n >= 0 && n <= count() ? (int)n : 0; // "0" = none
    for (int i = 0; i < count(); i++)
        if (!slots_[i].name.empty() && strcasecmp(slots_[i].name.c_str(), key.c_str()) == 0)
            return i + 1;
    return 0;
}

// <data>/nozzles.txt
//   mounted = 2
//   slot 1 name="CN040" dx=0 dy=0 dz=0
void Nozzles::load()
{
    loaded_ = true;
    for (auto &s : slots_)
        s = NozzleSlot();
    mounted_ = 0;
    std::ifstream f(sdfs_path("/nozzles.txt"));
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("mounted", 0) == 0)
        {
            const size_t eq = line.find('=');
            if (eq != std::string::npos)
                mounted_ = atoi(line.c_str() + eq + 1);
            continue;
        }
        int n = 0;
        if (sscanf(line.c_str(), "slot %d", &n) != 1 || n < 1 || n > MAX_NOZZLES)
            continue;
        NozzleSlot &s = slots_[n - 1];
        const size_t q1 = line.find("name=\"");
        if (q1 != std::string::npos)
        {
            const size_t q2 = line.find('"', q1 + 6);
            s.name = line.substr(q1 + 6, q2 == std::string::npos ? std::string::npos : q2 - q1 - 6);
        }
        const size_t dx = line.find(" dx="), dy = line.find(" dy="), dz = line.find(" dz=");
        if (dz != std::string::npos)
            s.dz = strtof(line.c_str() + dz + 4, nullptr);
        if (dx != std::string::npos)
            s.dx = strtof(line.c_str() + dx + 4, nullptr);
        if (dy != std::string::npos)
            s.dy = strtof(line.c_str() + dy + 4, nullptr);
    }
    if (mounted_ < 0 || mounted_ > MAX_NOZZLES)
        mounted_ = 0;
    // Simulation: the mounted nozzle is not in its slot
    simMask_ = 0xFF;
    if (mounted_)
        simMask_ &= ~(1u << (mounted_ - 1));
}

bool Nozzles::save()
{
    const std::string path = sdfs_path("/nozzles.txt");
    {
        std::ofstream f(path + ".tmp");
        if (!f)
            return false;
        f << "# Nozzle rack: names and tip offsets per slot, the nozzle on the head (written by pap-core)\n";
        f << "mounted = " << mounted_ << "\n";
        for (int i = 0; i < MAX_NOZZLES; i++)
        {
            char d[80];
            snprintf(d, sizeof(d), " dx=%.3f dy=%.3f dz=%.3f", slots_[i].dx, slots_[i].dy, slots_[i].dz);
            f << "slot " << (i + 1) << " name=\"" << slots_[i].name << "\"" << d << "\n";
        }
    }
    return std::rename((path + ".tmp").c_str(), path.c_str()) == 0;
}

void Nozzles::setMounted(int n)
{
    mounted_ = n < 0 || n > MAX_NOZZLES ? 0 : n;
    save();
}

bool Nozzles::readRaw(unsigned &raw)
{
    if (octo.simulated())
    {
        raw = simRaw_;
        return true;
    }
    const OctoReply r = octo.call("SENSORS", 2000);
    if (!r.ok || r.records.empty() || !r.records[0].integer("noz_ok"))
        return false;
    raw = 0;
    for (int i = 0; i < MAX_NOZZLES; i++)
    {
        char k[8];
        snprintf(k, sizeof(k), "noz%d", i + 1);
        if (r.records[0].integer(k))
            raw |= 1u << i;
    }
    return true;
}

bool Nozzles::readPresent(unsigned &mask)
{
    if (octo.simulated())
    {
        mask = simMask_;
        return true;
    }
    unsigned raw;
    if (!readRaw(raw))
        return false;
    // nozzle_sensor_invert = 0: bit set = nozzle in the slot
    if (settings.nozzleSensorInvert)
        raw = ~raw & 0xFF;
    mask = 0;
    for (int i = 0; i < MAX_NOZZLES; i++)
        if ((raw >> settings.nozzleBit[i]) & 1)
            mask |= 1u << i;
    return true;
}

bool Nozzles::slotShows(int slot, bool want, bool &there)
{
    const uint32_t t0 = millis();
    for (;;)
    {
        unsigned mask;
        if (!readPresent(mask))
            return false;
        there = (mask >> (slot - 1)) & 1;
        if (there == want || octo.simulated() || millis() - t0 >= SENSOR_SETTLE_MS)
            return true;
        delay(100);
    }
}

void NozzleLearn::start(unsigned raw, int slots)
{
    active_ = slots > 0;
    slots_ = slots > MAX_NOZZLES ? MAX_NOZZLES : slots;
    next_ = 1;
    last_ = raw;
    used_ = 0;
}

NozzleLearn::Step NozzleLearn::feed(unsigned raw)
{
    if (!active_)
        return WAITING;
    const unsigned fresh = (raw ^ last_) & 0xFF & ~used_;
    last_ = raw;
    if (!fresh)
        return WAITING;
    if (fresh & (fresh - 1))
        return TWO_AT_ONCE;
    int b = 0;
    while (!((fresh >> b) & 1))
        b++;
    bit_[next_ - 1] = b;
    used_ |= fresh;
    if (++next_ > slots_)
    {
        active_ = false;
        return FINISHED;
    }
    return ASSIGNED;
}

std::vector<NozzleStep> Nozzles::plan(int target) const
{
    std::vector<NozzleStep> p;
    const int from = mounted_;
    if (target == from)
        return p;
    const float safe = settings.nozzleSafeZ, sp = settings.nozzleSpeed;
    const float sx = settings.nozzleSlide[0], sy = settings.nozzleSlide[1];
    const bool slide = fabsf(sx) > 0.001f || fabsf(sy) > 0.001f;
    auto move = [&](float x, float y, float z, float speed) {
        NozzleStep s{NozzleStep::MOVE};
        s.x = x, s.y = y, s.z = z, s.speed = speed;
        p.push_back(s);
    };
    auto expect = [&](int slot, bool present) {
        if (!settings.nozzleCheck)
            return;
        NozzleStep s{NozzleStep::EXPECT};
        s.slot = slot, s.present = present;
        p.push_back(s);
    };
    auto mounted = [&](int slot) {
        NozzleStep s{NozzleStep::MOUNTED};
        s.slot = slot;
        p.push_back(s);
    };
    p.push_back(NozzleStep{NozzleStep::ROTATE0});
    float x, y;
    if (from && slotXY(from, x, y))
    {
        expect(from, false); // the slot must be free for it
        move(x + sx, y + sy, safe, 0);
        move(x + sx, y + sy, settings.nozzleZDrop, sp);
        if (slide)
            move(x, y, settings.nozzleZDrop, sp);
        move(x, y, safe, sp); // the slot keeps the nozzle
        mounted(0);
        expect(from, true);
    }
    if (target && slotXY(target, x, y))
    {
        expect(target, true);
        move(x, y, safe, 0);
        move(x, y, settings.nozzleZPick, sp); // pushed on
        NozzleStep w{NozzleStep::WAIT};
        w.ms = settings.nozzleWaitMs;
        p.push_back(w);
        if (slide)
            move(x + sx, y + sy, settings.nozzleZPick, sp);
        move(slide ? x + sx : x, slide ? y + sy : y, safe, sp);
        mounted(target);
        expect(target, false);
        // Centre it: the bottom camera measures where the tip sits relative to the rotation centre
        if (settings.nozzleCalibrate && vision::available(vision::BOTTOM))
        {
            const float *c = settings.camBottomPos;
            move(c[0], c[1], safe, 0);
            move(c[0], c[1], c[2], 0);
            NozzleStep cal{NozzleStep::CALIBRATE};
            cal.slot = target;
            p.push_back(cal);
            move(c[0], c[1], safe, 0);
        }
        // Tip height: touch the load cell (seesaw stops on contact)
        if (settings.nozzleTouchoff && settings.loadcell)
        {
            NozzleStep t{NozzleStep::TOUCHOFF};
            t.slot = target;
            p.push_back(t);
        }
    }
    return p;
}

void Nozzles::simApply(const NozzleStep &s)
{
    if (s.kind != NozzleStep::MOUNTED)
        return;
    if (mounted_)
        simMask_ |= 1u << (mounted_ - 1); // the old one stays in its slot
    if (s.slot)
        simMask_ &= ~(1u << (s.slot - 1)); // the new one leaves its slot
}

static std::string slotText(int n) { return "slot " + std::to_string(n); }

// Blocking version for commands (the controller thread waits; jobs use their own executor)
bool Nozzles::change(int target, std::string &err)
{
    if (target < 0 || target > count())
    {
        err = "no such nozzle slot";
        return false;
    }
    char cmd[96];
    for (const NozzleStep &s : plan(target))
    {
        switch (s.kind)
        {
        case NozzleStep::ROTATE0:
        {
            const OctoReply r = octo.call("ROTATE 0", 30000);
            if (!r.ok)
                return err = r.error, false;
            break;
        }
        case NozzleStep::MOVE:
        {
            if (s.speed > 0)
                snprintf(cmd, sizeof(cmd), "MOVE x=%.3f y=%.3f z=%.3f speed=%.2f", s.x, s.y, s.z, s.speed);
            else
                snprintf(cmd, sizeof(cmd), "MOVE x=%.3f y=%.3f z=%.3f", s.x, s.y, s.z);
            OctoReply r = octo.call(cmd, 60000);
            if (r.ok)
                r = octo.call("WAIT", 120000);
            if (!r.ok)
            {
                motion.invalidateTargets();
                return err = r.error.empty() ? "move failed" : r.error, false;
            }
            break;
        }
        case NozzleStep::WAIT:
            delay(s.ms);
            break;
        case NozzleStep::CALIBRATE:
            if (!calibrate(s.slot, err))
            {
                motion.invalidateTargets();
                return false;
            }
            break;
        case NozzleStep::TOUCHOFF:
            if (!touchOff(s.slot, err))
                return false;
            break;
        case NozzleStep::MOUNTED:
            if (octo.simulated())
                simApply(s);
            setMounted(s.slot);
            break;
        case NozzleStep::EXPECT:
        {
            bool there = false;
            if (!slotShows(s.slot, s.present, there))
                return err = "nozzle sensors not readable (tool changer board silent or Octopus offline)", false;
            if (there != s.present)
            {
                motion.invalidateTargets();
                err = s.present ? slotText(s.slot) + ": no nozzle seen in the slot"
                                : slotText(s.slot) + ": slot is not empty";
                return false;
            }
            break;
        }
        }
    }
    motion.invalidateTargets();
    return true;
}

bool Nozzles::calibrate(int n, std::string &err)
{
    if (n < 1 || n > MAX_NOZZLES)
        return err = "no such slot", false;
    mosfets.setLight(false, settings.lightBottom / 100.0f); // the top light goes dark by itself (Octopus)
    delay(settings.camSettleMs);
    float dx = 0, dy = 0;
    const bool ok = vision::measureNozzle(dx, dy, err);
    mosfets.setLight(false, 0);
    if (!ok)
        return false;
    slot(n).dx = dx;
    slot(n).dy = dy;
    save();
    return true;
}

float Nozzles::simTipDz(int n)
{
    static const float DZ[MAX_NOZZLES] = {0.10f, -0.15f, 0.22f, 0.0f, -0.08f, 0.30f, -0.20f, 0.05f};
    return n >= 1 && n <= MAX_NOZZLES ? DZ[n - 1] : 0.0f;
}

// Seesaw: head 2 (vacuum nozzle) goes down with negative Z, its tip is reach + z above the bed.
bool Nozzles::touchOff(int n, std::string &err, float *reachOut, float *zOut)
{
    if (!settings.loadcell)
        return err = "no load cell (Settings > Load cell)", false;
    if (n < 0 || n > MAX_NOZZLES)
        return err = "no such slot", false;
    const float pad = settings.loadcellHeight;
    const float reachNow = settings.headReach[1] + (n ? slot(n).dz : 0.0f);
    const float expect = pad - reachNow; // contact Z if the stored height is right
    const float start = fminf(0.0f, expect + settings.loadcellBackoff + 2.0f);
    const float safe = fminf(0.0f, fmaxf(settings.nozzleSafeZ, start));
    char c[128];
    auto run = [&](const char *line, bool wait) {
        OctoReply r = octo.call(line, 60000);
        if (r.ok && wait)
            r = octo.call("WAIT", 120000);
        if (!r.ok)
            err = r.error.empty() ? std::string("motion board offline") : r.error;
        return r.ok;
    };
    auto probe = [&](float to, float speed, float &z) {
        snprintf(c, sizeof(c), "PROBE z=%.3f speed=%.3f", to, speed);
        const OctoReply r = octo.call(c, 120000);
        if (!r.ok)
            return err = r.error.empty() ? std::string("probe failed") : r.error, false;
        const Record *h = r.first("probe");
        if (!h || !h->flag("hit"))
            return err = "no contact with the load cell: check loadcell_x / _y / _height and head2_reach", false;
        z = h->num("z");
        return true;
    };
    float z1 = 0, z2 = 0;
    bool ok = true;
    snprintf(c, sizeof(c), "MOVE z=%.3f", safe);
    ok = ok && run(c, true);
    snprintf(c, sizeof(c), "MOVE x=%.3f y=%.3f", settings.loadcellPos[0], settings.loadcellPos[1]);
    ok = ok && run(c, true);
    snprintf(c, sizeof(c), "MOVE z=%.3f", start);
    ok = ok && run(c, true);
    ok = ok && probe(expect - settings.loadcellDepth, settings.loadcellSpeed, z1);
    snprintf(c, sizeof(c), "MOVE z=%.3f speed=%.3f", fminf(0.0f, z1 + settings.loadcellBackoff), settings.loadcellSpeed);
    ok = ok && run(c, true);
    ok = ok && probe(z1 - 1.0f, settings.loadcellSlow, z2);
    snprintf(c, sizeof(c), "MOVE z=%.3f speed=%.3f", safe, settings.loadcellSpeed);
    const bool up = run(c, true); // lift even after a failed touch
    motion.invalidateTargets();
    if (!ok || !up)
        return false;
    const float reach = pad - z2;
    if (n)
    {
        slot(n).dz = reach - settings.headReach[1];
        save();
    }
    else
    {
        settings.headReach[1] = reach;
        settings.save();
    }
    if (reachOut)
        *reachOut = reach;
    if (zOut)
        *zOut = z2;
    return true;
}
