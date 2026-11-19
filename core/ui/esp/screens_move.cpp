// Move (jog) and teaching a position (pocket X/Y, feeder E1)
#include "screens.h"

MoveScreen moveScreen;
TeachScreen teachScreen;

static const float STEPS[] = {0.1f, 1, 10, 50};
static const char *STEP_LABELS[] = {"0.1", "1", "10", "50"};
static const float TEACH_STEPS[] = {0.01f, 0.1f, 1, 10};
static const char *TEACH_LABELS[] = {"0.01", "0.1", "1", "10"};
static const float JOG_SPEED = 30; // mm/s (Z is capped by the controller)
static const char *AXES[] = {"X", "Y", "Z", "E1", "E0"};

static void sendJog(int axis, float d)
{
    char cmd[48];
    if (axis == 4)
        snprintf(cmd, sizeof(cmd), "E0 %.2f", d);
    else
        snprintf(cmd, sizeof(cmd), "JOG %s=%.3f speed=%.0f", axis == 3 ? "e1" : axis == 0 ? "x" : axis == 1 ? "y" : "z", d, JOG_SPEED);
    ui.send(cmd);
}

// ---------------------------------------------------------------- move

enum
{
    MV_AXIS = 1, // + axis (5)
    MV_YP = 20,
    MV_XM,
    MV_HALT,
    MV_XP,
    MV_YM,
    MV_ZP,
    MV_ZM,
    MV_STEP = 30, // + index
    MV_HOME = 40,
    MV_E1ZERO
};

void MoveScreen::jog(int axis, float dir)
{
    sendJog(axis, dir * STEPS[step_]);
}

bool MoveScreen::turn(int8_t steps)
{
    jog(axis_, steps);
    return true;
}

bool MoveScreen::press()
{
    axis_ = (axis_ + 1) % 5;
    return true;
}

void MoveScreen::draw(Gfx &g)
{
    const MachineStatus s = machine.status();
    char t[64];
    snprintf(t, sizeof(t), axis_ == 4 ? "step %s deg" : "step %s mm", STEP_LABELS[step_]);
    g.background();
    g.titleBar("Move", true, t);
    const float v[5] = {s.x, s.y, s.z, s.e1, s.e0};
    for (int i = 0; i < 5; i++)
    {
        snprintf(t, sizeof(t), i == 4 ? "%.0f°" : i == 3 ? "%.1f" : "%.2f", v[i]);
        g.valueBox(MV_AXIS + i, AXES[i], t, 8 + i * 61, 32, 58, 40, th::TEXT, i == axis_);
    }
    if (axis_ <= 1)
        snprintf(t, sizeof(t), "stick: move X / Y · press: next axis");
    else
        snprintf(t, sizeof(t), "stick up / down: move %s · press: next axis", AXES[axis_]);
    g.text(t, 160, 84, Font::Small, th::DIM, Align::Center);

    const int cx = 80, cy = 152;
    g.button(MV_YP, "Y+", cx - 22, cy - 62, 44, 36);
    g.button(MV_XM, "X-", cx - 70, cy - 18, 44, 36);
    g.button(MV_HALT, "", cx - 18, cy - 14, 36, 28, Kind::Normal, Icon::Stop);
    g.button(MV_XP, "X+", cx + 26, cy - 18, 44, 36);
    g.button(MV_YM, "Y-", cx - 22, cy + 26, 44, 36);
    // Right column: Z, or the selected E axis.
    const char *side = axis_ >= 2 ? AXES[axis_] : "Z";
    std::string p = std::string(side) + "+", m = std::string(side) + "-";
    g.button(MV_ZP, p.c_str(), 160, 90, 44, 36);
    g.button(MV_ZM, m.c_str(), 160, 134, 44, 36);
    for (int i = 0; i < 4; i++)
        g.button(MV_STEP + i, STEP_LABELS[i], 216 + (i % 2) * 50, 90 + (i / 2) * 44, 44, 36, i == step_ ? Kind::Primary : Kind::Normal);
    g.button(MV_HOME, "Home", 160, 182, 72, 34, Kind::Normal, Icon::Home);
    g.button(MV_E1ZERO, "E1 = 0", 240, 182, 72, 34);
}

void MoveScreen::action(uint16_t id)
{
    if (id >= MV_AXIS && id < MV_AXIS + 5)
        axis_ = id - MV_AXIS;
    else if (id >= MV_STEP && id < MV_STEP + 4)
        step_ = id - MV_STEP;
    else
        switch (id)
        {
        case MV_YP:
            jog(1, 1);
            break;
        case MV_YM:
            jog(1, -1);
            break;
        case MV_XP:
            jog(0, 1);
            break;
        case MV_XM:
            jog(0, -1);
            break;
        case MV_ZP:
            jog(axis_ >= 2 ? axis_ : 2, 1);
            break;
        case MV_ZM:
            jog(axis_ >= 2 ? axis_ : 2, -1);
            break;
        case MV_HALT:
            ui.send("HALT");
            break;
        case MV_HOME:
            homingScreen.start();
            break;
        case MV_E1ZERO:
            ui.send("SETPOS e1=0", [](const Reply &r) {
                if (r.ok)
                    ui.toast("E1 = 0 here");
            });
            break;
        }
}

