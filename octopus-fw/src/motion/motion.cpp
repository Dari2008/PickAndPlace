/**
 * motion.cpp - see motion.h. Homing sequences follow ../../../lib/Motion/endstops.cpp (GPLv3).
 */
#include "motion.h"
#include "planner.h"
#include "stepper.h"
#include "homing_gate.h"
#include "../settings.h"
#include "../tmc.h"
#include "../tmc_frame.h"
#include <Arduino.h>

#include <math.h>
#include <string.h>

Motion motion;

static const int8_t NO_INPUTS[N_AXIS] = {-1, -1, -1, -1, -1, -1, -1, -1};

void Motion::begin()
{
    inputs_init();
    plan_init();
    st_init();
    applySettings();
    plan_sync_position();
    for (uint8_t a = 0; a < N_AXIS; a++)
        target_[a] = getPosition(a);
    homed_ = e1Homed_ = homedX_ = homedY_ = false;
}

void Motion::applySettings()
{
    bool stepsChanged = false;
    for (uint8_t a = 0; a < N_AXIS; a++)
        if (axis_steps_per_mm[a] != cfg::axis(cfg::STEPS, a))
            stepsChanged = true;
    if (stepsChanged)
        waitUntilIdle();

    uint8_t invert = 0;
    st_prep_lock();
    for (uint8_t a = 0; a < N_AXIS; a++)
    {
        if (stepsChanged)
        {
            // Keep the position in units: rescale the step counters
            const float pos = st_get_position_steps(a) / axis_steps_per_mm[a];
            plan_set_steps(a, cfg::axis(cfg::STEPS, a));
            st_set_position_steps(a, lroundf(pos * axis_steps_per_mm[a]));
        }
        plan_set_max_rate(a, cfg::axis(cfg::SPEED, a));
        plan_set_acceleration(a, cfg::axis(cfg::ACCEL, a));
        if (cfg::axis(cfg::INVERT, a) > 0.5f)
            invert |= (uint8_t)bit(a);
    }
    plan_set_junction_deviation(cfg::get(cfg::JUNCTION_DEV));
    if (stepsChanged)
        plan_sync_position();
    st_prep_unlock();
    st_set_dir_invert(invert);

    uint8_t inInvert = 0;
    for (uint8_t i = 0; i < N_INPUTS; i++)
        if (cfg::get((cfg::Key)(cfg::IN_INVERT_0 + i)) > 0.5f)
            inInvert |= (uint8_t)bit(i);
    inputs_set_invert(inInvert);
}

bool Motion::aborted()
{
    return (abortHook && abortHook()) || st_alarm() != ALARM_NONE;
}

void Motion::flush()
{
    st_prep_lock();
    st_reset();
    plan_reset();
    plan_sync_position();
    st_prep_unlock();
    for (uint8_t a = 0; a < N_AXIS; a++)
        target_[a] = getPosition(a);
}

void Motion::queuedTargets(float *target) const { memcpy(target, target_, sizeof(target_)); }

bool Motion::queue(const float *target, float feed_per_min)
{
    for (;;)
    {
        st_prep_lock();
        if (!plan_check_full_buffer())
            break;
        st_prep_unlock();
        if (aborted())
            return false;
    }
    plan_buffer_line(target, feed_per_min); // false only for zero-length moves (fine)
    st_prep_buffer();
    st_prep_unlock();
    st_start_if_needed();
    memcpy(target_, target, sizeof(target_));
    return true;
}

bool Motion::moveTo(const float *target, float speed)
{
    if (st_alarm() != ALARM_NONE || speed <= 0.0f)
        return false;
    return queue(target, MM_PER_S_TO_MM_PER_MIN(speed));
}

bool Motion::moveAxisBy(uint8_t axis, float delta, float speed)
{
    if (axis >= N_AXIS)
        return false;
    float t[N_AXIS];
    queuedTargets(t);
    t[axis] += delta;
    return moveTo(t, speed);
}

bool Motion::isBusy()
{
    st_prep_lock();
    const bool busy = st_is_running() || st_segments_pending() || plan_get_current_block() != NULL;
    st_prep_unlock();
    return busy;
}

