// Feeders, feeder, pockets, pocket, auto detect
#include "screens.h"

FeedersScreen feedersScreen;
FeederScreen feederScreen;
PocketsScreen pocketsScreen;
PocketScreen pocketScreen;
ScanScreen scanScreen;

static std::string num2(int n)
{
    char t[8];
    snprintf(t, sizeof(t), "%02d", n);
    return t;
}

// FEEDER SET / POCKET ...: then reload the feeder screen and the feeder list.
static void feederCmd(const std::string &cmd, const char *okToast = "Saved to the SD card")
{
    ui.send(cmd, [okToast](const Reply &r) {
        if (!r.ok)
            return;
        feederScreen.load();
        machine.refreshFeeders();
        if (okToast)
            ui.toast(okToast);
    });
}

// ---------------------------------------------------------------- feeders

enum
{
    F_ROWS = 10
};

void FeedersScreen::draw(Gfx &g)
{
    const auto list = machine.feeders();
    char t[24];
    snprintf(t, sizeof(t), "%u feeder%s", (unsigned)list.size(), list.size() == 1 ? "" : "s");
    g.background();
    g.titleBar("Feeders", true, t);
    std::vector<Row> rows(2);
    rows[0].icon = Icon::Nfc, rows[0].iconColor = th::ACCENT, rows[0].label = "Auto detect";
    rows[0].sub = "home E1, find feeders, read their QR codes", rows[0].chev = true;
    rows[1].icon = Icon::Plus, rows[1].label = "Add feeder", rows[1].chev = true;
    for (auto &f : list)
    {
        Row r;
        r.label = num2(f.num) + "  " + f.name;
        char sub[64];
        snprintf(sub, sizeof(sub), "%s · E1 %.1f · pocket %d / %d", f.part.empty() ? "no part ID" : f.part.c_str(), f.pos,
                 f.next, f.pocketCount);
        r.sub = sub;
        r.chev = true;
        if (!f.pocketCount)
            r.pill = "NO POCKETS", r.pillColor = th::WARN, r.pillBg = th::WARN_BG;
        rows.push_back(r);
    }
    list_.draw(g, F_ROWS, rows, 25, 35);
}

void FeedersScreen::action(uint16_t id)
{
    const int i = id - F_ROWS;
    if (i == 0)
        dialog.open("Auto detect", "E1 drives along the rail; the top camera", "reads every QR code. Rail and bed clear?",
                    {{"Cancel", Kind::Normal, nullptr}, {"Start", Kind::Primary, [] { scanScreen.start(); }}}, Icon::Nfc, th::ACCENT, 1);
    else if (i == 1)
        ui.send("FEEDER ADD", [](const Reply &r) {
            if (!r.ok || r.records.empty())
                return;
            const int n = r.records[0].integer("num");
            machine.refreshFeeders();
            feederScreen.open(n);
            ui.toast("Feeder " + num2(n) + " added");
        });
    else
    {
        const auto list = machine.feeders();
        if (i - 2 < (int)list.size())
            feederScreen.open(list[i - 2].num);
    }
}

// ---------------------------------------------------------------- feeder

enum
{
    FD_ROWS = 10
};

void FeederScreen::open(int num)
{
    f_ = FeederInfo();
    f_.num = num;
    loaded_ = false;
    list_.reset();
    ui.push(this);
}

void FeederScreen::load()
{
    const int n = f_.num;
    ui.send("FEEDER " + std::to_string(n), [this, n](const Reply &r) {
        if (!r.ok || n != f_.num)
            return;
        for (auto &rec : r.records)
        {
            if (rec.type == "feeder")
            {
                f_ = feederFrom(rec);
                f_.pockets.clear();
            }
            else if (rec.type == "pocket")
                f_.pockets.push_back({rec.num("x"), rec.num("y")});
        }
        loaded_ = true;
    });
}

void FeederScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar(("Feeder " + num2(f_.num)).c_str(), true, f_.name.c_str());
    if (!loaded_)
    {
        g.text("Loading ...", 160, 120, Font::Body, th::DIM, Align::Center);
        return;
    }
    char t[32];
    std::vector<Row> rows(8);
    rows[0].label = "Pockets", rows[0].value = std::to_string(f_.pocketCount), rows[0].chev = true;
    rows[1].label = "Next pocket";
    rows[1].value = f_.pocketCount ? std::to_string(f_.next) + " of " + std::to_string(f_.pocketCount) : "-";
    snprintf(t, sizeof(t), "%.2f mm", f_.pos);
    rows[2].label = "Feeder pos (E1)", rows[2].value = t, rows[2].chev = true;
    if (f_.teeth)
        snprintf(t, sizeof(t), "%d teeth (%.1f°)", f_.teeth, f_.advance);
    else
        snprintf(t, sizeof(t), "%.1f°", f_.advance);
    rows[3].label = "Advance", rows[3].value = t, rows[3].sub = "teeth of the feed wheel per advance";
    rows[4].label = "Part ID", rows[4].value = f_.part.empty() ? "-" : f_.part, rows[4].sub = "from the QR code · tap to read it now";
    rows[5].label = "Go to feeder", rows[5].sub = "E1 drives to the feeder";
    rows[6].label = "Test advance", rows[6].icon = Icon::Play, rows[6].iconColor = th::ACCENT;
    rows[7].label = "Delete feeder", rows[7].icon = Icon::Trash, rows[7].iconColor = th::BAD, rows[7].labelColor = th::BAD;
    list_.draw(g, FD_ROWS, rows, 25, 35);
}

void FeederScreen::action(uint16_t id)
{
    const std::string n = std::to_string(f_.num);
    switch (id - FD_ROWS)
    {
    case 0:
        ui.push(&pocketsScreen);
        break;
    case 1:
    {
        if (!f_.pocketCount)
        {
            ui.toast("Teach a pocket first", th::WARN);
            break;
        }
        NumberEditor::Spec s;
        s.title = "Next pocket · feeder " + num2(f_.num);
        s.what = "the pocket that is picked next";
        s.value = f_.next;
        s.min = 1;
        s.max = f_.pocketCount;
        s.decimals = 0;
        s.steps = {1};
        numberEditor.open(s, [n](float v) { feederCmd("FEEDER SET " + n + " next=" + std::to_string((int)v)); });
        break;
    }
    case 2:
        teachScreen.openE1("Feeder pos · " + num2(f_.num), [n](float e1) {
            char c[48];
            snprintf(c, sizeof(c), "FEEDER SET %s pos=%.2f", n.c_str(), e1);
            feederCmd(c);
        });
        break;
    case 3:
    {
        const int wheel = atoi(machine.setting("feeder_wheel_teeth", "20").c_str());
        NumberEditor::Spec s;
        s.title = "Advance · feeder " + num2(f_.num);
        s.what = "teeth per advance (wheel: " + std::to_string(wheel > 0 ? wheel : 20) + " teeth)";
        s.value = f_.teeth ? f_.teeth : 1;
        s.unit = "teeth";
        s.min = 1;
        s.max = 100;
        s.decimals = 0;
        s.steps = {1, 5};
        numberEditor.open(s, [n](float v) { feederCmd("FEEDER SET " + n + " teeth=" + std::to_string((int)v)); });
        break;
    }
    case 4:
        ui.toast("Reading the QR code ...", th::ACCENT);
        ui.send("FEEDER QR " + n, [](const Reply &r) {
            if (r.ok)
                ui.toast("QR code read");
            machine.refreshFeeders();
        });
        break;
    case 5:
        ui.send("FEEDER GOTO " + n);
        break;
    case 6:
        ui.send("FEEDER ADVANCE " + n, [](const Reply &r) {
            if (r.ok)
                ui.toast("Advancing");
        });
        break;
    case 7:
        dialog.open("Delete feeder " + num2(f_.num) + "?", "/feeders/feeder" + num2(f_.num) + ".txt is removed", "from the SD card.",
                    {{"No", Kind::Normal, nullptr},
                     {"Delete", Kind::Danger, [n] {
                          ui.send("FEEDER DELETE " + n, [](const Reply &r) {
                              if (!r.ok)
                                  return;
                              machine.refreshFeeders();
                              ui.pop();
                              ui.toast("Feeder deleted");
                          });
                      }}},
                    Icon::Trash, th::BAD, 0);
        break;
    }
}

// ---------------------------------------------------------------- pockets

enum
{
    P_ROWS = 10
};

void PocketsScreen::draw(Gfx &g)
{
    const FeederInfo &f = feederScreen.info();
    g.background();
    g.titleBar(("Pockets · feeder " + num2(f.num)).c_str(), true, ("next: " + std::to_string(f.next)).c_str());
    std::vector<Row> rows;
    for (size_t i = 0; i < f.pockets.size(); i++)
    {
        Row r;
        r.label = std::to_string(i + 1);
        char sub[48];
        snprintf(sub, sizeof(sub), "X %.2f  Y %.2f", f.pockets[i].x, f.pockets[i].y);
        r.sub = sub;
        r.chev = true;
        if ((int)i + 1 == f.next)
            r.pill = "NEXT";
        rows.push_back(r);
    }
    Row add;
    add.icon = Icon::Plus;
    add.label = "Teach new pocket";
    add.sub = "jog the nozzle there and save";
    add.chev = true;
    rows.push_back(add);
    list_.draw(g, P_ROWS, rows, 25, 40);
}