// ---------------------------------------------------------------- teach

enum
{
    T_BOX = 1, // + 0/1
    T_MINUS0 = 10,
    T_PLUS0,
    T_MINUS1,
    T_PLUS1,
    T_STEP = 20, // + index
    T_CANCEL = 30,
    T_SAVE
};

void TeachScreen::openXY(const std::string &title, std::function<void(float, float)> onSave)
{
    e1_ = false;
    axis_ = 0;
    step_ = 1;
    title_ = title;
    onSaveXY_ = onSave;
    ui.push(this);
}

void TeachScreen::openE1(const std::string &title, std::function<void(float)> onSave)
{
    e1_ = true;
    axis_ = 0;
    step_ = 2;
    title_ = title;
    onSaveE1_ = onSave;
    ui.push(this);
}

void TeachScreen::jog(int axis, int dir)
{
    sendJog(e1_ ? 3 : axis, dir * TEACH_STEPS[step_]);
}

bool TeachScreen::turn(int8_t steps)
{
    jog(axis_, steps);
    return true;
}

bool TeachScreen::press()
{
    if (!e1_)
        axis_ ^= 1;
    else
        step_ = (step_ + 1) % 4;
    return true;
}

void TeachScreen::draw(Gfx &g)
{
    const MachineStatus s = machine.status();
    char t[40];
    snprintf(t, sizeof(t), "step %s mm", TEACH_LABELS[step_]);
    g.background();
    g.titleBar(title_.c_str(), true, t);
    if (e1_)
    {
        snprintf(t, sizeof(t), "%.2f", s.e1);
        g.valueBox(T_BOX, "E1 carriage", t, 8, 32, 304, 48, th::TEXT, true);
        g.text("Stick: move E1 until the feeder motor engages · press: step", 160, 92, Font::Small, th::DIM, Align::Center);
        g.button(T_MINUS0, "E1-", 8, 104, 148, 40);
        g.button(T_PLUS0, "E1+", 164, 104, 148, 40);
    }
    else
    {
        snprintf(t, sizeof(t), "%.2f", s.x);
        g.valueBox(T_BOX, "X", t, 8, 32, 148, 48, th::TEXT, axis_ == 0);
        snprintf(t, sizeof(t), "%.2f", s.y);
        g.valueBox(T_BOX + 1, "Y", t, 164, 32, 148, 48, th::TEXT, axis_ == 1);
        g.text("Stick: move X / Y · hold: cancel", 160, 92,
               Font::Small, th::DIM, Align::Center);
        g.button(T_MINUS0, "X-", 8, 104, 70, 40);
        g.button(T_PLUS0, "X+", 86, 104, 70, 40);
        g.button(T_MINUS1, "Y-", 164, 104, 70, 40);
        g.button(T_PLUS1, "Y+", 242, 104, 70, 40);
    }
    for (int i = 0; i < 4; i++)
        g.button(T_STEP + i, TEACH_LABELS[i], 8 + i * 78, 152, 70, 30, i == step_ ? Kind::Primary : Kind::Normal, Icon::None, Font::Small);
    g.button(T_CANCEL, "Cancel", 8, 192, 148, 38);
    g.button(T_SAVE, "Save here", 164, 192, 148, 38, Kind::Primary, Icon::Check);
}

void TeachScreen::action(uint16_t id)
{
    if (id == T_BOX || id == T_BOX + 1)
        axis_ = e1_ ? 0 : id - T_BOX;
    else if (id >= T_STEP && id < T_STEP + 4)
        step_ = id - T_STEP;
    else if (id == T_MINUS0 || id == T_PLUS0)
        jog(0, id == T_PLUS0 ? 1 : -1);
    else if (id == T_MINUS1 || id == T_PLUS1)
        jog(1, id == T_PLUS1 ? 1 : -1);
    else if (id == T_CANCEL)
        ui.pop();
    else if (id == T_SAVE)
    {
        // Wait until the moves are done, then read the exact position.
        ui.send("WAIT", [this](const Reply &w) {
            if (!w.ok)
                return;
            ui.send("POS", [this](const Reply &r) {
                if (!r.ok || r.records.empty())
                    return;
                const Record &p = r.records[0];
                ui.pop();
                if (e1_ && onSaveE1_)
                    onSaveE1_(p.num("e1"));
                else if (!e1_ && onSaveXY_)
                    onSaveXY_(p.num("x"), p.num("y"));
            });
        });
    }
}