bool Motion::waitUntilIdle()
{
    while (isBusy())
        if (aborted())
        {
            flush();
            return false;
        }
    return true;
}

bool Motion::waitQueue() { return waitUntilIdle(); }

void Motion::stop()
{
    st_prep_lock();
    st_begin_hold();
    st_prep_buffer();
    st_prep_unlock();
    st_start_if_needed();
    for (;;)
    {
        st_prep_lock();
        const bool done = st_hold_generation_done() && !st_is_running() && !st_segments_pending();
        st_prep_unlock();
        if (done || st_alarm() != ALARM_NONE)
            break;
    }
    flush();
}

void Motion::emergencyStop()
{
    flush();
    homed_ = e1Homed_ = homedX_ = homedY_ = false;
}

void Motion::raiseAlarm(uint8_t code)
{
    st_set_alarm(code);
    emergencyStop();
}

void Motion::enableMotors(bool on)
{
    st_enable_motors(on);
    if (!on)
        homed_ = e1Homed_ = homedX_ = homedY_ = false;
}

bool Motion::motorsOn() const { return st_motors_enabled(); }

void Motion::setPosition(uint8_t axis, float value)
{
    if (axis >= N_AXIS)
        return;
    waitUntilIdle();
    st_prep_lock();
    st_set_position_steps(axis, lroundf(value * axis_steps_per_mm[axis]));
    plan_sync_position();
    st_prep_unlock();
    target_[axis] = value;
}

float Motion::getPosition(uint8_t axis) { return st_get_position_steps(axis) / axis_steps_per_mm[axis]; }

uint8_t Motion::alarm() { return st_alarm(); }

void Motion::clearAlarm()
{
    flush();
    st_clear_alarm();
    homed_ = e1Homed_ = homedX_ = homedY_ = false;
}

uint8_t Motion::queueSpace()
{
    st_prep_lock();
    const uint8_t n = plan_get_block_buffer_available();
    st_prep_unlock();
    return n;
}

// ---- homing -----------------------------------------------------------------------

// Move the axes in `mask` by `distance` (signed) until each one's input reaches the wanted state.
bool Motion::seek(uint8_t mask, const int8_t *inputs, float distance, float rate, bool untilClear, uint32_t blankSteps,
                  uint32_t xSkewSteps)
{
    st_homing_begin(mask, inputs, untilClear, blankSteps, xSkewSteps);
    float t[N_AXIS];
    queuedTargets(t);
    uint8_t n = 0;
    for (uint8_t a = 0; a < N_MOVE_AXES; a++) // X2 in the mask only gets its stop: it follows X
        if (mask & bit(a))
        {
            t[a] += distance;
            n++;
        }
    queue(t, MM_PER_S_TO_MM_PER_MIN(rate) * sqrtf((float)n));
    while (st_homing_pending() && isBusy())
        if (aborted())
            break;
    const bool found = st_homing_pending() == 0 && st_alarm() == ALARM_NONE && !st_homing_x_skew_failed();
    flush(); // ISR froze each axis on its trigger step; drop the rest of the move
    st_homing_end();
    return found;
}

bool Motion::relative(uint8_t mask, float distance, float rate)
{
    float t[N_AXIS];
    queuedTargets(t);
    uint8_t n = 0;
    for (uint8_t a = 0; a < N_AXIS; a++)
        if (mask & bit(a))
        {
            t[a] += distance;
            n++;
        }
    if (!queue(t, MM_PER_S_TO_MM_PER_MIN(rate) * sqrtf((float)n)))
        return false;
    return waitQueue();
}

