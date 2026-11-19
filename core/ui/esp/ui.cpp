#include "ui.h"
#include "../hw/backlight.h"
#include "../hw/buzzer.h"
#include "../hw/display.h"
#include "../hw/input.h"
#include "../hw/touch.h"
#include "../machine/machine.h"
#include "../config.h"
#include "../net/web.h"
#include "screens.h"

Ui ui;

static LGFX_Sprite canvas; // the whole frame in RAM, sent by display.push()
static const uint16_t TOUCH_MS = 25;     // touch sampling
static const uint32_t OFFLINE_AFTER = 2500;

void Ui::begin()
{
    postMutex_ = xSemaphoreCreateMutex();
    canvas.setPsram(UI_BAND_H == SCREEN_H); // full frame in PSRAM, or a band in internal RAM
    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, UI_BAND_H);
    push(&bootScreen);
}

// ---------------------------------------------------------------- stack

void Ui::push(Screen *s)
{
    stack_.push_back(s);
    focus_ = s->defaultFocus();
    s->enter();
    dirty_ = true;
}

void Ui::pop()
{
    if (stack_.size() <= 1)
        return;
    stack_.pop_back();
    focus_ = top()->defaultFocus();
    top()->resume();
    dirty_ = true;
}

void Ui::replace(Screen *s)
{
    if (!stack_.empty())
        stack_.pop_back();
    push(s);
}

void Ui::goHome()
{
    while (stack_.size() > 1)
        stack_.pop_back();
    if (stack_.empty() || stack_[0] != &homeScreen)
    {
        stack_.clear();
        push(&homeScreen);
        return;
    }
    focus_ = homeScreen.defaultFocus();
    homeScreen.resume();
    dirty_ = true;
}

bool Ui::contains(Screen *s) const
{
    for (auto *x : stack_)
        if (x == s)
            return true;
    return false;
}

void Ui::goBack()
{
    if (stack_.size() > 1 && top()->back())
        pop();
}

void Ui::setFocus(uint16_t id)
{
    focus_ = id;
    dirty_ = true;
}

void Ui::toast(const std::string &text, uint16_t color)
{
    toast_ = text;
    toastColor_ = color;
    toastUntil_ = millis() + 1600;
    dirty_ = true;
}

void Ui::post(std::function<void()> fn)
{
    xSemaphoreTake(postMutex_, portMAX_DELAY);
    posted_.push_back(std::move(fn));
    xSemaphoreGive(postMutex_);
}

void Ui::send(const std::string &line, std::function<void(const Reply &)> done, bool toastError, bool redrawAfter)
{
    ctrl.send(line, [done, toastError, redrawAfter](const Reply &r) {
        ui.post([done, toastError, redrawAfter, r]() {
            if (!r.ok && toastError)
                ui.toast(r.error.empty() ? "error" : r.error, th::BAD);
            if (done)
                done(r);
            if (redrawAfter || !r.ok)
                ui.redraw();
        });
    });
}

void Ui::click()
{
    if (machine.setting("beep", "1") != "0")
        buzzer.click();
}

// ---------------------------------------------------------------- input

void Ui::moveFocus(int8_t steps)
{
    std::vector<uint16_t> ids;
    for (auto &h : hits_)
        if (h.id != ID_BACK)
            ids.push_back(h.id);
    if (ids.empty())
        return;
    int i = 0;
    for (; i < (int)ids.size() && ids[i] != focus_; i++)
        ;
    if (i == (int)ids.size())
        i = steps > 0 ? -1 : (int)ids.size();
    i = std::max(0, std::min((int)ids.size() - 1, i + steps));
    focus_ = ids[i];
}

void Ui::activate(uint16_t id)
{
    if (id == ID_NONE)
        return;
    if (id == ID_BACK)
        goBack();
    else if (top())
        top()->action(id);
    dirty_ = true;
}

