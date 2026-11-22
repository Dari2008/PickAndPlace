#include "gfx.h"
#include <cmath>
#include <cstring>

// Design space (all screens lay out in it)
static constexpr int W = 320, H = 240;
static constexpr bool LARGE = Gfx::KX > 1.2f; // 480 x 320 display

static const lgfx::IFont *fontOf(Font f)
{
    if (LARGE)
        switch (f)
        {
        case Font::Small:
            return &fonts::DejaVu12;
        case Font::Body:
            return &fonts::DejaVu18;
        case Font::Row:
            return &fonts::FreeSans12pt7b;
        case Font::Big:
            return &fonts::FreeSansBold18pt7b;
        case Font::Huge:
            return &fonts::FreeSansBold24pt7b;
        case Font::Giant:
            return &fonts::DejaVu40;
        default: // BodyBold, Title
            return &fonts::FreeSansBold12pt7b;
        }
    switch (f)
    {
    case Font::Small:
        return &fonts::DejaVu9;
    case Font::Body:
        return &fonts::DejaVu12;
    case Font::Row:
        return &fonts::FreeSans9pt7b;
    case Font::Big:
        return &fonts::FreeSansBold12pt7b;
    case Font::Huge:
        return &fonts::FreeSansBold18pt7b;
    case Font::Giant:
        return &fonts::FreeSansBold24pt7b;
    default: // BodyBold, Title
        return &fonts::FreeSansBold9pt7b;
    }
}

// ---------------------------------------------------------------- frame

void Gfx::beginFrame(uint16_t focusId, bool showFocus)
{
    focus_ = focusId;
    showFocus_ = showFocus;
    hits_.clear();
}

void Gfx::addHit(uint16_t id, Rect r, bool touchable)
{
    if (id != ID_NONE)
        hits_.push_back({r, id, touchable});
}

// ---------------------------------------------------------------- clipping

void Gfx::setBaseClip(int x, int y, int w, int h)
{
    base_ = true;
    bx_ = x, by_ = y, bw_ = w, bh_ = h;
    s.setClipRect(x, y, w, h);
}

void Gfx::clearBaseClip()
{
    base_ = false;
    s.clearClipRect();
}

void Gfx::clip(int x, int y, int w, int h)
{
    int x0 = px(x), y0 = py(y), x1 = x0 + pw(x, w), y1 = y0 + ph(y, h);
    if (base_)
    {
        x0 = std::max(x0, bx_), y0 = std::max(y0, by_);
        x1 = std::min(x1, bx_ + bw_), y1 = std::min(y1, by_ + bh_);
    }
    s.setClipRect(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
}

void Gfx::noClip()
{
    if (base_)
        s.setClipRect(bx_, by_, bw_, bh_);
    else
        s.clearClipRect();
}

// ---------------------------------------------------------------- primitives

void Gfx::rframe(int x, int y, int w, int h, int r, uint16_t c, int thick)
{
    const int t = std::max(1, (int)lroundf(thick * KR));
    const int X = px(x), Y = py(y), w2 = pw(x, w), h2 = ph(y, h), r2 = pl(r);
    for (int i = 0; i < t; i++)
        s.drawRoundRect(X + i, Y + i, w2 - 2 * i, h2 - 2 * i, r2 > i ? r2 - i : 0, c);
}

void Gfx::line(int x1, int y1, int x2, int y2, uint16_t c, float w)
{
    const float pwid = w * KR;
    if (pwid <= 1.0f)
        s.drawLine(px(x1), py(y1), px(x2), py(y2), c);
    else
        s.drawWideLine(px(x1), py(y1), px(x2), py(y2), pwid / 2, c);
}

void Gfx::ring(int x, int y, int r, uint16_t c, float w)
{
    const float R = r * KR, half = w * KR / 2;
    s.fillArc(px(x), py(y), R - half < 0 ? 0 : (int)lroundf(R - half), (int)lroundf(R + half - 0.5f), 0, 360, c);
}

// Text with a few glyphs the GFX fonts don't have: ° · – − … are drawn here.
namespace
{
    enum Glyph : uint8_t
    {
        G_TEXT,
        G_DEG,
        G_DOT
    };
    struct Seg
    {
        Glyph kind;
        std::string text;
    };

    void split(const char *t, std::vector<Seg> &out)
    {
        std::string cur;
        auto flush = [&]() {
            if (!cur.empty())
                out.push_back({G_TEXT, cur});
            cur.clear();
        };
        for (const unsigned char *p = (const unsigned char *)t; *p;)
        {
            if (p[0] == 0xC2 && p[1] == 0xB0) // °
            {
                flush();
                out.push_back({G_DEG, ""});
                p += 2;
            }
            else if (p[0] == 0xC2 && p[1] == 0xB7) // ·
            {
                flush();
                out.push_back({G_DOT, ""});
                p += 2;
            }
            else if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x93 || p[2] == 0x94)) // – —
            {
                cur += '-';
                p += 3;
            }
            else if (p[0] == 0xE2 && p[1] == 0x88 && p[2] == 0x92) // −
            {
                cur += '-';
                p += 3;
            }
            else if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0xA6) // …
            {
                cur += "...";
                p += 3;
            }
            else if (*p >= 0x80) // anything else outside ASCII
            {
                cur += '?';
                p++;
                while ((*p & 0xC0) == 0x80)
                    p++;
            }
            else
                cur += (char)*p++;
        }
        flush();
    }
}

