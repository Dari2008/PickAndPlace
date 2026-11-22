// GET SET RELOAD DEFAULTS
// Pi settings (general_settings.h) are stored in /settings.txt. Every other key
// belongs to the Octopus (octopus-fw/src/settings.h): SET sends it there and keeps
// it in /machine.txt (machine_store.h), GET lists both.
#include "commands.h"
#include "../settings/catalog.h"

#include <cctype>
#include "../../octo/octopus.h"
#include "../settings/feeder_store.h"
#include "../settings/general_settings.h"
#include "../settings/machine_store.h"

static void printSetting(Print &p, uint8_t i)
{
    char key[40], value[80];
    settings.keyAt(i, key);
    settings.valueAt(i, value, sizeof(value));
    out::rec(p, PSTR("set"));
    out::str(p, PSTR("key"), key);
    out::str(p, PSTR("value"), value);
    p.println();
}

static void printMachine(Print &p, const std::string &key, const std::string &value)
{
    out::rec(p, PSTR("set"));
    out::str(p, PSTR("key"), key.c_str());
    out::str(p, PSTR("value"), value.c_str());
    out::str(p, PSTR("on"), "octopus");
    p.println();
}

void cmdGet(Port &port, Args &args)
{
    if (args.count() > 1)
    {
        const int i = settings.find(args[1]);
        if (i >= 0)
        {
            printSetting(port.io, (uint8_t)i);
            out::ok(port.io);
            return;
        }
        std::string v = octo.value(args[1]);
        if (v.empty())
        {
            const OctoReply r = octo.call(std::string("GET ") + args[1]);
            const Record *rec = r.first("set");
            if (!r.ok || !rec)
            {
                out::error2(port.io, PSTR("unknown setting"), args[1]);
                return;
            }
            v = rec->str("value");
        }
        printMachine(port.io, args[1], v);
        out::ok(port.io);
        return;
    }
    const uint8_t n = settings.count();
    for (uint8_t i = 0; i < n; i++)
        printSetting(port.io, i);
    for (auto &kv : octo.values())
        printMachine(port.io, kv.first, kv.second);
    out::ok(port.io);
}

// SET key value | SET key=value
void cmdSet(Port &port, Args &args)
{
    char key[64];
    const char *value;
    const char *eq = strchr(args[1], '=');
    if (eq && args.count() == 2)
    {
        const size_t len = std::min((size_t)(eq - args[1]), sizeof(key) - 1);
        memcpy(key, args[1], len);
        key[len] = '\0';
        value = eq + 1;
    }
    else if (args.count() == 3)
    {
        strncpy(key, args[1], sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        value = args[2];
    }
    else
    {
        out::error(port.io, PSTR("usage: SET <key> <value>"));
        return;
    }
    const int i = settings.find(key);
    if (i < 0)
    {
        // Not a Pi setting: the Octopus decides (and checks the range)
        const OctoReply r = octo.call(std::string("SET ") + key + " " + value);
        if (!r.ok)
        {
            out::error2(port.io, PSTR("unknown setting or bad value:"), r.error.c_str());
            return;
        }
        machineStore::set(key, value);
        machineStore::save();
        machineStore::fetch();
        printMachine(port.io, key, octo.value(key, value));
        out::ok(port.io);
        return;
    }
    PGM_P err = settings.setValue((uint8_t)i, value);
    if (err)
    {
        out::error(port.io, err);
        return;
    }
    settings.applySpeeds();
    // the bottom ring is on: show the new brightness right away
    if (strcmp(key, "light_bottom") == 0 && octo.status().lightBottom > 0)
        octo.call("LIGHT bottom " + std::to_string((int)settings.lightBottom));
    if (!settings.save())
    {
        out::error(port.io, PSTR("set, but can't save settings.txt"));
        return;
    }
    printSetting(port.io, (uint8_t)i);
    out::ok(port.io);
}

void cmdReload(Port &port, Args &)
{
    if (commands::refuseIfBusy(port))
        return;
    settings.setDefaults();
    const bool ok = settings.load();
    settings.applySpeeds();
    machineStore::load();
    std::string err;
    machineStore::pushAll(err);
    machineStore::fetch();
    if (!feeders.scan() || !ok)
        out::error(port.io, PSTR("settings.txt missing (defaults in use)"));
    else if (!err.empty())
        out::error2(port.io, PSTR("machine.txt:"), err.c_str());
    else
        out::ok(port.io);
}

// DEFAULTS: Pi settings; DEFAULTS MACHINE: also the Octopus values (machine.txt emptied)
void cmdDefaults(Port &port, Args &args)
{
    if (commands::refuseIfBusy(port))
        return;
    if (args.is(1, PSTR("MACHINE")))
    {
        machineStore::clear();
        machineStore::save();
        octo.call("DEFAULTS");
        machineStore::fetch();
    }
    settings.setDefaults();
    settings.applySpeeds();
    if (!settings.save())
        out::error(port.io, PSTR("defaults set, but can't save settings.txt"));
    else
        out::ok(port.io);
}

// SETTINGS                 every setting with its description (catalog.h)
// SETTINGS GROUPS          group num= name= count=
// SETTINGS FIND <text>     the settings whose key, label, group or help contain <text>
static std::string lower(std::string s)
{
    for (auto &c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

static void printMeta(Print &p, const SettingMeta &m)
{
    out::rec(p, PSTR("setting"));
    out::str(p, PSTR("key"), m.key.c_str());
    out::str(p, PSTR("value"), settingValue(m).c_str());
    out::str(p, PSTR("group"), m.group.c_str());
    out::str(p, PSTR("label"), m.label.c_str());
    out::str(p, PSTR("unit"), m.unit.c_str());
    out::str(p, PSTR("kind"), m.kind.c_str());
    if (m.kind == "num")
    {
        out::flt(p, PSTR("min"), m.min, 4);
        out::flt(p, PSTR("max"), m.max, 4);
        out::num(p, PSTR("dec"), m.decimals);
    }
    if (!m.choices.empty())
        out::str(p, PSTR("choices"), m.choices.c_str());
    out::num(p, PSTR("machine"), m.machine);
    out::str(p, PSTR("help"), m.help.c_str());
    p.println();
}

void cmdSettings(Port &port, Args &args)
{
    Print &p = port.io;
    const auto &all = settingsCatalog();
    if (args.is(1, PSTR("GROUPS")))
    {
        int n = 0;
        for (const auto &g : settingsGroups())
        {
            int count = 0;
            for (const auto &m : all)
                count += m.group == g;
            out::rec(p, PSTR("group"));
            out::num(p, PSTR("num"), ++n);
            out::str(p, PSTR("name"), g.c_str());
            out::num(p, PSTR("count"), count);
            p.println();
        }
        out::ok(p);
        return;
    }
    std::string find;
    if (args.is(1, PSTR("FIND")))
    {
        // everything after FIND, as typed
        const std::string &l = commands::currentLine();
        size_t i = lower(l).find("find");
        find = lower(i == std::string::npos ? "" : l.substr(i + 4));
        while (!find.empty() && (find[0] == ' ' || find[0] == '"'))
            find.erase(0, 1);
        while (!find.empty() && (find.back() == ' ' || find.back() == '"'))
            find.pop_back();
    }
    for (const auto &m : all)
        if (find.empty() || lower(m.key + " " + m.label + " " + m.group + " " + m.help).find(find) != std::string::npos)
            printMeta(p, m);
    out::ok(p);
}
