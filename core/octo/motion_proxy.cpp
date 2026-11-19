#include "../compat/motion.h"
#include "../compat/Arduino.h"
#include "octopus.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

Motion motion;

// ATmega axis numbers -> Octopus names
static const char *const NAMES[5] = {"x", "y", "z", "e0", "e1"};
static const int OCTO_INDEX[5] = {OX, OY, OZ, OE0, OE1};

const char *octoAxisName(uint8_t axis) { return axis < 5 ? NAMES[axis] : "s"; }

static std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
static std::string fmt(const char *f, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof(b), f, ap);
    va_end(ap);
    return b;
}

bool Motion::command(const std::string &line)
{
    const OctoReply r = octo.call(line);
    if (!r.ok)
    {
        lastError_ = r.error.empty() ? "motion board offline" : r.error;
        return false;
    }
    // A status line that arrives after this "ok" already includes the move
    moveSeqStatus_ = octo.status().seq;
    movePending_ = true;
    return true;
}

void Motion::syncTargets()
{
    const OctoStatus s = octo.status();
    for (int a = 0; a < 5; a++)
        target_[a] = s.pos[OCTO_INDEX[a]];
    nozzle_ = s.pos[OROT];
    targetsValid_ = s.valid;
}

bool Motion::moveTo(float x, float y, float z, float feedrate)
{
    if (!targetsValid_)
        syncTargets();
    if (!command(fmt("MOVE x=%.4f y=%.4f z=%.4f speed=%.3f", x, y, z, feedrate)))
        return false;
    target_[X_AXIS] = x;
    target_[Y_AXIS] = y;
    target_[Z_AXIS] = z;
    return true;
}

bool Motion::rotate(float degrees, float speed)
{
    if (!targetsValid_)
        syncTargets();
    if (!command(fmt("E0 %.4f speed=%.3f", degrees, speed)))
        return false;
    target_[R_AXIS] += degrees;
    return true;
}

bool Motion::moveAxisBy(uint8_t axis, float delta, float speed)
{
    if (axis >= 5)
        return false;
    if (!targetsValid_)
        syncTargets();
    if (!command(fmt("JOG %s=%.4f speed=%.3f", NAMES[axis], delta, speed)))
        return false;
    target_[axis] += delta;
    return true;
}

bool Motion::moveE1To(float mm, float speed)
{
    if (!targetsValid_)
        syncTargets();
    if (!command(fmt("E1 %.4f speed=%.3f", mm, speed)))
        return false;
    target_[E1_AXIS] = mm;
    return true;
}

void Motion::setE1Position(float mm)
{
    if (command(fmt("SETPOS e1=%.4f", mm)))
        target_[E1_AXIS] = mm;
}

bool Motion::homeE1()
{
    const bool ok = command("HOME E1");
    delay(120); // let a fresh !status arrive
    syncTargets();
    return ok;
}

bool Motion::isE1Homed() { return octo.status().e1Homed; }

void Motion::startFeederProbe(float toMm, float speed)
{
    probeTicket_ = octo.send(fmt("PROBE e1=%.3f speed=%.3f", toMm, speed));
}

int8_t Motion::pollFeederProbe()
{
    if (!probeTicket_)
        return -1;
    OctoReply r;
    if (!octo.poll(probeTicket_, r))
        return 0;
    probeTicket_ = 0;
    syncTargets();
    const Record *p = r.first("probe");
    if (r.ok && p && p->flag("hit"))
    {
        target_[E1_AXIS] = p->num("e1", target_[E1_AXIS]);
        return 1;
    }
    return -1;
}

void Motion::cancelFeederProbe()
{
    if (!probeTicket_)
        return;
    octo.call("HALT"); // aborts the PROBE on the board
    OctoReply r;
    const uint32_t t0 = millis();
    while (!octo.poll(probeTicket_, r) && millis() - t0 < 3000)
        delay(5);
    probeTicket_ = 0;
    syncTargets();
}

bool Motion::feederSwitchHit()
{
    const OctoReply r = octo.call("SENSORS");
    const Record *s = r.first("sensors");
    return s && s->flag("feeder");
}

bool Motion::e1HomeSwitchHit()
{
    const OctoReply r = octo.call("SENSORS");
    const Record *s = r.first("sensors");
    return s && s->flag("e1_home");
}

bool Motion::home(bool zFlag)
{
    const bool ok = command(zFlag ? "HOME XY Z" : "HOME XY");
    delay(120);
    syncTargets();
    return ok;
}

bool Motion::rotateNozzleTo(float degrees, float speed)
{
    if (!command(fmt("ROTATE %.4f speed=%.3f", degrees, speed)))
        return false;
    nozzle_ = degrees;
    return true;
}

float Motion::nozzleAngle() { return nozzle_; }

bool Motion::pasteMicrolitres(float ul) { return command(fmt("PASTE %.5f", ul)); }

bool Motion::isBusy()
{
    if (!octo.connected())
        return false;
    const OctoStatus s = octo.status();
    // Not yet a status taken after the last move was queued: count as busy
    if (movePending_ && s.seq > moveSeqStatus_)
        movePending_ = false;
    return s.busy || movePending_;
}

void Motion::waitUntilIdle()
{
    command("WAIT");
}

void Motion::stop()
{
    command("HALT");
    delay(60);
    syncTargets();
}

void Motion::emergencyStop()
{
    octo.call("ESTOP");
    delay(60);
    syncTargets();
}

void Motion::enableMotors(bool on) { command(on ? "MOTORS ON" : "MOTORS OFF"); }

void Motion::setPosition(float x, float y, float z)
{
    if (command(fmt("SETPOS x=%.4f y=%.4f z=%.4f", x, y, z)))
    {
        target_[X_AXIS] = x;
        target_[Y_AXIS] = y;
        target_[Z_AXIS] = z;
    }
}

void Motion::setRotation(float degrees)
{
    if (command(fmt("SETPOS e0=%.4f", degrees)))
        target_[R_AXIS] = degrees;
}

float Motion::getPosition(uint8_t axis)
{
    const OctoStatus s = octo.status();
    return axis < 5 ? s.pos[OCTO_INDEX[axis]] : 0.0f;
}

float Motion::getTargetPosition(uint8_t axis)
{
    if (!targetsValid_)
        syncTargets();
    return axis < 5 ? target_[axis] : 0.0f;
}

float Motion::getMaxSpeed(uint8_t axis) { return octo.valueNum(std::string("speed_") + octoAxisName(axis), 0); }

bool Motion::isHomed() { return octo.status().homed; }

uint8_t Motion::alarm()
{
    const OctoStatus s = octo.status();
    return (uint8_t)s.alarm;
}

void Motion::clearAlarm()
{
    command("CLEAR");
    syncTargets();
}