// Width in display pixels (font already set).
static int widthPx(LGFX_Sprite &s, const std::vector<Seg> &segs)
{
    const int h = s.fontHeight();
    int w = 0;
    for (auto &g : segs)
        w += g.kind == G_TEXT ? s.textWidth(g.text.c_str()) : (g.kind == G_DEG ? h / 4 + 3 : h / 3 + 2);
    return w;
}

int Gfx::width(const char *t, Font f)
{
    s.setFont(fontOf(f));
    std::vector<Seg> segs;
    split(t, segs);
    return (int)ceilf(widthPx(s, segs) / KX);
}

std::string Gfx::fit(const std::string &t, Font f, int maxW)
{
    if (width(t.c_str(), f) <= maxW)
        return t;
    // Cut at a character boundary (UTF-8) and add "…"
    std::string cut = t;
    while (!cut.empty())
    {
        // one whole character: its continuation bytes, then its lead byte
        while (!cut.empty() && ((unsigned char)cut.back() & 0xC0) == 0x80)
            cut.pop_back();
        if (!cut.empty())
            cut.pop_back();
        while (!cut.empty() && cut.back() == ' ')
            cut.pop_back();
        if (width((cut + "...").c_str(), f) <= maxW)
            return cut + "...";
    }
    return "";
}

std::vector<std::string> Gfx::wrap(const std::string &t, Font f, int maxW, int maxLines)
{
    std::vector<std::string> lines;
    if (maxLines <= 1)
        return {fit(t, f, maxW)};
    std::string cur;
    size_t i = 0;
    while (i < t.size())
    {
        size_t j = t.find(' ', i);
        if (j == std::string::npos)
            j = t.size();
        const std::string word = t.substr(i, j - i);
        i = j + 1;
        if (word.empty())
            continue;
        const std::string tryLine = cur.empty() ? word : cur + " " + word;
        if (cur.empty() || width(tryLine.c_str(), f) <= maxW)
        {
            cur = tryLine;
            continue;
        }
        lines.push_back(cur);
        cur = word;
        if ((int)lines.size() == maxLines - 1)
        {
            // the rest goes on the last line
            cur = t.substr(i - word.size() - 1);
            break;
        }
    }
    if (!cur.empty())
        lines.push_back(cur);
    for (std::string &l : lines)
        l = fit(l, f, maxW);
    return lines;
}

