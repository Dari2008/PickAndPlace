/**
 * gfx.h - drawing toolkit of the display UI (the widgets of mockups/mockups.js)
 * -----------------------------------------------------------------------------
 * Screens draw every frame into an off-screen sprite through Gfx. Widgets that
 * can be used (buttons, rows, tiles ...) register a hit area with an id:
 * touch picks the area under the finger, the knob moves the focus through the
 * areas in drawing order. A focused widget draws its highlight itself.
 *
 *   g.button(ID_RUN, "Run", 8, 124, 148, 40, Kind::Primary, Icon::Play);
 *   list.draw(g, ID_ROWS, rows);          // rows ID_ROWS + 0, + 1, ...
 * -----------------------------------------------------------------------------
 */
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <string>
#include <vector>
#include "../config.h"
#include "theme.h"

constexpr uint16_t ID_NONE = 0;
constexpr uint16_t ID_BACK = 0xFFF0;         // "‹" in the title bar
constexpr uint16_t ID_TITLE_ACTION = 0xFFF1; // action icon in the title bar

enum class Icon : uint8_t
{
    None, Jobs, Feeders, Move, Menu, Gear, Wifi, Home, Power, Pump, Test, Refresh, Folder, File,
    Back, Chevron, Nfc, Sensor, Motor, Sd, Touch, Play, Pause, Stop, Skip, Check, Plus, Trash, Warn
};

enum class Kind : uint8_t
{
    Normal,
    Primary,
    Danger,
    Ghost
};

struct Rect
{
    int16_t x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct Hit
{
    Rect r;
    uint16_t id;
    bool touchable; // false for list rows scrolled out of view (knob only)
};

class Gfx
{
public:
    explicit Gfx(LGFX_Sprite &sprite) : s(sprite) {}

    // Screens lay out in a 320 x 240 design space; Gfx scales to the display
    // (480 x 320: x 1.5, y 1.33; round things and line widths by the smaller
    // factor). The sprite may hold only a band of the screen (boards without
    // PSRAM): `oy_` is the band's top in display pixels.
    static constexpr float KX = SCREEN_W / 320.0f;
    static constexpr float KY = SCREEN_H / 240.0f;
    static constexpr float KR = KX < KY ? KX : KY;
    LGFX_Sprite &s;
    void setBand(int top) { oy_ = top; }
    int px(float x) const { return (int)lroundf(x * KX); }
    int py(float y) const { return (int)lroundf(y * KY) - oy_; }
    int pw(float x, float w) const { return px(x + w) - px(x); }
    int ph(float y, float h) const { return (int)lroundf((y + h) * KY) - (int)lroundf(y * KY); }
    static int pl(float v) { return (int)lroundf(v * KR); }

    // ---- frame ------------------------------------------------------------------
    void beginFrame(uint16_t focusId, bool showFocus);
    void addHit(uint16_t id, Rect r, bool touchable = true);
    bool focused(uint16_t id) const { return showFocus_ && id == focus_; }
    uint16_t focusId() const { return focus_; }
    const std::vector<Hit> &hits() const { return hits_; }

    // ---- primitives -------------------------------------------------------------
    // All coordinates and sizes below are in the 320 x 240 design space.
    void fill(uint16_t c) { s.fillScreen(c); }
    void rrect(int x, int y, int w, int h, int r, uint16_t c) { s.fillSmoothRoundRect(px(x), py(y), pw(x, w), ph(y, h), pl(r), c); }
    void rect(int x, int y, int w, int h, uint16_t c) { s.fillRect(px(x), py(y), pw(x, w), ph(y, h), c); }
    void hline(int x, int y, int w, uint16_t c) { s.drawFastHLine(px(x), py(y), pw(x, w), c); }
    void circle(int x, int y, int r, uint16_t c) { s.drawCircle(px(x), py(y), pl(r), c); }
    void arc(int x, int y, int r0, int r1, float a0, float a1, uint16_t c) { s.fillArc(px(x), py(y), pl(r0), pl(r1), a0, a1, c); }
    // RGB565 pixels (native byte order) at design x / y; w / h in display pixels
    void image(const uint16_t *rgb565, int w, int h, int x, int y) { s.pushImage(px(x), py(y), w, h, (const lgfx::rgb565_t *)rgb565); }
    void qrcode(const char *text, int x, int y, int size) { s.qrcode(text, px(x), py(y), pl(size), 3); }
    // Clip to a design rect (within the base clip of a partial repaint).
    void clip(int x, int y, int w, int h);
    void noClip(); // back to the base clip
    // Partial repaint: everything drawn stays inside this rect (sprite pixels).
    void setBaseClip(int x, int y, int w, int h);
    void clearBaseClip();
    void rframe(int x, int y, int w, int h, int r, uint16_t c, int thick = 1);
    void line(int x1, int y1, int x2, int y2, uint16_t c, float w = 1);
    void disc(int x, int y, int r, uint16_t c) { s.fillSmoothCircle(px(x), py(y), pl(r), c); }
    void ring(int x, int y, int r, uint16_t c, float w = 1.4f);
    // y is the vertical centre of the text. Returns the width drawn.
    int text(const char *t, int x, int y, Font f, uint16_t color, Align a = Align::Left);
    int text(const std::string &t, int x, int y, Font f, uint16_t color, Align a = Align::Left)
    {
        return text(t.c_str(), x, y, f, color, a);
    }
    int width(const char *t, Font f);
    std::string fit(const std::string &t, Font f, int maxW); // cut with "..." to maxW design px
    // Breaks t at spaces into lines of at most maxW design px; the last of maxLines lines is cut with "..."
    std::vector<std::string> wrap(const std::string &t, Font f, int maxW, int maxLines);
    void icon(Icon i, int x, int y, uint16_t c, int arg = 3); // 14 x 14 box

