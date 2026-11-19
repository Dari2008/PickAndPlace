#include "machine_store.h"
#include "storage.h"
#include "../../octo/octopus.h"

static const char PATH[] = "/machine.txt";
static std::map<std::string, std::string> g_values;

namespace machineStore
{
    static void onKey(const char *key, const char *value, void *)
    {
        g_values[key] = value;
    }

    bool load()
    {
        g_values.clear();
        return storage.readKv(PATH, onKey, nullptr);
    }

    bool save()
    {
        File f;
        if (!storage.openWrite(f, PATH))
            return false;
        f.print("# Octopus machine values (sent to the board on every connect) - octopus-fw/README.md\n");
        for (auto &kv : g_values)
        {
            f.print(kv.first);
            f.print(" = ");
            f.println(kv.second);
        }
        return f.close();
    }

    const std::map<std::string, std::string> &values() { return g_values; }

    void set(const std::string &key, const std::string &value) { g_values[key] = value; }

    void clear() { g_values.clear(); }

    int pushAll(std::string &err)
    {
        int bad = 0;
        for (auto &kv : g_values)
        {
            const OctoReply r = octo.call("SET " + kv.first + " " + kv.second);
            if (!r.ok)
            {
                bad++;
                if (!err.empty())
                    err += "; ";
                err += kv.first + ": " + r.error;
            }
        }
        return bad;
    }

    bool fetch()
    {
        const OctoReply r = octo.call("GET");
        if (!r.ok)
            return false;
        std::map<std::string, std::string> v;
        for (const Record &rec : r.records)
            if (rec.type == "set")
                v[rec.str("key")] = rec.str("value");
        octo.setValues(v);
        return true;
    }
}
