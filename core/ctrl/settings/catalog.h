/**
 * catalog.h - every setting of the machine (Pi + Octopus) with group, label, unit, range and help
 * -----------------------------------------------------------------------------
 * One list for the display (Settings) and the website: sorted into groups, searchable.
 *   SETTINGS            setting key= value= group= label= unit= kind= min= max= dec= choices= help=   (all)
 *   SETTINGS GROUPS     group num= name= count=
 *   SETTINGS FIND <txt> the settings whose key, label or help contain <txt>
 * kind: num | bool | choice (choices "a|b|c") | text. Pi settings take range and default from
 * general_settings.cpp; Octopus settings (motion, homing, drivers, outputs) carry their firmware
 * defaults here and are shown with that value until the board has reported its own.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <string>
#include <vector>

struct SettingMeta
{
    std::string key, group, label, unit, help;
    std::string kind = "num";  // num | bool | choice | text
    std::string choices;       // "none|switch|sensorless"
    float min = 0, max = 0, def = 0;
    int decimals = 2;
    bool machine = false;      // lives on the Octopus
};

const std::vector<SettingMeta> &settingsCatalog();
const std::vector<std::string> &settingsGroups(); // display order
std::string settingValue(const SettingMeta &m);    // current value (text)