    // ---- widgets ------------------------------------------------------------------
    void background() { fill(th::BG); }
    void titleBar(const char *title, bool back = true, const char *right = nullptr, uint16_t rightColor = th::DIM,
                  Icon action = Icon::None);
    void button(uint16_t id, const char *label, int x, int y, int w, int h, Kind k = Kind::Normal,
                Icon ic = Icon::None, Font f = Font::BodyBold);
    void tile(uint16_t id, const char *label, const char *sub, Icon ic, uint16_t accent, uint16_t accentBg,
              int x, int y, int w, int h, const char *badge = nullptr, uint16_t badgeColor = 0, uint16_t badgeBg = 0);
    int pill(const char *t, int x, int y, uint16_t color, uint16_t bg, Font f = Font::Small);
    int pillWidth(const char *t, Font f = Font::Small);
    void progress(int x, int y, int w, int h, float frac, uint16_t color = th::ACCENT);
    void card(int x, int y, int w, int h) { rrect(x, y, w, h, 10, th::PANEL); }
    void valueBox(uint16_t id, const char *label, const char *value, int x, int y, int w, int h,
                  uint16_t color = th::TEXT, bool selected = false);
    void toggle(int x, int y, bool on);
    void hint(const char *t) { text(t, 160, 231, Font::Small, th::DIM, Align::Center); }
    void scrollbar(float pos, float size, int y0);
    void dim(); // darken everything drawn so far (behind a dialog)
    void toast(const char *t, uint16_t color);

    // Title bar status, set by the UI before drawing.
    bool linkOk = false;
    int wifiBars = 0;

private:
    void iconPx(Icon i, float ox, float oy, uint16_t c, int arg); // at display pixels, no clip handling
    int oy_ = 0;
    bool base_ = false;
    int bx_ = 0, by_ = 0, bw_ = 0, bh_ = 0;
    uint16_t focus_ = ID_NONE;
    bool showFocus_ = true;
    std::vector<Hit> hits_;
};

// ---- list -----------------------------------------------------------------------

struct Row
{
    std::string label, sub, value;
    Icon icon = Icon::None;
    int8_t iconArg = 3;     // e.g. WiFi bars
    uint16_t iconColor = 0; // 0 = dim (accent when focused)
    uint16_t labelColor = th::TEXT;
    uint16_t valueColor = th::DIM;
    bool chev = false;
    int8_t toggle = -1; // -1 none, 0 off, 1 on
    std::string pill;
    uint16_t pillColor = th::ACCENT, pillBg = th::ACCENT_BG;
    bool bold = false;
};

class ListView
{
public:
    // Rows get ids baseId + index. When the focus moves to a row (knob), that
    // row is scrolled into view; a touch drag scrolls freely.
    void draw(Gfx &g, uint16_t baseId, const std::vector<Row> &rows, int y0 = 25, int rowH = 34, int bottom = 240);
    void dragBy(int dy);
    void reset() { scroll_ = 0; lastFocus_ = -1; }

private:
    int16_t scroll_ = 0;
    int16_t maxScroll_ = 0;
    int16_t lastFocus_ = -1; // row brought into view last (-1: none)
};