int Gfx::text(const char *t, int x, int y, Font f, uint16_t color, Align a)
{
    s.setFont(fontOf(f));
    std::vector<Seg> segs;
    split(t, segs);
    const int wpx = widthPx(s, segs);
    int X = px(x);
    if (a == Align::Center)
        X -= wpx / 2;
    else if (a == Align::Right)
        X -= wpx;
    const int Y = py(y);
    const int h = s.fontHeight();
    s.setTextColor(color);
    s.setTextDatum(textdatum_t::middle_left);
    for (auto &g : segs)
    {
        if (g.kind == G_TEXT)
        {
            s.drawString(g.text.c_str(), X, Y);
            X += s.textWidth(g.text.c_str());
        }
        else if (g.kind == G_DEG)
        {
            const int r = h / 9 + 1;
            s.drawCircle(X + r + 1, Y - h / 4, r, color);
            X += h / 4 + 3;
        }
        else
        {
            const int r = h > 16 ? 2 : 1;
            s.fillCircle(X + h / 6 + 1, Y, r, color);
            X += h / 3 + 2;
        }
    }
    return (int)ceilf(wpx / KX);
}

// ---------------------------------------------------------------- icons (14 x 14 design box)

// LovyanGFX's anti-aliased primitives (drawWideLine, fillSmoothCircle, fillArc) ignore the clip
// rect: an icon that is not fully inside it (a list row scrolled half under the title bar) is drawn
// into a small sprite first and copied, which does clip.
void Gfx::icon(Icon i, int x, int y, uint16_t c, int arg)
{
    // Icons scale evenly (KR) and sit centred in their scaled box.
    const float ox = px(x) + (14 * KX - 14 * KR) / 2;
    const float oy = py(y) + (14 * KY - 14 * KR) / 2;
    const int pad = 2, size = (int)ceilf(14 * KR) + 2 * pad;
    const int bx = (int)floorf(ox) - pad, by = (int)floorf(oy) - pad;
    int32_t cx, cy, cw, ch;
    s.getClipRect(&cx, &cy, &cw, &ch);
    if (bx >= cx && by >= cy && bx + size <= cx + cw && by + size <= cy + ch)
    {
        iconPx(i, ox, oy, c, arg);
        return;
    }
    if (bx + size <= cx || by + size <= cy || bx >= cx + cw || by >= cy + ch)
        return; // completely outside
    static LGFX_Sprite tmp;
    if (tmp.width() != size)
    {
        tmp.deleteSprite();
        tmp.setColorDepth(16);
        tmp.createSprite(size, size);
    }
    static const uint16_t KEY = 0x0821; // transparent: a colour no icon uses
    tmp.fillScreen(KEY);
    Gfx t(tmp);
    t.iconPx(i, ox - bx, oy - by, c, arg);
    tmp.pushSprite(&s, bx, by, KEY);
}

