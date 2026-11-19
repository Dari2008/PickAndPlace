#include "machine.h"
#include "../hw/buzzer.h"
#include "../link/link.h"

Machine machine;

namespace
{
    struct Lock
    {
        SemaphoreHandle_t m;
        explicit Lock(SemaphoreHandle_t mutex) : m(mutex) { xSemaphoreTake(m, portMAX_DELAY); }
        ~Lock() { xSemaphoreGive(m); }
    };
}

void fillStatus(MachineStatus &s, const Record &r)
{
    s.state = r.str("state", "idle");
    s.x = r.num("x");
    s.y = r.num("y");
    s.z = r.num("z");
    s.e0 = r.num("e0");
    s.e1 = r.num("e1");
    s.rot = r.num("rot");
    s.homed = r.flag("homed");
    s.e1Homed = r.flag("e1_homed");
    s.busy = r.flag("busy");
    s.alarm = r.integer("alarm");
    s.pump = r.integer("pump");
    s.pumpUsed = r.integer("pump_used", 1) != 0;
    s.valve = r.flag("valve");
    s.lightTop = r.integer("light_top");
    s.lightBottom = r.integer("light_bottom");
    s.sd = r.flag("sd");
}

void fillJob(JobStatus &j, const Record &r)
{
    j.state = r.str("state", "idle");
    j.file = r.str("file");
    j.name = r.str("name");
    j.part = r.str("part");
    j.feeder = r.str("feeder");
    j.phase = r.str("phase");
    j.next = r.str("next");
    j.message = r.str("message");
    j.error = r.str("error");
    j.done = r.integer("done");
    j.total = r.integer("total");
    j.skipped = r.integer("skipped");
    j.percent = r.integer("percent");
    j.elapsed = r.integer("elapsed");
    j.remaining = r.integer("remaining", -1);
}

FeederInfo feederFrom(const Record &r)
{
    FeederInfo f;
    f.num = r.integer("num");
    f.name = r.str("name");
    f.part = r.str("part");
    f.tag = r.str("tag");
    f.pos = r.num("pos");
    f.advance = r.num("advance");
    f.teeth = (int)r.integer("teeth");
    f.next = r.integer("next", 1);
    f.pocketCount = r.integer("pockets");
    return f;
}

void Machine::begin()
{
    mutex_ = xSemaphoreCreateMutex();
    ctrl.onEvent([this](const Record &e) { handle(e); });
}

void Machine::handle(const Record &e)
{
    {
        Lock lock(mutex_);
        if (e.type == "status")
        {
            fillStatus(status_, e);
            status_.connected = true;
            if (status_.state != "scanning" && scan_.active)
                scan_.active = false;
        }
        else if (e.type == "job")
        {
            const bool wasActive = job_.active();
            fillJob(job_, e);
            if (wasActive && !job_.active())
                initPending_ = true; // refresh feeders (next pockets changed)
        }
        else if (e.type == "scan")
        {
            scan_.active = true;
            const std::string state = e.str("state");
            if (state == "homing")
            {
                scan_ = ScanInfo();
                scan_.active = true;
            }
            if (e.find("num"))
            {
                ScanFound f;
                f.num = e.integer("num");
                f.pos = e.num("pos");
                f.tag = e.flag("tag");
                f.isNew = e.flag("new");
                f.name = e.str("name");
                scan_.found.push_back(f);
            }
            if (!state.empty())
                scan_.state = state;
            if (e.find("count"))
                scan_.count = e.integer("count");
            if (e.find("e1"))
                scan_.e1 = e.num("e1");
            if (e.find("name"))
                scan_.last = e.str("name");
            if (e.find("error"))
                scan_.error = e.str("error");
            if (state == "done" || state == "failed")
            {
                scan_.active = false;
                initPending_ = true;
            }
        }
        else if (e.type == "alarm")
            alarm_ = e.str("text");
        else if (e.type == "ready")
            initPending_ = true; // controller restarted: set it up again
        // Live status (4 per second) only repaints the parts of a screen that show it.
        if (e.type == "status" || e.type == "job")
            statusRevision_++;
        else
            revision_++;
    }
    if (e.type == "changed")
    {
        if (e.str("what") == "feeders")
            refreshFeeders();
        else
            refreshSettings();
    }
    if (e.type == "beep" && setting("beep", "1") != "0")
        buzzer.beep(e.integer("ms", 100));
    for (auto &fn : listeners_)
        fn(e);
}