void Ui::handleInput()
{
    Screen *s = top();
    if (!s)
        return;

    // Screen off: the first knob turn / press only switches the light on.
    int8_t steps = input.takeSteps();
    ButtonEvent button = input.takeButton();
    if ((steps || button != BUTTON_NONE) && backlight.wake())
    {
        steps = 0;
        button = BUTTON_NONE;
        dirty_ = true;
    }
    if (steps)
    {
        if (!showFocus_)
            showFocus_ = true; // after touch, the first knob turn only shows the highlight
        else if (!s->turn(steps))
            moveFocus(steps);
        dirty_ = true;
    }
    switch (button)
    {
    case BUTTON_PRESS:
        click();
        showFocus_ = true;
        if (!s->press())
            activate(focus_);
        dirty_ = true;
        break;
    case BUTTON_HOLD:
        click();
        if (!s->hold())
            goBack();
        dirty_ = true;
        break;
    default:
        break;
    }

    if (millis() - lastTouchRead_ < TOUCH_MS)
        return;
    lastTouchRead_ = millis();
    if (s->rawTouchMode())
    {
        int16_t rx, ry;
        uint16_t pressure;
        if (touch.readRaw(rx, ry, pressure))
        {
            backlight.wake();
            touching_ = true;
            rawSumX_ += rx;
            rawSumY_ += ry;
            rawCount_++;
        }
        else if (touching_)
        {
            touching_ = false;
            if (rawCount_ >= 3)
            {
                click();
                s->rawTouch(rawSumX_ / rawCount_, rawSumY_ / rawCount_);
            }
            rawSumX_ = rawSumY_ = rawCount_ = 0;
            dirty_ = true;
        }
        return;
    }
    TouchPoint p;
    if (touch.read(p))
    {
        // Screen off: this touch only switches the light on.
        if (backlight.wake() && !touching_)
            wakeTouch_ = true;
        if (wakeTouch_)
        {
            dirty_ = true;
            return;
        }
        // Display pixels -> the 320 x 240 design space of the screens.
        p.x = (int16_t)(p.x / Gfx::KX);
        p.y = (int16_t)(p.y / Gfx::KY);
        if (!touching_)
        {
            touching_ = true;
            dragging_ = false;
            touchStartY_ = p.y;
            touchStartMs_ = millis();
        }
        else if (!dragging_ && abs(p.y - touchStartY_) > touch.options().dragStart)
            dragging_ = true;
        if (dragging_)
        {
            s->drag(p.y - touchLastY_);
            dirty_ = true;
        }
        touchLastX_ = p.x;
        touchLastY_ = p.y;
    }
    else if (wakeTouch_)
        wakeTouch_ = false; // released: the next touch works normally
    else if (touching_)
    {
        touching_ = false;
        if (dragging_ || millis() - touchStartMs_ < touch.options().minPressMs)
            return; // a scroll, or too short (ghost touch)
        // Tap: the last area drawn under the finger (dialogs are drawn last).
        for (int i = (int)hits_.size() - 1; i >= 0; i--)
        {
            const Hit &h = hits_[i];
            if (h.touchable && h.r.contains(touchLastX_, touchLastY_))
            {
                click();
                showFocus_ = false;
                focus_ = h.id;
                activate(h.id);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------- machine events

void Ui::watchMachine()
{
    static uint32_t offlineSince = 0;
    static bool wasJobActive = false;
    static std::string lastAlarmState;
    const MachineStatus st = machine.status();
    const JobStatus job = machine.job();

    // Controller gone / back
    if (!st.connected)
    {
        if (!offlineSince)
            offlineSince = millis();
        if (millis() - offlineSince > OFFLINE_AFTER && top() != &bootScreen && !contains(&offlineScreen))
            push(&offlineScreen);
    }
    else
        offlineSince = 0;

    // Alarm (once per alarm)
    if (st.connected && st.state == "alarm" && lastAlarmState != "alarm" && !contains(&alarmScreen) && top() != &bootScreen)
    {
        push(&alarmScreen);
        backlight.wake();
    }
    lastAlarmState = st.state;

    // A job started (here, on the website or over USB): show it.
    const bool active = job.active();
    if (active && !wasJobActive && !contains(&jobScreen) && top() != &bootScreen)
    {
        goHome();
        push(&jobScreen);
    }
    wasJobActive = active;

    // The job's PAUSE "message" waits for Continue.
    const bool msgOpen = top() == &dialog && dialog.tag == "message";
    if (job.state == "message" && !msgOpen && top() != &bootScreen && !contains(&dialog))
    {
        dialog.open("Job is waiting", job.message, "PAUSE in the job file",
                    {{"Stop job", Kind::Normal, [] {
                          dialog.open("Stop the job?", "The machine parks (run_after),", "then pump and valve switch off.",
                                      {{"No", Kind::Normal, nullptr}, {"Stop", Kind::Danger, [] { ui.send("STOP"); }}},
                                      Icon::Stop, th::BAD, 0);
                      }},
                     {"Continue", Kind::Primary, [] { ui.send("CONTINUE"); }}},
                    Icon::Pause, th::ACCENT, 1);
        dialog.tag = "message";
        backlight.wake();
    }
    else if (job.state != "message" && msgOpen)
        pop();
}

// ---------------------------------------------------------------- render

static const Rect BANNER = {20, 186, 280, 42};

static bool bannerVisible(const UploadState &u)
{
    return u.active || (u.endedAt && millis() - u.endedAt <= 3000);
}

static void drawUploadBanner(Gfx &g)
{
    const UploadState u = uploadState();
    if (!bannerVisible(u))
        return;
    const int x = BANNER.x, y = BANNER.y, w = BANNER.w;
    g.rrect(x, y, w, 42, 10, th::PANEL2);
    g.rframe(x, y, w, 42, 10, th::LINE);
    const uint16_t c = u.active ? th::ACCENT : (u.ok ? th::OK : th::BAD);
    g.icon(u.active ? Icon::Jobs : (u.ok ? Icon::Check : Icon::Warn), x + 10, y + 9, c);
    std::string t = (u.active ? "Receiving " : (u.ok ? "Received " : "Upload failed: ")) + (u.ok || u.active ? u.path : u.error);
    g.text(t, x + 30, y + 15, Font::Small, th::TEXT);
    g.progress(x + 30, y + 27, w - 44, 5, u.total ? (float)u.done / u.total : 0, c);
}

void Ui::render(const Rect *rects, uint8_t count)
{
    Gfx g(canvas);
    g.linkOk = machine.status().connected;
    g.wifiBars = wifiBars();

    // The parts to send, in display pixels (one: the whole screen).
    struct Area
    {
        int x, y, w, h;
    } areas[10];
    uint8_t n = 0;
    if (!count)
        areas[n++] = {0, 0, SCREEN_W, SCREEN_H};
    for (uint8_t i = 0; i < count && n < 10; i++)
    {
        const Rect &r = rects[i];
        // One pixel more on each side for anti-aliased edges.
        const int x0 = std::max(0, (int)floorf(r.x * Gfx::KX) - 1), y0 = std::max(0, (int)floorf(r.y * Gfx::KY) - 1);
        const int x1 = std::min(SCREEN_W, (int)ceilf((r.x + r.w) * Gfx::KX) + 1);
        const int y1 = std::min(SCREEN_H, (int)ceilf((r.y + r.h) * Gfx::KY) + 1);
        if (x1 > x0 && y1 > y0)
            areas[n++] = {x0, y0, x1 - x0, y1 - y0};
    }

    // Without PSRAM the sprite holds only a band of the screen: the frame is
    // drawn once per band, each band pushed at its place.
    for (int top = 0; top < SCREEN_H; top += UI_BAND_H)
    {
        for (uint8_t a = 0; a < n; a++)
        {
            const Area &ar = areas[a];
            const int y0 = std::max(ar.y, top), y1 = std::min(ar.y + ar.h, top + UI_BAND_H);
            if (y1 <= y0)
                continue;
            g.setBand(top);
            if (count)
                g.setBaseClip(ar.x, y0 - top, ar.w, y1 - y0);
            g.beginFrame(focus_, showFocus_);
            const size_t ns = stack_.size();
            if (ns >= 2 && stack_[ns - 1]->modal())
            {
                // The screen below, then the dialog over it; only the dialog is usable.
                g.beginFrame(ID_NONE, false);
                stack_[ns - 2]->draw(g);
                g.beginFrame(focus_, showFocus_);
                g.dim();
            }
            if (ns)
                stack_[ns - 1]->draw(g);
            drawUploadBanner(g);
            if (millis() < toastUntil_)
                g.toast(toast_.c_str(), toastColor_);
            g.clearBaseClip();
            display.push(canvas, ar.x, y0, ar.w, y1 - y0);
        }
    }
    hits_ = g.hits();

    // Focus on something that exists.
    bool found = false;
    for (auto &h : hits_)
        if (h.id == focus_)
            found = true;
    if (!found && !hits_.empty())
    {
        const uint16_t def = top() ? top()->defaultFocus() : ID_NONE;
        uint16_t pick = ID_NONE;
        for (auto &h : hits_)
            if (h.id == def)
                pick = def;
        if (pick == ID_NONE)
            for (auto &h : hits_)
                if (h.id != ID_BACK)
                {
                    pick = h.id;
                    break;
                }
        if (pick != ID_NONE && pick != focus_)
        {
            focus_ = pick;
            dirty_ = true;
        }
    }
}

void Ui::update()
{
    // Work handed over from other tasks (link answers).
    std::vector<std::function<void()>> work;
    xSemaphoreTake(postMutex_, portMAX_DELAY);
    work.swap(posted_);
    xSemaphoreGive(postMutex_);
    for (auto &fn : work)
        fn();

    input.setSteps(!(top() && top()->usesStick()));
    handleInput();
    watchMachine();
    if (top())
        top()->tick();

    const uint32_t now = millis();
    const uint32_t rev = machine.revision();
    const uint32_t srev = machine.statusRevision();
    const UploadState up = uploadState();
    const bool banner = bannerVisible(up);
    const bool toastChange = toastUntil_ && now >= toastUntil_ && now - toastUntil_ < 100;

    // Full redraw: screen changed (input, answers), data changed, toast / banner came or went.
    if (dirty_ || rev != lastRevision_ || toastChange || banner != bannerShown_)
    {
        lastRevision_ = rev;
        lastStatusRevision_ = srev;
        bannerShown_ = banner;
        dirty_ = false;
        lastRender_ = lastAnim_ = now;
        render();
        return;
    }

    // Partial: live status areas, animations, the upload banner's progress.
    Screen *s = top();
    if (!s)
        return;
    Rect r[10];
    uint8_t n = 0;
    if (srev != lastStatusRevision_)
    {
        lastStatusRevision_ = srev;
        n += s->liveRects(r + n);
    }
    if (now - lastAnim_ >= s->animMs())
    {
        const uint8_t k = s->animRects(r + n);
        if (k)
            lastAnim_ = now;
        n += k;
    }
    if (up.active && now - lastRender_ >= 200)
        r[n++] = BANNER;
    if (n)
    {
        lastRender_ = now;
        render(r, n);
    }
}
