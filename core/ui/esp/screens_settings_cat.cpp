// Settings from the catalog (SETTINGS): a group or search results, editors per kind, choice list
#include <cctype>
#include <cstdlib>
#include "screens.h"

SettingsGroupScreen settingsGroupScreen;
ChoiceScreen choiceScreen;

enum
{
    SG_ROWS = 10,
    CH_ROWS = 10
};

static std::string lower(std::string s)
{
    for (auto &c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

static void saveSetting(const std::string &key, const std::string &value)
{
    ui.send("SET " + key + " " + quote(value), [](const Reply &r) {
        machine.refreshSettings();
        if (r.ok)
            ui.toast("Saved");
    });
}

// "12.5 mm/s", "on", "sensorless"
static std::string shown(const CatalogEntry &e)
{
    if (e.kind == "bool")
        return e.value == "1" || e.value == "on" ? "on" : "off";
    if (e.value.empty())
        return "-";
    return e.unit.empty() || e.kind != "num" ? e.value : e.value + " " + e.unit;
}

void editSetting(const CatalogEntry &e)
{
    if (e.kind == "bool")
    {
        saveSetting(e.key, e.value == "1" ? "0" : "1");
        return;
    }
    if (e.kind == "choice")
    {
        choiceScreen.open(e);
        return;
    }
    if (e.kind == "text")
    {
        const std::string key = e.key;
        textInput.open(e.label, e.value, false, 60, [key](const std::string &t) { saveSetting(key, t); });
        return;
    }
    NumberEditor::Spec s;
    s.title = e.label;
    s.what = e.help.empty() ? e.key : e.help;
    s.value = (float)atof(e.value.c_str());
    s.unit = e.unit;
    s.min = e.min;
    s.max = e.max > e.min ? e.max : 1e9f;
    s.decimals = (uint8_t)std::max(0, std::min(e.decimals, 5));
    switch (s.decimals)
    {
    case 0:
        s.steps = {1, 10, 100};
        break;
    case 1:
        s.steps = {0.1f, 1, 10};
        break;
    case 2:
        s.steps = {0.01f, 0.1f, 1};
        break;
    default:
        s.steps = {0.001f, 0.01f, 0.1f};
        break;
    }
    s.step = 1;
    const std::string key = e.key;
    const int dec = s.decimals;
    numberEditor.open(s, [key, dec](float v) { saveSetting(key, fmtNum(v, dec)); });
}

// ---------------------------------------------------------------- group / search

void SettingsGroupScreen::openGroup(const std::string &group)
{
    group_ = group;
    search_.clear();
    list_.reset();
    ui.push(this);
}

void SettingsGroupScreen::openSearch(const std::string &text)
{
    group_.clear();
    search_ = text;
    list_.reset();
    ui.push(this);
}

std::vector<CatalogEntry> SettingsGroupScreen::entries() const
{
    std::vector<CatalogEntry> out;
    const std::string q = lower(search_);
    for (auto &e : machine.catalog())
    {
        if (!search_.empty())
        {
            if (lower(e.key + " " + e.label + " " + e.group + " " + e.help).find(q) != std::string::npos)
                out.push_back(e);
        }
        else if (e.group == group_)
            out.push_back(e);
    }
    return out;
}

void SettingsGroupScreen::draw(Gfx &g)
{
    g.background();
    const auto list = entries();
    if (search_.empty())
        g.titleBar(group_.c_str());
    else
    {
        const std::string t = "\"" + search_ + "\"";
        g.titleBar(t.c_str(), true, (std::to_string(list.size()) + " found").c_str());
    }
    std::vector<Row> rows;
    for (auto &e : list)
    {
        Row r;
        r.label = e.label;
        r.sub = search_.empty() ? e.help : e.group + (e.help.empty() ? "" : " · " + e.help);
        if (e.kind == "bool")
            r.toggle = e.value == "1" ? 1 : 0;
        else
        {
            r.value = shown(e);
            r.valueColor = th::TEXT;
        }
        rows.push_back(r);
    }
    if (rows.empty())
    {
        Row r;
        r.icon = Icon::Warn;
        r.label = search_.empty() ? "Loading ..." : "No setting matches";
        r.sub = search_.empty() ? "" : "try a shorter word, e.g. speed, nozzle, camera";
        rows.push_back(r);
    }
    list_.draw(g, SG_ROWS, rows, 25, 38);
}

void SettingsGroupScreen::action(uint16_t id)
{
    const auto list = entries();
    const int i = id - SG_ROWS;
    if (i >= 0 && i < (int)list.size())
        editSetting(list[i]);
}

// ---------------------------------------------------------------- choice

void ChoiceScreen::open(const CatalogEntry &e)
{
    e_ = e;
    options_.clear();
    size_t p = 0;
    while (p <= e.choices.size())
    {
        size_t bar = e.choices.find('|', p);
        if (bar == std::string::npos)
            bar = e.choices.size();
        options_.push_back(e.choices.substr(p, bar - p));
        p = bar + 1;
    }
    list_.reset();
    ui.push(this);
}

void ChoiceScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar(e_.label.c_str());
    std::vector<Row> rows;
    for (auto &o : options_)
    {
        Row r;
        r.label = o;
        const bool cur = o == e_.value;
        r.icon = cur ? Icon::Check : Icon::None;
        r.iconColor = th::OK;
        if (cur)
            r.pill = "CURRENT";
        rows.push_back(r);
    }
    list_.draw(g, CH_ROWS, rows, 25, 38);
}

void ChoiceScreen::action(uint16_t id)
{
    const int i = id - CH_ROWS;
    if (i < 0 || i >= (int)options_.size())
        return;
    saveSetting(e_.key, options_[i]);
    ui.pop();
}
