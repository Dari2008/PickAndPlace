#include "widgets.h"
#include <cmath>

Dialog dialog;
NumberEditor numberEditor;
TextInput textInput;

std::string fmtNum(float v, int decimals)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

// ---------------------------------------------------------------- dialog

void Dialog::open(const std::string &title, const std::string &line1, const std::string &line2,
                  std::vector<DialogButton> buttons, Icon icon, uint16_t iconColor, int focusButton)
{
    title_ = title;
    line1_ = line1;
    line2_ = line2;
    buttons_ = std::move(buttons);
    icon_ = icon;
    iconColor_ = iconColor;
    focus_ = focusButton;
    tag.clear();
    if (ui.top() == this)
        ui.setFocus(defaultFocus());
    else
        ui.push(this);
}

void Dialog::draw(Gfx &g)
{
    const int x = 24, w = 320 - 48, h = 132, y = (240 - h) / 2;
    g.rrect(x, y, w, h, 12, th::PANEL);
    g.rframe(x, y, w, h, 12, th::LINE);
    int tx = x + 16;
    if (icon_ != Icon::None)
    {
        g.icon(icon_, tx, y + 14, iconColor_);
        tx += 22;
    }
    g.text(title_, tx, y + 21, Font::Title, th::TEXT);
    g.text(line1_, x + 16, y + 46, Font::Body, th::TEXT);
    g.text(line2_, x + 16, y + 62, Font::Body, th::DIM);
    const int n = buttons_.size();
    const int bw = (w - 32 - (n - 1) * 10) / n;
    for (int i = 0; i < n; i++)
        g.button(1 + i, buttons_[i].label.c_str(), x + 16 + i * (bw + 10), y + h - 44, bw, 32, buttons_[i].kind);
}

void Dialog::action(uint16_t id)
{
    if (id < 1 || id > buttons_.size())
        return;
    auto fn = buttons_[id - 1].fn;
    ui.pop();
    if (fn)
        fn();
}

// ---------------------------------------------------------------- number editor

enum
{
    NE_VALUE = 0,
    NE_MINUS = 1,
    NE_STEP0 = 2, // + index
    NE_PLUS = 10,
    NE_CANCEL = 11,
    NE_SAVE = 12
};

void NumberEditor::open(const Spec &spec, std::function<void(float)> onSave)
{
    spec_ = spec;
    if (spec_.step >= spec_.steps.size())
        spec_.step = 0;
    onSave_ = onSave;
    ui.push(this);
}

void NumberEditor::change(int8_t steps)
{
    float v = spec_.value + steps * spec_.steps[spec_.step];
    const float p = powf(10, spec_.decimals);
    v = roundf(v * p) / p;
    spec_.value = std::max(spec_.min, std::min(spec_.max, v));
}

bool NumberEditor::turn(int8_t steps)
{
    change(steps);
    return true;
}

bool NumberEditor::press()
{
    spec_.step = (spec_.step + 1) % spec_.steps.size();
    return true;
}

bool NumberEditor::hold()
{
    action(NE_SAVE);
    return true;
}

void NumberEditor::action(uint16_t id)
{
    if (id == NE_MINUS)
        change(-1);
    else if (id == NE_PLUS)
        change(1);
    else if (id >= NE_STEP0 && id < NE_STEP0 + spec_.steps.size())
        spec_.step = id - NE_STEP0;
    else if (id == NE_CANCEL)
        ui.pop();
    else if (id == NE_SAVE)
    {
        auto fn = onSave_;
        const float v = spec_.value;
        ui.pop();
        if (fn)
            fn(v);
    }
}

void NumberEditor::draw(Gfx &g)
{
    g.background();
    g.titleBar(spec_.title.c_str());
    g.card(8, 34, 304, 96);
    g.text(spec_.what, 160, 52, Font::Small, th::DIM, Align::Center);
    const bool unlimited = spec_.zeroIsUnlimited && spec_.value <= 0;
    const std::string v = unlimited ? "unlimited" : fmtNum(spec_.value, spec_.decimals);
    const int w = g.width(v.c_str(), unlimited ? Font::Huge : Font::Giant);
    const int uw = unlimited || spec_.unit.empty() ? 0 : g.width(spec_.unit.c_str(), Font::Big) + 6;
    const int x = 160 - (w + uw) / 2;
    g.text(v, x, 88, unlimited ? Font::Huge : Font::Giant, th::TEXT);
    if (uw)
        g.text(spec_.unit, x + w + 6, 82, Font::Big, th::DIM);
    char hint[64];
    snprintf(hint, sizeof(hint), "step %s · stick: change · press: step · hold: save",
             fmtNum(spec_.steps[spec_.step], spec_.decimals).c_str());
    g.text(hint, 160, 120, Font::Small, th::DIM, Align::Center);
    g.addHit(NE_VALUE, {8, 34, 304, 96}, false);

    g.button(NE_MINUS, "-", 8, 140, 70, 44, Kind::Normal, Icon::None, Font::Big);
    const int n = spec_.steps.size();
    const int cw = (320 - 16 - 2 * 78 - (n - 1) * 6) / n;
    for (int i = 0; i < n; i++)
        g.button(NE_STEP0 + i, fmtNum(spec_.steps[i], spec_.steps[i] < 1 ? (spec_.steps[i] < 0.1f ? 2 : 1) : 0).c_str(),
                 86 + i * (cw + 6), 140, cw, 44, i == spec_.step ? Kind::Primary : Kind::Normal);
    g.button(NE_PLUS, "+", 320 - 78, 140, 70, 44, Kind::Normal, Icon::None, Font::Big);
    g.button(NE_CANCEL, "Cancel", 8, 192, 148, 38);
    g.button(NE_SAVE, "Save", 164, 192, 148, 38, Kind::Primary, Icon::Check);
}