void Machine::init()
{
    ctrl.send("REPORT 250");
    ctrl.send("JOB", [this](const Reply &r) {
        if (!r.ok || r.records.empty())
            return;
        Lock lock(mutex_);
        fillJob(job_, r.records[0]);
        revision_++;
    });
    refreshSettings();
    refreshFeeders();
}

void Machine::tick()
{
    const bool connected = ctrl.connected();
    if (connected != wasConnected_)
    {
        Lock lock(mutex_);
        status_.connected = connected;
        if (!connected)
            status_.state = "offline";
        revision_++;
    }
    if (connected && !wasConnected_)
        initPending_ = true;
    // The controller only reports after REPORT: poke it while we hear nothing.
    static uint32_t lastPoke = 0;
    if (!connected && millis() - lastPoke > 3000)
    {
        lastPoke = millis();
        ctrl.send("REPORT 250", nullptr, 1000);
    }
    wasConnected_ = connected;
    if (initPending_ && connected)
    {
        initPending_ = false;
        init();
    }
}

void Machine::resetScan()
{
    Lock lock(mutex_);
    scan_ = ScanInfo();
    revision_++;
}

void Machine::refreshFeeders()
{
    ctrl.send("FEEDERS", [this](const Reply &r) {
        if (!r.ok)
            return;
        std::vector<FeederInfo> list;
        for (auto &rec : r.records)
            if (rec.type == "feeder")
                list.push_back(feederFrom(rec));
        Lock lock(mutex_);
        feeders_ = std::move(list);
        revision_++;
    });
}

void Machine::refreshSettings()
{
    ctrl.send("GET", [this](const Reply &r) {
        if (!r.ok)
            return;
        std::vector<SettingInfo> list;
        for (auto &rec : r.records)
            if (rec.type == "set")
                list.push_back({rec.str("key"), rec.str("value")});
        Lock lock(mutex_);
        settings_ = std::move(list);
        revision_++;
    });
    // The catalog (labels, groups, ranges) with the current values: Settings screens
    ctrl.send("SETTINGS", [this](const Reply &r) {
        if (!r.ok)
            return;
        std::vector<CatalogEntry> list;
        for (auto &rec : r.records)
        {
            if (rec.type != "setting")
                continue;
            CatalogEntry e;
            e.key = rec.str("key"), e.value = rec.str("value"), e.group = rec.str("group"), e.label = rec.str("label");
            e.unit = rec.str("unit"), e.kind = rec.str("kind", "num"), e.choices = rec.str("choices"), e.help = rec.str("help");
            e.min = rec.num("min"), e.max = rec.num("max"), e.decimals = (int)rec.integer("dec", 2);
            e.machine = rec.flag("machine");
            list.push_back(e);
        }
        Lock lock(mutex_);
        catalog_ = std::move(list);
        revision_++;
    });
}

std::vector<CatalogEntry> Machine::catalog()
{
    Lock lock(mutex_);
    return catalog_;
}

MachineStatus Machine::status()
{
    Lock lock(mutex_);
    return status_;
}

JobStatus Machine::job()
{
    Lock lock(mutex_);
    return job_;
}

std::vector<FeederInfo> Machine::feeders()
{
    Lock lock(mutex_);
    return feeders_;
}

std::vector<SettingInfo> Machine::settings()
{
    Lock lock(mutex_);
    return settings_;
}

ScanInfo Machine::scan()
{
    Lock lock(mutex_);
    return scan_;
}

std::string Machine::lastAlarm()
{
    Lock lock(mutex_);
    return alarm_;
}

std::string Machine::setting(const char *key, const char *def)
{
    Lock lock(mutex_);
    for (auto &s : settings_)
        if (s.key == key)
            return s.value;
    return def;
}
