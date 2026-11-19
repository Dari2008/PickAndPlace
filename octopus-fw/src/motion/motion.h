/**
 * motion.h - public motion API of the Octopus firmware (8 axes, homing, feeder probe)
 * -----------------------------------------------------------------------------
 * Derived from ../../../lib/Motion/motion.h (GPLv3). Moves are queued and run in
 * the background (TIM6 / TIM7 interrupts); queue functions return at once unless
 * the queue is full. Blocking calls (home, waitUntilIdle, stop) call the abort
 * hook in their wait loops: when it returns true they stop at once and fail.
 *
 *   motion.begin();
 *   motion.home(MASK_HOME_XY | MASK_HOME_Z);
 *   float t[N_AXIS]; motion.queuedTargets(t); t[X_AXIS] = 100; motion.moveTo(t, 50);
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>
#include "mconfig.h"

#define MASK_HOME_XY 1
#define MASK_HOME_Z 2
#define MASK_HOME_E1 4
#define MASK_HOME_X 8  // X alone (both X motors), for testing / calibrating sensorless homing
#define MASK_HOME_Y 16 // Y alone

class Motion
{
public:
    void begin();
    // Steps, speeds, accelerations, DIR invert, junction deviation, input polarity from settings.
    // Waits until idle when steps change.
    void applySettings();

    // Absolute move of all axes to target[] (units: mm / deg) at `speed` units/s along the path.
    bool moveTo(const float *target, float speed);
    // Relative move of one axis.
    bool moveAxisBy(uint8_t axis, float delta, float speed);
    void queuedTargets(float *target) const; // where the queue ends (float, no rounding drift)

    // Homing (blocks). `what` = MASK_HOME_XY | MASK_HOME_Z | MASK_HOME_E1. False with a reason when it fails.
    bool home(uint8_t what, const char *&why);
    bool isHomed() const { return homed_; }
    bool isE1Homed() const { return e1Homed_; }

    // Probe: drive `axis` towards `toMm`, the step ISR stops it on the step `input` becomes hit
    // (E1 + IN_FEEDER: feeder switch; Z + IN_LOADCELL: nozzle tip touches the load cell).
    // poll: 0 moving, 1 hit (axis stopped there), -1 reached toMm.
    bool startProbe(uint8_t axis, int8_t input, float toMm, float speed);
    int8_t pollProbe();
    void cancelProbe();

    bool isBusy();
    bool waitUntilIdle(); // false when aborted
    void stop();          // decelerate, then drop the queue
    void emergencyStop(); // halt now (steps may be lost: not homed any more)
    void enableMotors(bool on);
    bool motorsOn() const;

    void setPosition(uint8_t axis, float value); // waits until idle
    float getPosition(uint8_t axis);             // executed position
    float getTargetPosition(uint8_t axis) const { return target_[axis]; }

    uint8_t alarm();
    void raiseAlarm(uint8_t code); // halt now + alarm (moves rejected until CLEAR)
    void clearAlarm();
    uint8_t queueSpace();
    float lastXSkew() const { return xSkew_; } // mm the X sides stopped apart at the last homing

    // StallGuard measurement for calibrating sensorless homing: `axis` (X, Y or E1) runs `mm` away from
    // its home end and back at `speed` with the homing current (`curPercent`), the driver's SG_RESULT is
    // read all along (not in the first / last home_blank mm of each way, where it speeds up / slows down).
    // X reads both X motors. Stalls don't stop it. False when the move was refused or aborted.
    struct SgStats
    {
        uint16_t n = 0, min = 0xFFFF, max = 0;
        uint32_t sum = 0;
        void add(uint16_t v)
        {
            n++, sum += v;
            if (v < min)
                min = v;
            if (v > max)
                max = v;
        }
    };
    bool sgTest(uint8_t axis, float mm, float speed, float curPercent, SgStats &s, SgStats &s2);

    bool (*abortHook)() = nullptr; // polled while blocking

private:
    void flush();
    bool queue(const float *target, float feed_per_min);
    bool waitQueue(); // wait until the queue is empty, false when aborted
    bool aborted();
    bool homeX(bool sensorless, const char *&why);
    bool homeY(bool sensorless, const char *&why);
    bool homeZFlag(const char *&why);
    bool homeE1(const char *&why);
    bool seek(uint8_t mask, const int8_t *inputs, float distance, float rate, bool untilClear, uint32_t blankSteps = 0,
              uint32_t xSkewSteps = 0);
    bool relative(uint8_t mask, float distance, float rate); // queued + waited
    bool homed_ = false;
    bool homedX_ = false, homedY_ = false; // homed_ = both (single-axis homing: HOME X / HOME Y)
    bool e1Homed_ = false;
    bool probing_ = false;
    float xSkew_ = 0;
    float target_[N_AXIS] = {0};
};

extern Motion motion;