// Both X motors run; each stops on its own input, so both sides end on their stop and the gantry is square.
bool Motion::homeX(bool sensorless, const char *&why)
{
    const uint8_t m = X_PAIR;
    int8_t in[N_AXIS];
    memcpy(in, NO_INPUTS, sizeof(in));
    in[X_AXIS] = IN_X_MIN;
    in[X2_AXIS] = IN_X2_MIN;
    const float perMm = axis_steps_per_mm[X_AXIS];
    const float fast = cfg::get(cfg::HOME_SPEED);
    const float slow = cfg::get(cfg::HOME_SLOW);
    const float pulloff = cfg::get(cfg::HOME_PULLOFF);
    const uint32_t blank = sensorless ? (uint32_t)lroundf(cfg::get(cfg::HOME_BLANK) * perMm) : 0;
    const uint32_t skew = (uint32_t)lroundf(cfg::get(cfg::HOME_SKEW_MAX) * perMm);

    if (sensorless)
    {
        tmcs.configure(X_AXIS, cfg::get(cfg::HOME_CUR));
        tmcs.configure(X2_AXIS, cfg::get(cfg::HOME_CUR));
    }
    const bool found = seek(m, in, -cfg::get(cfg::TRAVEL_X) * 1.5f, fast, false, blank, skew);
    if (sensorless)
    {
        tmcs.configure(X_AXIS);
        tmcs.configure(X2_AXIS);
    }
    xSkew_ = st_homing_x_skew() / perMm;
    if (!found)
    {
        if (st_homing_x_skew_failed())
            why = "X sides stopped more than home_skew_max apart (tmc_sg_x / _x2, stops on both beams)";
        else
            why = sensorless ? "X stall not detected on both sides (tmc_sg_x / _x2, DIAG jumpers 0 / 1)" : "X switches not found";
        return false;
    }
    if (!sensorless)
    {
        if (!relative(bit(X_AXIS), pulloff, fast))
        {
            why = "aborted";
            return false;
        }
        if (input_hit(IN_X_MIN) || input_hit(IN_X2_MIN))
        {
            why = "X switch stays closed after pull-off";
            return false;
        }
        if (!seek(m, in, -pulloff * 3.0f, slow, false, 0, skew))
        {
            why = "X switches not found on the slow approach";
            return false;
        }
    }
    setPosition(X_AXIS, 0);
    if (!relative(bit(X_AXIS), pulloff, fast))
    {
        why = "aborted";
        return false;
    }
    const float trim = cfg::get(cfg::X2_OFFSET);
    if (trim != 0.0f)
    {
        st_homing_begin(bit(X2_AXIS), NO_INPUTS); // holds X back: only X2 takes this move
        const bool ok = relative(bit(X_AXIS), trim, slow);
        flush();
        st_homing_end();
        if (!ok)
        {
            why = "aborted";
            return false;
        }
    }
    return true;
}

bool Motion::homeY(bool sensorless, const char *&why)
{
    const uint8_t m = (uint8_t)bit(Y_AXIS);
    int8_t in[N_AXIS];
    memcpy(in, NO_INPUTS, sizeof(in));
    in[Y_AXIS] = IN_Y_MIN;
    const float fast = cfg::get(cfg::HOME_SPEED);
    const float slow = cfg::get(cfg::HOME_SLOW);
    const float pulloff = cfg::get(cfg::HOME_PULLOFF);
    const uint32_t blank = sensorless ? (uint32_t)lroundf(cfg::get(cfg::HOME_BLANK) * axis_steps_per_mm[Y_AXIS]) : 0;

    if (sensorless)
        tmcs.configure(Y_AXIS, cfg::get(cfg::HOME_CUR)); // home_cur like X: the setting is calibrated for all of them
    const bool found = seek(m, in, -cfg::get(cfg::TRAVEL_Y) * 1.5f, fast, false, blank);
    if (sensorless)
        tmcs.configure(Y_AXIS);
    if (!found)
    {
        why = sensorless ? "Y stall not detected (tmc_sg_y, DIAG jumper 7)" : "Y switch not found";
        return false;
    }
    if (!sensorless)
    {
        if (!relative(m, pulloff, fast))
        {
            why = "aborted";
            return false;
        }
        if (input_hit(IN_Y_MIN))
        {
            why = "Y switch stays closed after pull-off";
            return false;
        }
        if (!seek(m, in, -pulloff * 3.0f, slow, false))
        {
            why = "Y switch not found on the slow approach";
            return false;
        }
    }
    setPosition(Y_AXIS, 0);
    if (!relative(m, pulloff, fast))
    {
        why = "aborted";
        return false;
    }
    return true;
}

