/**
 * prefs.h - small key = value store for the display UI (replaces the ESP32's Preferences / NVS)
 * -----------------------------------------------------------------------------
 *   prefs.get("bl_pct", "100");   prefs.set("bl_pct", "80");   // saved at once
 * File: <data>/ui.txt (next to settings.txt).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <map>
#include <mutex>
#include <string>

class Prefs
{
public:
    std::string get(const std::string &key, const std::string &def = "");
    int getInt(const std::string &key, int def);
    void set(const std::string &key, const std::string &value);
    void setInt(const std::string &key, int v) { set(key, std::to_string(v)); }

private:
    void load();
    bool loaded_ = false;
    std::map<std::string, std::string> kv_;
    std::mutex m_;
};

extern Prefs prefs;
