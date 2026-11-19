#include "input.h"
#include <Arduino.h>
#include "../../ctrl/settings/general_settings.h"

#include <algorithm>
#include <cmath>

Input input;

static const uint16_t DEBOUNCE_MS = 20;
static const float STEP_AT = 0.45f;    // deflection that starts stepping
static const float BACK_AT = -0.75f;   // left flick = back
static const uint16_t FIRST_REPEAT = 450, FASTEST = 60;

void Input::begin() {}

void Input::update()
{
    const uint32_t now = millis();
    last_ = joystick.read();

    // Up / down -> focus steps, repeating while held over (faster with more deflection)
    const float y = last_.y;
    const int8_t dir = y > STEP_AT ? -1 : y < -STEP_AT ? 1 : 0; // up = towards the top of the list
    if (!stepsOn_ || dir == 0)
        dir_ = 0;
    else if (dir != dir_)
    {
        dir_ = dir;
        steps_ += dir;
        nextStep_ = now + FIRST_REPEAT;
    }
    else if ((int32_t)(now - nextStep_) >= 0)
    {
        steps_ += dir;
        const float k = (fabsf(y) - STEP_AT) / (1 - STEP_AT); // 0 .. 1
        nextStep_ = now + (uint32_t)(FIRST_REPEAT / 2 - k * (FIRST_REPEAT / 2 - FASTEST));
    }

    // Left flick -> back (once per flick)
    if (stepsOn_ && last_.x < BACK_AT && leftArmed_ && fabsf(y) < STEP_AT)
    {
        leftArmed_ = false;
        pending_ = BUTTON_HOLD;
    }
    else if (last_.x > BACK_AT / 2)
        leftArmed_ = true;

    // Button with debounce, press / hold like the knob
    const bool raw = last_.button;
    if (raw != raw_)
    {
        raw_ = raw;
        changedAt_ = now;
    }
    if (now - changedAt_ < DEBOUNCE_MS || raw == down_)
    {
        if (down_ && !holdSent_ && now - downAt_ >= std::max<uint16_t>(200, settings.joyHoldMs)) // joy_hold_ms
        {
            holdSent_ = true;
            pending_ = BUTTON_HOLD;
        }
        return;
    }
    down_ = raw;
    if (down_)
    {
        downAt_ = now;
        holdSent_ = false;
    }
    else if (!holdSent_)
        pending_ = BUTTON_PRESS;
}

int8_t Input::takeSteps()
{
    const int s = steps_.exchange(0);
    return (int8_t)std::max(-100, std::min(100, s));
}

ButtonEvent Input::takeButton()
{
    return (ButtonEvent)pending_.exchange(BUTTON_NONE);
}

void Input::inject(int8_t steps, ButtonEvent b)
{
    steps_ += steps;
    if (b != BUTTON_NONE)
        pending_ = b;
}