void PocketsScreen::action(uint16_t id)
{
    const FeederInfo &f = feederScreen.info();
    const int i = id - P_ROWS;
    const std::string n = std::to_string(f.num);
    if (i == (int)f.pockets.size())
        teachScreen.openXY("Teach pocket " + std::to_string(i + 1), [n](float x, float y) {
            char c[64];
            snprintf(c, sizeof(c), "POCKET %s ADD x=%.2f y=%.2f", n.c_str(), x, y);
            feederCmd(c, "Pocket saved");
        });
    else if (i >= 0 && i < (int)f.pockets.size())
        pocketScreen.open(i + 1);
}

// ---------------------------------------------------------------- pocket

enum
{
    PK_ROWS = 10
};

void PocketScreen::open(int index)
{
    i_ = index;
    ui.push(this);
}

void PocketScreen::draw(Gfx &g)
{
    const FeederInfo &f = feederScreen.info();
    g.background();
    g.titleBar(("Pocket " + std::to_string(i_) + " · feeder " + num2(f.num)).c_str());
    if (i_ > (int)f.pockets.size())
    {
        g.text("Pocket deleted", 160, 120, Font::Body, th::DIM, Align::Center);
        return;
    }
    const Pocket p = f.pockets[i_ - 1];
    char t[24];
    std::vector<Row> rows(6);
    snprintf(t, sizeof(t), "%.2f mm", p.x);
    rows[0].label = "X", rows[0].value = t;
    snprintf(t, sizeof(t), "%.2f mm", p.y);
    rows[1].label = "Y", rows[1].value = t;
    rows[2].label = "Move here", rows[2].icon = Icon::Move, rows[2].sub = "nozzle over the pocket, current height";
    rows[3].label = "Teach again", rows[3].icon = Icon::Touch, rows[3].chev = true;
    rows[4].label = "Pick this one next", rows[4].icon = Icon::Check;
    if (f.next == i_)
        rows[4].pill = "NEXT";
    rows[5].label = "Delete pocket", rows[5].icon = Icon::Trash, rows[5].iconColor = th::BAD, rows[5].labelColor = th::BAD;
    list_.draw(g, PK_ROWS, rows, 25, 35);
}

void PocketScreen::action(uint16_t id)
{
    const FeederInfo &f = feederScreen.info();
    if (i_ > (int)f.pockets.size())
        return;
    const Pocket p = f.pockets[i_ - 1];
    const std::string n = std::to_string(f.num), i = std::to_string(i_);
    auto setXY = [n, i](float x, float y) {
        char c[64];
        snprintf(c, sizeof(c), "POCKET %s SET %s x=%.2f y=%.2f", n.c_str(), i.c_str(), x, y);
        feederCmd(c);
    };
    switch (id - PK_ROWS)
    {
    case 0:
    case 1:
    {
        const bool isX = id == PK_ROWS;
        NumberEditor::Spec s;
        s.title = std::string(isX ? "X" : "Y") + " · pocket " + i;
        s.what = "machine position of the pocket (nozzle)";
        s.value = isX ? p.x : p.y;
        s.unit = "mm";
        s.min = -50;
        s.max = 500;
        s.decimals = 2;
        s.steps = {0.01f, 0.1f, 1, 10};
        s.step = 1;
        numberEditor.open(s, [setXY, p, isX](float v) { isX ? setXY(v, p.y) : setXY(p.x, v); });
        break;
    }
    case 2:
        ui.send("POCKET " + n + " GOTO " + i);
        break;
    case 3:
        teachScreen.openXY("Teach pocket " + i, setXY);
        break;
    case 4:
        feederCmd("FEEDER SET " + n + " next=" + i);
        break;
    case 5:
        dialog.open("Delete pocket " + i + "?", "The pockets after it move up one.", "",
                    {{"No", Kind::Normal, nullptr},
                     {"Delete", Kind::Danger, [n, i] {
                          ui.pop();
                          feederCmd("POCKET " + n + " DELETE " + i, "Pocket deleted");
                      }}},
                    Icon::Trash, th::BAD, 0);
        break;
    }
}

// ---------------------------------------------------------------- auto detect

enum
{
    S_STOP = 1,
    S_OK,
    S_ROWS = 10
};

