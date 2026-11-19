// Nozzle rack (Pi): slots with name / sensor / on the head; change, rename, tip offset, teach the rack
#include "screens.h"

NozzlesScreen nozzlesScreen;

enum
{
    NZ_ROWS = 200, // + row: the slots, then put back, teach first, teach last, heights
};

static std::string slotTitle(int num, const std::string &name)
{
    return std::to_string(num) + "  " + (name.empty() ? std::string("(no name)") : name);
}

void NozzlesScreen::poll()
{
    waiting_ = true;
    ui.send(
        "NOZZLES",
        [this](const Reply &r) {
            waiting_ = false;
            if (!r.ok)
                return;
            std::vector<Slot> list;
            for (auto &rec : r.records)
            {
                if (rec.type == "rack")
                    mounted_ = (int)rec.integer("mounted");
                if (rec.type != "nozzle")
                    continue;
                Slot s;
                s.num = (int)rec.integer("num");
                s.name = rec.str("name");
                s.dx = rec.num("dx"), s.dy = rec.num("dy"), s.dz = rec.num("dz"), s.x = rec.num("x"), s.y = rec.num("y");
                s.present = (int)rec.integer("present", -1);
                s.mounted = rec.flag("mounted");
                list.push_back(s);
            }
            slots_ = std::move(list);
            ui.redraw();
        },
        false, false);
}

void NozzlesScreen::tick()
{
    // A nozzle taken out by hand changes its sensor: look again every second
    if (!waiting_ && !busy_ && millis() - lastPoll_ > 1000)
    {
        lastPoll_ = millis();
        poll();
    }
}

void NozzlesScreen::draw(Gfx &g)
{
    g.background();
    std::string on = "none";
    for (auto &s : slots_)
        if (s.mounted)
            on = s.name.empty() ? "slot " + std::to_string(s.num) : s.name;
    const std::string right = busy_ ? std::string("changing ...") : "on head: " + on;
    g.titleBar("Nozzles", true, right.c_str(), busy_ ? th::ACCENT : th::DIM);
    std::vector<Row> rows;
    for (auto &s : slots_)
    {
        Row r;
        r.icon = s.present == 1 ? Icon::Check : s.mounted ? Icon::Move : Icon::None;
        r.iconColor = s.present == 1 ? th::OK : s.mounted ? th::ACCENT : th::DIM;
        r.label = slotTitle(s.num, s.name);
        r.sub = s.mounted ? "on the head" : s.present == 1 ? "in the rack" : s.present == 0 ? "slot empty" : "sensor unknown";
        if (s.dx != 0 || s.dy != 0)
        {
            char t[40];
            snprintf(t, sizeof(t), " · offset %.2f / %.2f", s.dx, s.dy);
            r.sub += t;
        }
        if (s.dz != 0)
        {
            char t[32];
            snprintf(t, sizeof(t), " · height %+.2f", s.dz);
            r.sub += t;
        }
        if (s.mounted)
            r.pill = "ON HEAD";
        r.chev = true;
        rows.push_back(r);
    }
    if (slots_.empty())
    {
        Row r;
        r.icon = Icon::Warn, r.label = "No nozzle rack";
        r.sub = "set the number of slots: Settings, nozzle_count";
        rows.push_back(r);
    }
    Row put, touchRow, first, last, heights;
    put.icon = Icon::Back, put.label = "Put the nozzle back";
    put.sub = mounted_ ? "into its slot, the head stays empty" : "nothing on the head";
    touchRow.icon = Icon::Sensor, touchRow.label = "Touch off on the load cell";
    touchRow.sub = machine.setting("loadcell", "0") == "1" ? "measures the tip height of the nozzle on the head"
                                                         : "no load cell (Settings > Load cell)";
    first.icon = Icon::Move, first.label = "Teach slot 1", first.sub = "jog the nozzle holder over slot 1";
    last.icon = Icon::Move, last.label = "Teach the last slot", last.sub = "the slots between are spaced evenly";
    heights.icon = Icon::Gear, heights.label = "Push-on / park height", heights.chev = true;
    heights.sub = "seesaw Z " + machine.setting("nozzle_z_pick", "?") + " / " + machine.setting("nozzle_z_drop", "?");
    rows.push_back(put);
    rows.push_back(touchRow);
    rows.push_back(first);
    rows.push_back(last);
    rows.push_back(heights);
    list_.draw(g, NZ_ROWS, rows);
}

