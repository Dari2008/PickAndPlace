#include "prefs.h"
#include "../compat/SdFat.h"

#include <cstdlib>
#include <fstream>

Prefs prefs;

void Prefs::load()
{
    if (loaded_)
        return;
    loaded_ = true;
    std::ifstream f(sdfs_path("/ui.txt"));
    std::string line;
    while (std::getline(f, line))
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#')
            continue;
        auto trim = [](std::string s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r'))
                s.pop_back();
            while (!s.empty() && s[0] == ' ')
                s.erase(0, 1);
            return s;
        };
        kv_[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
}

std::string Prefs::get(const std::string &key, const std::string &def)
{
    std::lock_guard<std::mutex> l(m_);
    load();
    auto it = kv_.find(key);
    return it == kv_.end() ? def : it->second;
}

int Prefs::getInt(const std::string &key, int def)
{
    const std::string v = get(key);
    return v.empty() ? def : atoi(v.c_str());
}

void Prefs::set(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> l(m_);
    load();
    kv_[key] = value;
    const std::string path = sdfs_path("/ui.txt");
    {
        std::ofstream f(path + ".tmp");
        f << "# display UI (touch calibration, backlight, ...), written by pap-core\n";
        for (auto &e : kv_)
            f << e.first << " = " << e.second << "\n";
    }
    std::rename((path + ".tmp").c_str(), path.c_str());
}