// ---------------------------------------------------------------- keyboard

enum
{
    K_SHIFT = 90, // page key left of row 2
    K_DEL,
    K_MODE,
    K_SPACE,
    K_OK,
    K_SHOW,
    K_KEY0 = 100 // + row * 10 + column
};

static const float KEY_STEP = 31.2f;
static const int KEY_W = 28, ROW_Y[] = {64, 104, 144}, ROW_H = 36;

void TextInput::open(const std::string &title, const std::string &text, bool secret, uint8_t maxLen,
                     std::function<void(const std::string &)> onDone)
{
    title_ = title;
    text_ = text;
    secret_ = secret;
    show_ = false;
    maxLen_ = maxLen;
    page_ = LOWER;
    onDone_ = onDone;
    ui.push(this);
}

const char *TextInput::row(uint8_t r) const
{
    static const char *ROWS[4][3] = {
        {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
        {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
        {"1234567890", "-/:;()$&@", ".,?!'\"%"},
        {"[]{}#^*+=_", "|~<>`$&@", ".,?!'\"%"},
    };
    return ROWS[page_][r];
}

uint8_t TextInput::animRects(Rect *r) const
{
    r[0] = {8, 30, 240, 28}; // text field with the cursor
    return 1;
}

void TextInput::draw(Gfx &g)
{
    g.background();
    g.titleBar(title_.c_str());

    // Text field: the end of the text when it doesn't fit, '*' while hidden.
    g.rrect(8, 30, 304, 28, 7, th::PANEL2);
    g.rframe(8, 30, 304, 28, 7, th::ACCENT);
    std::string shown = secret_ && !show_ ? std::string(text_.size(), '*') : text_;
    const int maxW = secret_ ? 222 : 284;
    bool cut = false;
    while (!shown.empty() && g.width(shown.c_str(), Font::Body) > maxW)
    {
        shown.erase(0, 1);
        cut = true;
    }
    if (cut)
        shown = "..." + shown;
    const int w = shown.empty() ? 0 : g.text(shown, 16, 44, Font::Body, th::TEXT);
    if ((millis() / 500) % 2 == 0)
        g.rect(17 + w, 36, 2, 16, th::ACCENT);
    if (shown.empty())
        g.text(secret_ ? "password" : "type here", 22, 44, Font::Body, th::DIM);
    if (secret_)
        g.button(K_SHOW, show_ ? "Hide" : "Show", 254, 33, 52, 22, Kind::Normal, Icon::None, Font::Small);

    // Three rows of characters.
    for (uint8_t r = 0; r < 3; r++)
    {
        const char *keys = row(r);
        const float x0 = r == 0 ? 4 : r == 1 ? 4 + KEY_STEP / 2 : 52;
        for (uint8_t c = 0; keys[c]; c++)
        {
            const char label[2] = {keys[c], 0};
            g.button(K_KEY0 + r * 10 + c, label, (int)(x0 + c * KEY_STEP), ROW_Y[r], KEY_W, ROW_H, Kind::Normal, Icon::None, Font::Row);
        }
    }
    static const char *PAGE_KEY[] = {"aA", "Aa", "#+=", "123"};
    g.button(K_SHIFT, PAGE_KEY[page_], 4, ROW_Y[2], 44, ROW_H, page_ == UPPER ? Kind::Primary : Kind::Normal, Icon::None, Font::Small);
    g.button(K_DEL, "Del", 272, ROW_Y[2], 44, ROW_H, Kind::Normal, Icon::None, Font::Small);

    // Bottom row
    g.button(K_MODE, page_ <= UPPER ? "?123" : "abc", 4, 184, 50, 38, Kind::Normal, Icon::None, Font::Small);
    g.button(K_SPACE, "space", 58, 184, 154, 38, Kind::Normal, Icon::None, Font::Small);
    g.button(K_OK, "OK", 216, 184, 100, 38, Kind::Primary, Icon::Check);
}

void TextInput::action(uint16_t id)
{
    if (id >= K_KEY0)
    {
        const uint8_t r = (id - K_KEY0) / 10, c = (id - K_KEY0) % 10;
        if (r < 3 && c < strlen(row(r)) && text_.size() < maxLen_)
            text_ += row(r)[c];
        return;
    }
    switch (id)
    {
    case K_SHIFT:
        page_ = page_ == LOWER ? UPPER : page_ == UPPER ? LOWER : page_ == SYM1 ? SYM2 : SYM1;
        break;
    case K_DEL:
        if (!text_.empty())
            text_.pop_back();
        break;
    case K_MODE:
        page_ = page_ <= UPPER ? SYM1 : LOWER;
        break;
    case K_SPACE:
        if (text_.size() < maxLen_)
            text_ += ' ';
        break;
    case K_SHOW:
        show_ = !show_;
        break;
    case K_OK:
    {
        auto fn = onDone_;
        const std::string t = text_;
        ui.pop();
        if (fn)
            fn(t);
        break;
    }
    }
}