void NozzlesScreen::touch()
{
    busy_ = true;
    ui.redraw();
    ui.send(
        "NOZZLE TOUCH",
        [this](const Reply &r) {
            busy_ = false;
            if (r.ok && !r.records.empty())
            {
                char t[64];
                snprintf(t, sizeof(t), "Tip height measured: reach %.2f mm", r.records[0].num("reach"));
                ui.toast(t, th::OK);
            }
            poll();
        },
        true, true);
}

void NozzlesScreen::change(int slot)
{
    busy_ = true;
    ui.redraw();
    ui.send(
        "NOZZLE CHANGE " + std::to_string(slot),
        [this, slot](const Reply &r) {
            busy_ = false;
            if (r.ok)
                ui.toast(slot ? "Nozzle " + std::to_string(slot) + " is on the head" : "Nozzle put back");
            poll();
        },
        true, true);
}

static void editOffset(const NozzlesScreen::Slot s)
{
    NumberEditor::Spec x;
    x.title = "Offset X · nozzle " + std::to_string(s.num);
    x.what = "tip offset from the holder centre";
    x.unit = "mm";
    x.value = s.dx, x.min = -5, x.max = 5, x.decimals = 2, x.steps = {0.01f, 0.1f, 1};
    numberEditor.open(x, [s](float vx) {
        ui.post([s, vx] {
            NumberEditor::Spec y;
            y.title = "Offset Y · nozzle " + std::to_string(s.num);
            y.what = "tip offset from the holder centre";
            y.unit = "mm";
            y.value = s.dy, y.min = -5, y.max = 5, y.decimals = 2, y.steps = {0.01f, 0.1f, 1};
            numberEditor.open(y, [s, vx](float vy) {
                char c[64];
                snprintf(c, sizeof(c), "NOZZLE %d dx=%.3f dy=%.3f", s.num, vx, vy);
                ui.send(c);
            });
        });
    });
}

static void editHeights()
{
    NumberEditor::Spec z;
    z.title = "Push-on height";
    z.what = "seesaw Z that pushes a nozzle on (- = nozzle side down)";
    z.unit = "mm";
    z.value = (float)atof(machine.setting("nozzle_z_pick", "-8").c_str()), z.min = -50, z.max = 50, z.decimals = 2;
    z.steps = {0.1f, 1, 5};
    numberEditor.open(z, [](float v) {
        char c[48];
        snprintf(c, sizeof(c), "SET nozzle_z_pick %.2f", v);
        ui.send(c);
        ui.post([] {
            NumberEditor::Spec d;
            d.title = "Park height";
            d.what = "seesaw Z at which a nozzle sits in its slot";
            d.unit = "mm";
            d.value = (float)atof(machine.setting("nozzle_z_drop", "-8").c_str()), d.min = -50, d.max = 50, d.decimals = 2;
            d.steps = {0.1f, 1, 5};
            numberEditor.open(d, [](float v2) {
                char c2[48];
                snprintf(c2, sizeof(c2), "SET nozzle_z_drop %.2f", v2);
                ui.send(c2);
            });
        });
    });
}

static void teachSlot(bool first)
{
    teachScreen.openXY(first ? "Rack: slot 1" : "Rack: last slot", [first](float x, float y) {
        char c[64];
        snprintf(c, sizeof(c), "SET nozzle_%s_x %.2f", first ? "first" : "last", x);
        ui.send(c);
        snprintf(c, sizeof(c), "SET nozzle_%s_y %.2f", first ? "first" : "last", y);
        ui.send(c);
    });
}

