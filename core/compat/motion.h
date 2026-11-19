/**
 * motion.h - the Motion API of the ATmega firmware (../../../lib/Motion/motion.h), on the Pi:
 *            every call becomes a command to the Octopus (../octo/octopus.h)
 * -----------------------------------------------------------------------------
 * The ported controller code (job runner, feeder commands, auto detect) keeps
 * calling motion.moveTo(), motion.isBusy() ... as before. Axis numbers keep the
 * ATmega meaning: X Y Z, R = E0 (feeder motor, deg), E1 = gantry (mm).
 * The nozzle rotation and the paste extruder have their own drivers on the
 * Octopus now (ROTATE / PASTE), so the old tool switch is gone.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <cstdint>
#include <string>

#define HAS_E1 1
#define HAS_TOOL 1
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2
#define R_AXIS 3  // E0: feeder motor, degrees
#define E1_AXIS 4 // gantry, mm
#define N_USER_AXES 5
#define TOOL_PASTE 0
#define TOOL_ROTATE 1

// Values the old code took from motion_config.h
#define Z_MAX_RATE 5.0f
#define E1_DEFAULT_SPEED 30.0f
#define ROT_DEFAULT_SPEED 180.0f
#define FEEDER_SCAN_SPEED 5.0f
#define FEEDER_SWITCH_RELEASE 30.0f
#define ALARM_NONE 0

class Motion
{
public:
    // Queue a move to absolute (x, y, z) mm at `feedrate` mm/s. False if rejected / offline.
    bool moveTo(float x, float y, float z, float feedrate);
    // Feeder motor (E0) relative, degrees
    bool rotate(float degrees, float speed = ROT_DEFAULT_SPEED);
    bool moveAxisBy(uint8_t axis, float delta, float speed);
    bool moveE1To(float mm, float speed = E1_DEFAULT_SPEED);
    void setE1Position(float mm);
    bool homeE1();
    bool isE1Homed();

    void startFeederProbe(float toMm, float speed);
    int8_t pollFeederProbe();
    void cancelFeederProbe();
    bool feederSwitchHit();
    bool e1HomeSwitchHit();

    bool home(bool zFlag);

    // Nozzle and paste on their own Octopus drivers
    bool rotateNozzleTo(float degrees, float speed); // absolute degrees
    float nozzleAngle();
    bool pasteMicrolitres(float ul); // relative, at the Octopus paste_speed

    bool isBusy();
    void waitUntilIdle();
    void stop();
    void emergencyStop();
    void enableMotors(bool on);
    void setPosition(float x, float y, float z);
    void setRotation(float degrees);

    float getPosition(uint8_t axis);
    float getTargetPosition(uint8_t axis);
    void setMaxSpeed(uint8_t, float) {} // Octopus setting speed_<axis> now
    float getMaxSpeed(uint8_t axis);
    bool isHomed();
    uint8_t alarm();
    void clearAlarm();
    uint8_t queueSpace() { return 8; } // the Octopus queues 32 moves; MOVE waits when it is full

    // After HOME, HALT, SETPOS, (re)connect: take the targets from the board again
    void syncTargets();
    void invalidateTargets() { targetsValid_ = false; } // next getTargetPosition() reads the board
    std::string lastError() const { return lastError_; }

private:
    bool command(const std::string &line);
    float target_[5] = {0};
    float nozzle_ = 0;
    bool targetsValid_ = false;
    uint64_t probeTicket_ = 0;
    uint64_t moveSeqStatus_ = 0; // status seq seen when the last move was acknowledged
    bool movePending_ = false;   // a command was acknowledged, no status taken after it yet
    std::string lastError_;
};

extern Motion motion;
const char *octoAxisName(uint8_t axis); // ATmega axis number -> Octopus protocol name