void ScanScreen::start()
{
    started_ = true;
    startedAt_ = millis();
    machine.resetScan();
    list_.reset();
    ui.push(this);
    ui.send("SCAN");
}

bool ScanScreen::back()
{
    const ScanInfo s = machine.scan();
    return !s.active && millis() - startedAt_ > 1500;
}

void ScanScreen::draw(Gfx &g)
{
    const ScanInfo sc = machine.scan();
    const bool starting = !sc.active && sc.state.empty() && millis() - startedAt_ < 3000;
    const bool running = sc.active || starting;
    char t[48];
    snprintf(t, sizeof(t), "%u found", (unsigned)sc.found.size());
    g.background();

    if (running && sc.state == "reading")
    {
        g.titleBar("Auto detect", false, t);
        g.disc(160, 74, 30, th::ACCENT_BG);
        g.icon(Icon::Nfc, 155, 67, th::ACCENT);
        g.text("Reading the QR code ...", 160, 124, Font::Big, th::TEXT, Align::Center);
        snprintf(t, sizeof(t), "feeder switch hit at E1 %.2f mm", sc.e1);
        g.text(t, 160, 144, Font::Small, th::DIM, Align::Center);
        g.button(S_STOP, "Stop", 96, 186, 128, 34, Kind::Danger, Icon::Stop);
        return;
    }
    if (running)
    {
        g.titleBar("Auto detect", false, t);
        g.card(8, 32, 304, 88);
        const bool homing = sc.state == "homing" || starting;
        const int w = g.pill(homing ? "HOMING E1" : "SEARCHING", 18, 42, th::ACCENT, th::ACCENT_BG);
        g.text(homing ? "E1 looks for its home switch" : "E1 drives slowly along the rail", 18 + w + 8, 50, Font::Small, th::DIM);
        snprintf(t, sizeof(t), "E1  %.1f mm", sc.e1);
        g.text(t, 18, 76, Font::Huge, th::TEXT);
        g.progress(18, 100, 284, 6, sc.e1 / 500.0f);
        g.text("0", 18, 113, Font::Small, th::DIM);
        g.text("500 mm", 302, 113, Font::Small, th::DIM, Align::Right);
        std::vector<Row> rows;
        for (auto &f : sc.found)
        {
            Row r;
            r.icon = Icon::Check;
            r.iconColor = th::OK;
            r.label = num2(f.num) + "  " + f.name;
            snprintf(t, sizeof(t), "%.2f mm · %s", f.pos, f.tag ? "QR read" : "no QR read");
            r.sub = t;
            rows.push_back(r);
        }
        list_.draw(g, S_ROWS, rows, 124, 32, 190);
        g.button(S_STOP, "Stop", 96, 196, 128, 34, Kind::Danger, Icon::Stop);
        return;
    }

    // Results
    const bool failed = sc.state == "failed";
    g.titleBar(failed ? "Auto detect: failed" : "Auto detect: done", true, t, failed ? th::BAD : th::OK);
    std::vector<Row> rows;
    for (auto &f : sc.found)
    {
        Row r;
        r.label = num2(f.num) + "  " + f.name;
        snprintf(t, sizeof(t), "%.2f mm", f.pos);
        r.sub = t;
        if (!f.tag)
            r.pill = "NO QR", r.pillColor = th::WARN, r.pillBg = th::WARN_BG;
        else if (f.isNew)
            r.pill = "NEW";
        else
            r.pill = "UPDATED", r.pillColor = th::DIM, r.pillBg = th::PANEL2;
        rows.push_back(r);
    }
    for (auto &f : machine.feeders())
    {
        bool seen = false;
        for (auto &x : sc.found)
            seen |= x.num == f.num;
        if (seen)
            continue;
        Row r;
        r.label = num2(f.num) + "  " + f.name;
        r.labelColor = th::DIM;
        r.sub = "not found on the rail";
        rows.push_back(r);
    }
    if (failed)
    {
        Row r;
        r.icon = Icon::Warn;
        r.iconColor = th::BAD;
        r.label = sc.error.empty() ? "stopped with an error" : sc.error;
        r.labelColor = th::BAD;
        rows.insert(rows.begin(), r);
    }
    list_.draw(g, S_ROWS, rows, 25, 34, 196);
    g.button(S_OK, "OK", 96, 202, 128, 30, Kind::Primary);
}

void ScanScreen::action(uint16_t id)
{
    if (id == S_STOP)
        ui.send("SCAN STOP");
    else if (id == S_OK)
        ui.pop();
}