void NozzlesScreen::action(uint16_t id)
{
    if (id < NZ_ROWS || busy_)
        return;
    const int i = id - NZ_ROWS;
    const int n = (int)slots_.size();
    const int base = slots_.empty() ? 1 : n; // the "no rack" row stands in for the slots
    if (i < n)
    {
        const Slot s = slots_[i];
        dialog.open(slotTitle(s.num, s.name),
                    s.mounted ? "This nozzle is on the head." : s.present == 1 ? "In the rack." : "The slot is empty.",
                    "Pick it up, rename it or set its tip offset.",
                    {{"Name", Kind::Normal,
                      [s] {
                          textInput.open("Nozzle " + std::to_string(s.num), s.name, false, 15, [s](const std::string &t) {
                              std::string clean;
                              for (char c : t)
                                  if (c != ' ' && c != '"')
                                      clean += c;
                              ui.send("NOZZLE " + std::to_string(s.num) + " name=" + clean);
                          });
                      }},
                     {"Offset", Kind::Normal, [s] { editOffset(s); }},
                     {"Pick up", Kind::Primary, [this, s] { change(s.num); }}},
                    Icon::Check, th::ACCENT, 2);
        return;
    }
    switch (i - base)
    {
    case 0:
        if (mounted_)
            change(0);
        break;
    case 1:
        touch();
        break;
    case 2:
        teachSlot(true);
        break;
    case 3:
        teachSlot(false);
        break;
    case 4:
        editHeights();
        break;
    }
}

// ---------------------------------------------------------------- sensor order
NozzleLearnScreen nozzleLearnScreen;

void NozzleLearnScreen::apply(const Reply &r)
{
    waiting_ = false;
    failed_ = !r.ok || r.records.empty();
    if (failed_)
    {
        active_ = false;
        ui.redraw();
        return;
    }
    const Record &l = r.records[0];
    active_ = l.flag("active");
    next_ = (int)l.integer("next");
    count_ = (int)l.integer("count");
    two_ = l.str("step") == "two";
    bits_ = l.str("bits");
    if (l.str("step") == "finished")
    {
        done_ = true;
        machine.refreshSettings();
    }
    ui.redraw();
}

void NozzleLearnScreen::enter()
{
    done_ = two_ = failed_ = active_ = false;
    next_ = count_ = 0;
    bits_.clear();
    lastPoll_ = millis();
    waiting_ = true;
    ui.send("NOZZLE LEARN START", [this](const Reply &r) { apply(r); }, false, false);
}

void NozzleLearnScreen::tick()
{
    if (!active_ || waiting_ || millis() - lastPoll_ < 250)
        return;
    lastPoll_ = millis();
    waiting_ = true;
    ui.send("NOZZLE LEARN", [this](const Reply &r) { apply(r); }, false, false);
}

bool NozzleLearnScreen::back()
{
    if (active_)
        ui.send("NOZZLE LEARN CANCEL");
    return true;
}

void NozzleLearnScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Nozzle sensor order");
    g.card(8, 40, 304, 150);
    char t[48];
    if (failed_)
    {
        g.text("Tool changer not heard", 160, 90, Font::Body, th::WARN, Align::Center);
        g.text("check the wire and nozzle_count", 160, 122, Font::Body, th::DIM, Align::Center);
    }
    else if (done_)
    {
        g.text("Saved", 160, 80, Font::Big, th::OK, Align::Center);
        snprintf(t, sizeof(t), "slot 1..%d = bit %s", count_, bits_.c_str());
        g.text(t, 160, 122, Font::Body, th::TEXT, Align::Center);
    }
    else if (active_)
    {
        snprintf(t, sizeof(t), "Trigger slot %d of %d", next_, count_);
        g.text(t, 160, 80, Font::Big, th::TEXT, Align::Center);
        g.text(two_ ? "Two changed at once: trigger it again" : "nozzle in or out, or a finger on the sensor", 160, 122, Font::Body,
               two_ ? th::WARN : th::DIM, Align::Center);
        if (!bits_.empty())
            g.text("bits so far: " + bits_, 160, 152, Font::Body, th::DIM, Align::Center);
    }
    else
        g.text("...", 160, 100, Font::Big, th::DIM, Align::Center);
}