// Seesaw: the flag on DIAG2 covers the head-1 side (Z > 0); its edge is Z = 0, approached slowly from the head-2 side.
bool Motion::homeZFlag(const char *&why)
{
    const uint8_t m = (uint8_t)bit(Z_AXIS);
    int8_t in[N_AXIS];
    memcpy(in, NO_INPUTS, sizeof(in));
    in[Z_AXIS] = IN_Z_FLAG;
    const float range = cfg::get(cfg::Z_TRAVEL) * 2.2f;
    const float fast = cfg::get(cfg::Z_FLAG_SPEED);
    const float slow = cfg::get(cfg::Z_FLAG_SLOW);
    const float back = cfg::get(cfg::Z_FLAG_BACKOFF);
    const bool onFlag = input_hit(IN_Z_FLAG);
    if (!(onFlag ? seek(m, in, -range, fast, true) : seek(m, in, range, fast, false)))
    {
        why = "seesaw flag edge not found";
        return false;
    }
    if (!relative(m, -back, fast))
    {
        why = "aborted";
        return false;
    }
    if (input_hit(IN_Z_FLAG))
    {
        why = "seesaw flag still seen after backing off";
        return false;
    }
    if (!seek(m, in, back * 3.0f, slow, false))
    {
        why = "seesaw flag edge not found on the slow approach";
        return false;
    }
    setPosition(Z_AXIS, 0);
    return true;
}

bool Motion::homeE1(const char *&why)
{
    const int mode = (int)cfg::get(cfg::E1_HOME);
    if (mode == 0)
    {
        why = "e1_home = none";
        return false;
    }
    const uint8_t m = (uint8_t)bit(E1_AXIS);
    int8_t in[N_AXIS];
    memcpy(in, NO_INPUTS, sizeof(in));
    in[E1_AXIS] = IN_E1_HOME;
    const float dir = cfg::get(cfg::E1_HOME_DIR) >= 0 ? 1.0f : -1.0f;
    const float travel = cfg::get(cfg::TRAVEL_E1);
    const float speed = cfg::get(cfg::E1_HOME_SPEED);
    const float pulloff = cfg::get(cfg::E1_HOME_PULLOFF);
    const float homePos = dir < 0 ? 0.0f : travel;

    const uint32_t blank = mode == 2 ? (uint32_t)lroundf(cfg::get(cfg::HOME_BLANK) * axis_steps_per_mm[E1_AXIS]) : 0;
    if (mode == 2)
        tmcs.configure(E1_AXIS, cfg::get(cfg::HOME_CUR));
    const bool found = seek(m, in, dir * travel * 1.2f, speed, false, blank);
    if (mode == 2)
        tmcs.configure(E1_AXIS);
    if (!found)
    {
        why = mode == 2 ? "gantry stall not detected (check DIAG jumper 6, tmc_sg_e1)" : "gantry switch not found";
        return false;
    }
    if (mode == 1)
    {
        if (!relative(m, -dir * pulloff, speed))
        {
            why = "aborted";
            return false;
        }
        if (input_hit(IN_E1_HOME))
        {
            why = "gantry switch stays closed after pull-off";
            return false;
        }
        if (!seek(m, in, dir * pulloff * 3.0f, speed / 5.0f, false))
        {
            why = "gantry switch not found on the slow approach";
            return false;
        }
    }
    setPosition(E1_AXIS, homePos);
    if (!relative(m, -dir * pulloff, speed))
    {
        why = "aborted";
        return false;
    }
    return true;
}

