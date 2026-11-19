/**
 * widgets.h - reusable screens: dialog and number editor
 * -----------------------------------------------------------------------------
 *   dialog.open("Delete feeder 02?", "is removed from the SD card.", "",
 *               {{"No", Kind::Normal, nullptr}, {"Delete", Kind::Danger, [] { ... }}},
 *               Icon::Trash, th::BAD);
 *
 *   NumberEditor::Spec s;  s.title = "Advance"; s.value = 20; s.unit = "°"; ...
 *   numberEditor.open(s, [](float v) { ... });
 * -----------------------------------------------------------------------------
 */
#pragma once

#include "ui.h"

struct DialogButton
{
    std::string label;
    Kind kind;
    std::function<void()> fn; // nullptr: just close
};

class Dialog : public Screen
{
public:
    void open(const std::string &title, const std::string &line1, const std::string &line2,
              std::vector<DialogButton> buttons, Icon icon = Icon::Warn, uint16_t iconColor = th::WARN,
              int focusButton = 0);
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool modal() const override { return true; }
    uint16_t defaultFocus() const override { return 1 + focus_; }
    // A dialog with this tag is open (e.g. the job's PAUSE message).
    std::string tag;

private:
    std::string title_, line1_, line2_;
    std::vector<DialogButton> buttons_;
    Icon icon_ = Icon::Warn;
    uint16_t iconColor_ = th::WARN;
    int focus_ = 0;
};

extern Dialog dialog;

class NumberEditor : public Screen
{
public:
    struct Spec
    {
        std::string title;   // "Advance · feeder 02"
        std::string what;    // "degrees E0 turns per advance"
        float value = 0;
        std::string unit;    // "°", "mm", "%"
        float min = 0, max = 1000;
        uint8_t decimals = 1;
        std::vector<float> steps{0.1f, 1, 10};
        uint8_t step = 0;       // index into steps
        bool zeroIsUnlimited = false;
    };
    void open(const Spec &spec, std::function<void(float)> onSave);
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    bool turn(int8_t steps) override;
    bool press() override;
    bool hold() override; // save
    uint16_t defaultFocus() const override { return 12; } // Save

private:
    void change(int8_t steps);
    Spec spec_;
    std::function<void(float)> onSave_;
};

extern NumberEditor numberEditor;

// On-screen keyboard (touch: tap keys; knob: turn to a key, press to type,
// hold to cancel). Letters with Shift, two symbol pages: every printable
// ASCII character can be typed.
//   textInput.open("Password · HomeNet", "", true, 63, [](const std::string &t) { ... });
class TextInput : public Screen
{
public:
    void open(const std::string &title, const std::string &text, bool secret, uint8_t maxLen,
              std::function<void(const std::string &)> onDone);
    void draw(Gfx &g) override;
    void action(uint16_t id) override;
    uint16_t defaultFocus() const override { return 100; } // first key
    uint8_t animRects(Rect *r) const override;              // blinking cursor
    uint16_t animMs() const override { return 500; }

private:
    enum Page : uint8_t
    {
        LOWER,
        UPPER,
        SYM1,
        SYM2
    };
    const char *row(uint8_t r) const; // the characters of key row r on this page
    std::string title_, text_;
    bool secret_ = false, show_ = false;
    uint8_t maxLen_ = 63;
    Page page_ = LOWER;
    std::function<void(const std::string &)> onDone_;
};

extern TextInput textInput;

// "12.50" / "unlimited"
std::string fmtNum(float v, int decimals);
