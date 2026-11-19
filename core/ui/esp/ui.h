/**
 * ui.h - screens, input and rendering of the display UI (ui-mockups.md)
 * -----------------------------------------------------------------------------
 * A stack of screens; the top one is drawn every frame into an off-screen
 * sprite (PSRAM) that is then pushed to the display. Dialogs are "modal"
 * screens drawn over a dimmed copy of the screen below.
 *
 * Input (both at the same time):
 *   knob turn   screen.turn(), else moves the focus through the hit areas
 *   knob press  screen.press(), else activates the focused area
 *   knob hold   screen.hold(), else back
 *   touch tap   activates the area under the finger; vertical drag scrolls
 *
 * Commands to the controller go through ui.send(): the answer comes back on
 * the UI thread (never inside the link task), errors show as a toast.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <Arduino.h>
#include <functional>
#include <string>
#include <vector>
#include "../link/link.h"
#include "../rtos.h"
#include "gfx.h"

class Screen
{
public:
    virtual ~Screen() {}
    virtual void enter() {}  // opened (pushed)
    virtual void resume() {} // the screen above it was closed
    virtual void draw(Gfx &g) = 0;
    virtual void action(uint16_t id) {}
    virtual bool turn(int8_t steps) { return false; } // true = used (else: focus moves)
    virtual bool press() { return false; }            // true = used (else: activate focus)
    virtual bool hold() { return false; }             // true = used (else: back)
    virtual bool back() { return true; }              // false = don't close
    virtual void drag(int dy) {}
    virtual bool rawTouchMode() const { return false; } // touch calibration
    virtual void rawTouch(int16_t rx, int16_t ry) {}
    virtual bool modal() const { return false; }
    virtual uint16_t defaultFocus() const { return ID_NONE; }
    virtual void tick() {}
    // The joystick is used by the screen itself (jogging, joystick test): no focus steps from it
    virtual bool usesStick() const { return false; }

    // Partial repaints. A full redraw over SPI takes ~100 ms, so the parts
    // that change on their own are repainted alone:
    //   liveRects()   areas showing live status (positions, outputs, job
    //                 progress): repainted on every "!status" / "!job"
    //   animRects()   animations: repainted every animMs()
    // Rects are in the 320 x 240 design space; return how many were written
    // (max 4). A screen whose layout changes calls ui.redraw() for a full one.
    virtual uint8_t liveRects(Rect *r) const { return 0; }
    virtual uint8_t animRects(Rect *r) const { return 0; }
    virtual uint16_t animMs() const { return 50; }
};

class Ui
{
public:
    void begin();
    void update(); // from loop()

    void push(Screen *s);
    void pop();
    void replace(Screen *s); // pop the top, push s
    void goHome();           // back to the home screen
    Screen *top() const { return stack_.empty() ? nullptr : stack_.back(); }
    bool contains(Screen *s) const;

    void toast(const std::string &text, uint16_t color = th::OK);
    void post(std::function<void()> fn); // run on the UI thread (thread-safe)
    void redraw() { dirty_ = true; }
    void setFocus(uint16_t id);

    // Send a protocol command; `done` runs on the UI thread. Errors -> toast.
    // `redrawAfter` = full redraw once the answer is handled (off for polling
    // that only feeds live / animated areas).
    void send(const std::string &line, std::function<void(const Reply &)> done = nullptr, bool toastError = true,
              bool redrawAfter = true);

    void click(); // key click (if beep is on)

private:
    // Draw the frame; with `rects`, only those areas are drawn and sent.
    void render(const Rect *rects = nullptr, uint8_t count = 0);
    void handleInput();
    void watchMachine();
    void goBack();
    void moveFocus(int8_t steps);
    void activate(uint16_t id);

    std::vector<Screen *> stack_;
    std::vector<Hit> hits_;
    uint16_t focus_ = ID_NONE;
    bool showFocus_ = true;
    bool dirty_ = true;
    uint32_t lastRender_ = 0, lastAnim_ = 0;
    uint32_t lastRevision_ = 0, lastStatusRevision_ = 0;
    bool bannerShown_ = false;
    std::string toast_;
    uint16_t toastColor_ = th::OK;
    uint32_t toastUntil_ = 0;

    // touch
    bool touching_ = false, dragging_ = false;
    int16_t touchStartY_ = 0, touchLastX_ = 0, touchLastY_ = 0;
    uint32_t touchStartMs_ = 0;
    bool wakeTouch_ = false; // this touch switched the backlight on: ignore it
    int32_t rawSumX_ = 0, rawSumY_ = 0, rawCount_ = 0;
    uint32_t lastTouchRead_ = 0;

    SemaphoreHandle_t postMutex_ = nullptr;
    std::vector<std::function<void()>> posted_;
};

extern Ui ui;