void Gfx::iconPx(Icon i, float ox, float oy, uint16_t c, int arg)
{
    auto P = [&](float v) { return (int)lroundf(ox + v * KR); };
    auto Q = [&](float v) { return (int)lroundf(oy + v * KR); };
    auto L = [&](float x1, float y1, float x2, float y2, float w = 1.4f) {
        const float pwid = w * KR;
        if (pwid <= 1.0f)
            s.drawLine(P(x1), Q(y1), P(x2), Q(y2), c);
        else
            s.drawWideLine(P(x1), Q(y1), P(x2), Q(y2), pwid / 2, c);
    };
    auto T = [&](float x1, float y1, float x2, float y2, float x3, float y3) {
        s.fillTriangle(P(x1), Q(y1), P(x2), Q(y2), P(x3), Q(y3), c);
    };
    auto A = [&](float cx, float cy, float r, float a0, float a1, float w = 1.5f, uint16_t col = 0) {
        const float R = r * KR, half = w * KR / 2;
        s.fillArc(P(cx), Q(cy), (int)lroundf(std::max(0.0f, R - half)), (int)lroundf(R + half - 0.5f), a0, a1, col ? col : c);
    };
    auto D = [&](float cx, float cy, float r) { s.fillSmoothCircle(P(cx), Q(cy), std::max(1, (int)lroundf(r * KR)), c); };
    auto FR = [&](float fx, float fy, float w, float h, float r) { // frame
        s.drawRoundRect(P(fx), Q(fy), (int)lroundf(w * KR), (int)lroundf(h * KR), (int)lroundf(r * KR), c);
    };
    auto B = [&](float fx, float fy, float w, float h, float r, uint16_t col) { // filled box
        s.fillRoundRect(P(fx), Q(fy), (int)lroundf(w * KR), (int)lroundf(h * KR), (int)lroundf(r * KR), col);
    };
    switch (i)
    {
    case Icon::Jobs:
    case Icon::Folder:
        if (i == Icon::Folder)
            B(1, 3, 12, 9, 2, th::tint565(c, th::BG, 60));
        FR(1, 3, 12, 9, 2);
        L(1, 5, 6, 5);
        L(6, 5, 7, 3);
        break;
    case Icon::Feeders:
        A(7, 7, 5, 0, 360, 1.4f);
        D(7, 7, 1);
        for (int k = 0; k < 6; k++)
        {
            const float r = k * 3.14159f / 3;
            D(7 + cosf(r) * 3.4f, 7 + sinf(r) * 3.4f, 0.6f);
        }
        break;
    case Icon::Move:
        L(7, 1, 7, 13);
        L(1, 7, 13, 7);
        T(7, 0, 4.5f, 3, 9.5f, 3);
        T(7, 14, 4.5f, 11, 9.5f, 11);
        T(0, 7, 3, 4.5f, 3, 9.5f);
        T(14, 7, 11, 4.5f, 11, 9.5f);
        break;
    case Icon::Menu:
        L(2, 3, 12, 3, 1.6f);
        L(2, 7, 12, 7, 1.6f);
        L(2, 11, 12, 11, 1.6f);
        break;
    case Icon::Gear:
        A(7, 7, 4, 0, 360, 1.4f);
        for (int k = 0; k < 8; k++)
        {
            const float r = k * 3.14159f / 4;
            L(7 + cosf(r) * 4.5f, 7 + sinf(r) * 4.5f, 7 + cosf(r) * 6.5f, 7 + sinf(r) * 6.5f, 1.8f);
        }
        D(7, 7, 1);
        break;
    case Icon::Wifi:
        for (int k = 0; k < 3; k++)
            A(7, 12, 2.8f + k * 3.4f, 225, 315, 1.6f, k < arg ? c : th::LINE);
        D(7, 12, 1);
        break;
    case Icon::Home:
        L(1, 7, 7, 1.5f, 1.5f);
        L(7, 1.5f, 13, 7, 1.5f);
        FR(3, 6, 8, 7, 1);
        break;
    case Icon::Power:
        A(7, 7.5f, 5, 297, 603);
        L(7, 1, 7, 7, 1.5f);
        break;
    case Icon::Pump:
        A(7, 7, 5, 0, 360, 1.4f);
        for (int k = 0; k < 3; k++)
        {
            const float r = k * 2.094f;
            L(7, 7, 7 + cosf(r) * 4, 7 + sinf(r) * 4, 1.2f);
        }
        break;
    case Icon::Test:
        L(5, 1, 5, 6);
        L(5, 6, 1.5f, 12.5f);
        L(1.5f, 12.5f, 12.5f, 12.5f);
        L(12.5f, 12.5f, 9, 6);
        L(9, 6, 9, 1);
        L(4, 1, 10, 1);
        break;
    case Icon::Refresh:
        A(7, 7, 4.8f, 36, 306, 1.6f);
        T(12.5f, 2, 12.5f, 7, 8, 7);
        break;
    case Icon::File:
        L(3, 1, 9, 1, 1.2f);
        L(9, 1, 12, 4, 1.2f);
        L(12, 4, 12, 13, 1.2f);
        L(12, 13, 3, 13, 1.2f);
        L(3, 13, 3, 1, 1.2f);
        L(5, 7, 10, 7, 1);
        L(5, 10, 10, 10, 1);
        break;
    case Icon::Back:
        L(9, 2, 4, 7, 2);
        L(4, 7, 9, 12, 2);
        break;
    case Icon::Chevron:
        L(5, 3, 9, 7, 1.6f);
        L(9, 7, 5, 11, 1.6f);
        break;
    case Icon::Nfc:
        for (int k = 0; k < 3; k++)
            A(3, 7, 3 + k * 3, -40, 40, 1.4f);
        break;
    case Icon::Sensor:
        A(7, 7, 5, 0, 360, 1.4f);
        D(7, 7, 2);
        break;
    case Icon::Motor:
        FR(2, 2, 10, 10, 2);
        A(7, 7, 2, 0, 360, 1.2f);
        break;
    case Icon::Sd:
        L(3, 1, 11, 1, 1.3f);
        L(11, 1, 11, 13, 1.3f);
        L(11, 13, 3, 13, 1.3f);
        L(3, 13, 3, 4, 1.3f);
        L(3, 4, 5, 1, 1.3f);
        L(5, 3, 5, 5, 1);
        L(7, 3, 7, 5, 1);
        L(9, 3, 9, 5, 1);
        break;
    case Icon::Touch:
        D(7, 7, 2);
        A(7, 7, 5, 0, 360, 1.2f);
        break;
    case Icon::Play:
        T(3, 1.5f, 12, 7, 3, 12.5f);
        break;
    case Icon::Pause:
        B(3, 2, 3, 10, 1, c);
        B(8, 2, 3, 10, 1, c);
        break;
    case Icon::Stop:
        B(3, 3, 9, 9, 2, c);
        break;
    case Icon::Skip:
        T(2, 2, 9, 7, 2, 12);
        B(10, 2, 3, 10, 1, c);
        break;
    case Icon::Check:
        L(2, 7.5f, 5.5f, 11, 2);
        L(5.5f, 11, 12, 3.5f, 2);
        break;
    case Icon::Plus:
        L(7, 2, 7, 12, 2);
        L(2, 7, 12, 7, 2);
        break;
    case Icon::Trash:
        FR(3, 4, 8, 9, 2);
        L(2, 3, 12, 3, 1.3f);
        L(5.5f, 1.5f, 8.5f, 1.5f, 1.3f);
        break;
    case Icon::Warn:
        L(7, 1, 13, 12.5f, 1.5f);
        L(13, 12.5f, 1, 12.5f, 1.5f);
        L(1, 12.5f, 7, 1, 1.5f);
        L(7, 5, 7, 8.5f, 1.6f);
        D(7, 11, 1);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- widgets

void Gfx::titleBar(const char *title, bool back, const char *right, uint16_t rightColor, Icon action)
{
    rect(0, 0, W, 24, th::PANEL);
    hline(0, 24, W, th::LINE);
    int x = 8;
    if (back)
    {
        icon(Icon::Back, 4, 5, th::TEXT);
        addHit(ID_BACK, {0, 0, 34, 25});
        if (focused(ID_BACK))
            rframe(1, 2, 22, 20, 5, th::ACCENT);
        x = 24;
    }
    text(title, x, 12, Font::Title, th::TEXT);
    int rx = W - 6;
    icon(Icon::Wifi, rx - 15, 4, wifiBars ? th::TEXT : th::DIM, wifiBars);
    rx -= 20;
    disc(rx - 4, 12, 3, linkOk ? th::OK : th::BAD);
    rx -= 12;
    if (action != Icon::None)
    {
        rrect(rx - 26, 2, 26, 20, 6, focused(ID_TITLE_ACTION) ? th::ACCENT_BG : th::PANEL2);
        icon(action, rx - 20, 5, th::ACCENT);
        addHit(ID_TITLE_ACTION, {(int16_t)(rx - 30), 0, 34, 25});
        rx -= 32;
    }
    if (right && *right)
        text(right, rx, 12, Font::Small, rightColor, Align::Right);
}

void Gfx::button(uint16_t id, const char *label, int x, int y, int w, int h, Kind k, Icon ic, Font f)
{
    uint16_t fillc = th::PANEL2, color = th::TEXT;
    if (k == Kind::Primary)
        fillc = th::ACCENT, color = th::ON_ACCENT;
    else if (k == Kind::Danger)
        fillc = th::BAD, color = th::WHITE;
    if (k == Kind::Ghost)
        rframe(x, y, w, h, 8, th::LINE);
    else
        rrect(x, y, w, h, 8, fillc);
    if (focused(id))
        rframe(x - 3, y - 3, w + 6, h + 6, 10, k == Kind::Primary ? th::TEXT : th::ACCENT, 2);
    // Label too wide: a smaller font, then two lines (tall enough), then cut with "..."
    const bool hasLabel = label && *label, hasIcon = ic != Icon::None;
    const int avail = w - 10 - (hasIcon ? 18 : 0);
    std::vector<std::string> lines;
    if (hasLabel)
    {
        bool fits = false;
        for (Font c : {f, Font::Body, Font::Small})
            if (width(label, c) <= avail)
            {
                f = c, fits = true;
                break;
            }
        if (fits)
            lines.push_back(label);
        else
        {
            f = Font::Small;
            lines = wrap(label, f, avail, h >= 26 ? 2 : 1);
        }
    }
    int lw = 0;
    for (const std::string &l : lines)
        lw = std::max(lw, width(l.c_str(), f));
    const int tw = lw + (hasIcon ? (hasLabel ? 18 : 14) : 0);
    int tx = x + (w - tw) / 2;
    if (hasIcon)
    {
        icon(ic, tx, y + h / 2 - 7, color);
        tx += 18;
    }
    const int lh = 11;
    for (size_t i = 0; i < lines.size(); i++)
        text(lines[i], tx + lw / 2, y + h / 2 + (int)(i * lh) - (int)((lines.size() - 1) * lh) / 2, f, color, Align::Center);
    addHit(id, {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h});
}

void Gfx::tile(uint16_t id, const char *label, const char *sub, Icon ic, uint16_t accent, uint16_t accentBg,
               int x, int y, int w, int h, const char *badge, uint16_t badgeColor, uint16_t badgeBg)
{
    rrect(x, y, w, h, 10, th::PANEL);
    if (focused(id))
        rframe(x, y, w, h, 10, th::ACCENT, 2);
    rrect(x + 10, y + 10, 26, 26, 7, accentBg);
    icon(ic, x + 16, y + 16, accent);
    text(label, x + 44, y + 19, Font::Title, th::TEXT);
    if (sub && *sub)
        text(sub, x + 44, y + 34, Font::Small, th::DIM);
    if (badge && *badge)
        pill(badge, x + w - pillWidth(badge) - 8, y + h - 24, badgeColor, badgeBg);
    addHit(id, {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h});
}

int Gfx::pillWidth(const char *t, Font f)
{
    return width(t, f) + 12;
}

int Gfx::pill(const char *t, int x, int y, uint16_t color, uint16_t bg, Font f)
{
    const int w = pillWidth(t, f);
    rrect(x, y, w, 16, 8, bg);
    text(t, x + w / 2, y + 8, f, color, Align::Center);
    return w;
}

void Gfx::progress(int x, int y, int w, int h, float frac, uint16_t color)
{
    rrect(x, y, w, h, h / 2, th::PANEL2);
    if (frac > 1)
        frac = 1;
    if (frac > 0)
        rrect(x, y, std::max(h, (int)(w * frac)), h, h / 2, color);
}

void Gfx::valueBox(uint16_t id, const char *label, const char *value, int x, int y, int w, int h, uint16_t color, bool selected)
{
    const bool sel = selected || focused(id);
    rrect(x, y, w, h, 8, sel ? th::PANEL2 : th::PANEL);
    if (sel)
        rframe(x, y, w, h, 8, th::ACCENT, 2);
    if (h < 38)
    {
        text(label, x + 8, y + h / 2, Font::Small, th::DIM);
        text(value, x + w - 8, y + h / 2, Font::BodyBold, color, Align::Right);
    }
    else
    {
        text(label, x + 8, y + 11, Font::Small, th::DIM);
        text(value, x + 8, y + h - 13, Font::Title, color);
    }
    addHit(id, {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h});
}

void Gfx::toggle(int x, int y, bool on)
{
    rrect(x, y, 30, 16, 8, on ? th::ACCENT : th::PANEL2);
    if (!on)
        rframe(x, y, 30, 16, 8, th::LINE);
    disc(x + (on ? 22 : 8), y + 8, 6, on ? th::ON_ACCENT : th::DIM);
}

void Gfx::scrollbar(float pos, float size, int y0)
{
    const int top = y0 + 4, hh = H - top - 4;
    rrect(W - 6, top, 3, hh, 1, th::PANEL2);
    rrect(W - 6, top + (int)(hh * pos), 3, std::max(6, (int)(hh * size)), 1, th::DIM);
}

void Gfx::dim()
{
    s.fillRectAlpha(0, py(0), SCREEN_W, SCREEN_H, 184, (uint16_t)th::rgb(8, 9, 11));
}

void Gfx::toast(const char *t, uint16_t color)
{
    // One line when it fits, else up to 4 lines in the plain font, growing upwards
    const int textMax = W - 16 - 40;
    Font f = Font::BodyBold;
    std::vector<std::string> lines;
    if (width(t, f) <= textMax)
        lines.push_back(t);
    else
    {
        f = Font::Body;
        lines = wrap(t, f, textMax, 4);
    }
    int tw = 0;
    for (const std::string &l : lines)
        tw = std::max(tw, width(l.c_str(), f));
    const int n = (int)lines.size(), lh = 14, h = std::max(30, n * lh + 14);
    const int w = tw + 40, x = (W - w) / 2, y = H - 14 - h;
    rrect(x, y, w, h, 15, th::PANEL2);
    rframe(x, y, w, h, 15, th::LINE);
    icon(color == th::BAD ? Icon::Warn : Icon::Check, x + 10, y + h / 2 - 7, color);
    for (int i = 0; i < n; i++)
        text(lines[i], x + 28, y + h / 2 + i * lh - (n - 1) * lh / 2, f, th::TEXT);
}

// ---------------------------------------------------------------- list

void ListView::dragBy(int dy)
{
    scroll_ = std::max(0, std::min((int)maxScroll_, scroll_ - dy));
}

void ListView::draw(Gfx &g, uint16_t baseId, const std::vector<Row> &rows, int y0, int h, int bottom)
{
    const int n = rows.size();
    const int view = bottom - y0;
    maxScroll_ = std::max(0, n * h - view);
    // Bring the focused row into view when the focus moved there (knob), not
    // on every frame: that would undo a touch drag.
    const int f = (int)g.focusId() - baseId;
    const bool moved = f != lastFocus_;
    lastFocus_ = (f >= 0 && f < n) ? f : -1;
    if (moved && f >= 0 && f < n)
    {
        if (f * h < scroll_)
            scroll_ = f * h;
        if ((f + 1) * h > scroll_ + view)
            scroll_ = (f + 1) * h - view;
    }
    scroll_ = std::max(0, std::min((int)maxScroll_, (int)scroll_));
    const bool bar = maxScroll_ > 0;
    // Rows are drawn into an image of the list area and copied into it: the anti-aliased
    // primitives (rounded boxes, toggles, icons) ignore clip rects and would paint over the
    // title bar when a row is scrolled half out of view.
    static LGFX_Sprite area;
    const int ax = g.px(0), ay = g.py(y0), aw = g.pw(0, 320), ah = g.ph(y0, view);
    if (area.width() != aw || area.height() != ah)
    {
        area.deleteSprite();
        area.setColorDepth(16);
        area.createSprite(aw, ah);
    }
    area.fillScreen(th::BG);
    Gfx d(area);
    d.setBand((int)lroundf(y0 * Gfx::KY)); // design y0 = top row of the image
    for (int i = 0; i < n; i++)
    {
        const Row &r = rows[i];
        const int y = y0 + i * h - scroll_;
        const uint16_t id = baseId + i;
        const bool visible = y + h > y0 && y < bottom;
        g.addHit(id, {0, (int16_t)std::max(y, y0), (int16_t)(bar ? 312 : 320), (int16_t)(std::min(y + h, bottom) - std::max(y, y0))},
                 visible && y >= y0 - h / 3 && y + h <= bottom + h / 3);
        if (!visible)
            continue;
        const bool sel = g.focused(id);
        if (sel)
        {
            d.rrect(4, y + 3, 320 - 12, h - 4, 7, th::PANEL2);
            d.rrect(4, y + 3, 3, h - 4, 1, th::ACCENT);
        }
        int x = 14;
        if (r.icon != Icon::None)
        {
            d.icon(r.icon, x, y + h / 2 - 7, r.iconColor ? r.iconColor : (sel ? th::ACCENT : th::DIM), r.iconArg);
            x += 22;
        }
        // Right side first (chevron, toggle, value, pill): label and sub are cut to the space left of it
        int rz = 320 - (bar ? 18 : 14);
        if (r.chev)
            rz -= 16;
        if (r.toggle >= 0)
            rz -= 36;
        if (!r.value.empty())
            rz -= d.width(r.value.c_str(), Font::Body) + 6;
        if (!r.pill.empty())
            rz -= d.pillWidth(r.pill.c_str()) + 4;
        const int room = std::max(20, rz - x - 6);
        const int ly = y + h / 2 - (r.sub.empty() ? 0 : 6);
        d.text(d.fit(r.label, r.bold ? Font::Title : Font::Row, room), x, ly, r.bold ? Font::Title : Font::Row, r.labelColor);
        if (!r.sub.empty())
            d.text(d.fit(r.sub, Font::Small, room), x, y + h / 2 + 9, Font::Small, th::DIM);
        int vx = 320 - (bar ? 18 : 14);
        if (r.chev)
        {
            d.icon(Icon::Chevron, vx - 12, y + h / 2 - 7, th::DIM);
            vx -= 16;
        }
        if (r.toggle >= 0)
        {
            d.toggle(vx - 30, y + h / 2 - 8, r.toggle == 1);
            vx -= 36;
        }
        if (!r.value.empty())
            vx -= d.text(r.value, vx, y + h / 2, Font::Body, r.valueColor, Align::Right) + 6;
        if (!r.pill.empty())
            d.pill(r.pill.c_str(), vx - d.pillWidth(r.pill.c_str()), y + h / 2 - 8, r.pillColor, r.pillBg);
        if (i < n - 1 && !sel && !g.focused(id + 1))
            d.hline(14, y + h, 320 - 28, th::LINE);
    }
    area.pushSprite(&g.s, ax, ay); // clipped by the frame's own clip (partial redraws)
    if (bar)
        g.scrollbar((float)scroll_ / (n * h), (float)view / (n * h), y0);
}