bool Motion::home(uint8_t what, const char *&why)
{
    why = "";
    if (st_alarm() != ALARM_NONE)
    {
        why = "alarm active (CLEAR first)";
        return false;
    }
    if (!waitUntilIdle())
    {
        why = "aborted";
        return false;
    }
    bool ok = true;
    if (ok && (what & MASK_HOME_Z) && cfg::get(cfg::Z_HOME) >= 1)
        ok = homeZFlag(why);
    const bool wantX = what & (MASK_HOME_XY | MASK_HOME_X), wantY = what & (MASK_HOME_XY | MASK_HOME_Y);
    if (ok && (wantX || wantY))
    {
        const int mode = (int)cfg::get(cfg::XY_HOME);
        if (mode != 0) // mode 0: nothing to home, positions stay as they are
        {
            if (wantX)
                ok = homeX(mode == 2, why);
            if (ok && wantY)
                ok = homeY(mode == 2, why);
        }
        if (ok)
        {
            homedX_ = homedX_ || wantX;
            homedY_ = homedY_ || wantY;
            homed_ = homedX_ && homedY_;
        }
    }
    if (ok && (what & MASK_HOME_E1))
    {
        ok = homeE1(why);
        e1Homed_ = ok;
    }
    if (!ok)
    {
        flush();
        st_homing_end();
        if (wantX)
            homedX_ = false;
        if (wantY)
            homedY_ = false;
        homed_ = homedX_ && homedY_;
    }
    return ok;
}

// ---- StallGuard measurement (calibrating sensorless homing) ---------------------------

bool Motion::sgTest(uint8_t axis, float mm, float speed, float curPercent, SgStats &s, SgStats &s2)
{
    if (axis != X_AXIS && axis != Y_AXIS && axis != E1_AXIS)
        return false;
    if (st_alarm() != ALARM_NONE || !waitUntilIdle())
        return false;
    // "away from home": X / Y home at their minimum, the gantry towards e1_home_dir
    const float away = axis == E1_AXIS && cfg::get(cfg::E1_HOME_DIR) >= 0 ? -1.0f : 1.0f;
    const float blankMm = cfg::get(cfg::HOME_BLANK);
    tmcs.configure(axis, curPercent);
    if (axis == X_AXIS)
        tmcs.configure(X2_AXIS, curPercent);

    bool ok = true;
    for (int leg = 0; leg < 2 && ok; leg++)
    {
        const float start = getPosition(axis);
        const float delta = (leg == 0 ? 1.0f : -1.0f) * away * mm;
        if (!moveAxisBy(axis, delta, speed))
        {
            ok = false;
            break;
        }
        while (isBusy())
        {
            if (aborted())
            {
                ok = false;
                break;
            }
            const float done = fabsf(getPosition(axis) - start);
            if (done < blankMm || done > fabsf(delta) - blankMm)
                continue; // speeding up / slowing down: SG_RESULT isn't meaningful there
            uint32_t v;
            if (tmcs.readReg(axis, tmc::SG_RESULT, v))
                s.add((uint16_t)(v & 0x3FF));
            if (axis == X_AXIS && tmcs.readReg(X2_AXIS, tmc::SG_RESULT, v))
                s2.add((uint16_t)(v & 0x3FF));
            delay(5);
        }
    }
    tmcs.configure(axis);
    if (axis == X_AXIS)
        tmcs.configure(X2_AXIS);
    return ok;
}

// ---- feeder probe ------------------------------------------------------------------

bool Motion::startProbe(uint8_t axis, int8_t input, float toMm, float speed)
{
    if (axis >= N_AXIS || !waitUntilIdle())
        return false;
    int8_t in[N_AXIS];
    memcpy(in, NO_INPUTS, sizeof(in));
    in[axis] = input;
    st_homing_begin((uint8_t)bit(axis), in, false);
    float t[N_AXIS];
    queuedTargets(t);
    t[axis] = toMm;
    probing_ = moveTo(t, speed);
    if (!probing_)
        st_homing_end();
    return probing_;
}

int8_t Motion::pollProbe()
{
    if (!probing_)
        return -1;
    if (st_homing_pending() == 0)
    {
        flush();
        st_homing_end();
        probing_ = false;
        return 1;
    }
    if (!isBusy())
    {
        st_homing_end();
        probing_ = false;
        return -1;
    }
    return 0;
}

void Motion::cancelProbe()
{
    if (!probing_)
        return;
    flush();
    st_homing_end();
    probing_ = false;
}
