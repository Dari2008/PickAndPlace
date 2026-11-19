// HOME MOVE JOG E1 E0 ROTATE PASTE MOTOR SETPOS POS MOTORS HALT ESTOP CLEAR SENSORS
// PUMP VALVE LIGHT OUTPUTS TMC OCTO
// On the Pi these are carried out by the Octopus: the line goes there unchanged
// (same protocol, octopus-fw/README.md) and its answer comes back. The Pi adds the
// busy check (job / auto detect) and keeps its target positions in step.
#include "commands.h"
#include <motion.h>
#include "../../octo/octopus.h"
#include "../mosfets.h"
#include "../settings/general_settings.h"

// Send the command line as it came in and copy the board's answer to the port.
static bool forward(Port &port, const std::string &line, int timeoutMs = 0)
{
    const OctoReply r = octo.call(line, timeoutMs);
    for (const Record &rec : r.records)
        port.io.println(rec.text.c_str());
    if (r.ok)
        out::ok(port.io);
    else
        out::error(port.io, r.error.empty() ? "motion board offline" : r.error.c_str());
    return r.ok;
}

static bool forwardMoving(Port &port)
{
    if (commands::refuseIfBusy(port))
        return false;
    const bool ok = forward(port, commands::currentLine());
    motion.invalidateTargets();
    return ok;
}

float nozzleAngle() { return octo.status().pos[OROT]; }

void cmdHome(Port &port, Args &)
{
    if (commands::refuseIfBusy(port))
        return;
    forward(port, commands::currentLine(), 300000);
    delay(120);
    motion.syncTargets();
}

void cmdMove(Port &port, Args &) { forwardMoving(port); }
void cmdJog(Port &port, Args &) { forwardMoving(port); }
void cmdE1(Port &port, Args &) { forwardMoving(port); }
void cmdE0(Port &port, Args &) { forwardMoving(port); }
void cmdMotor(Port &port, Args &) { forwardMoving(port); }
void cmdPaste(Port &port, Args &) { forwardMoving(port); }

void cmdRotate(Port &port, Args &args)
{
    if (commands::refuseIfBusy(port))
        return;
    float deg;
    if (args.count() < 2 || !Args::number(args[1], deg))
    {
        out::error(port.io, PSTR("usage: ROTATE <deg>"));
        return;
    }
    if (motion.rotateNozzleTo(deg, octo.valueNum("rot_speed", 180)))
        out::ok(port.io);
    else
        out::error(port.io, motion.lastError().c_str());
}

void cmdSetPos(Port &port, Args &)
{
    if (commands::refuseIfBusy(port))
        return;
    forward(port, commands::currentLine());
    delay(60);
    motion.syncTargets();
}

void cmdPos(Port &port, Args &) { forward(port, "POS"); }

void cmdMotors(Port &port, Args &)
{
    if (commands::refuseIfBusy(port))
        return;
    forward(port, commands::currentLine());
}

void cmdHalt(Port &port, Args &)
{
    if (commands::refuseIfBusy(port))
        return;
    forward(port, "HALT");
    delay(60);
    motion.syncTargets();
}

// Always allowed: also stops a job's motion at once (the job fails on it).
void cmdEstop(Port &port, Args &)
{
    forward(port, "ESTOP");
    delay(60);
    motion.syncTargets();
}

void cmdClear(Port &port, Args &)
{
    forward(port, "CLEAR");
    motion.syncTargets();
}

void cmdSensors(Port &port, Args &) { forward(port, "SENSORS"); }
void cmdOutputs(Port &port, Args &) { forward(port, "OUTPUTS"); }

// PUMP ON uses pump_power of the Octopus; the rest are passed on as they are
void cmdPump(Port &port, Args &) { forward(port, commands::currentLine()); }
void cmdValve(Port &port, Args &) { forward(port, commands::currentLine()); }
void cmdLight(Port &port, Args &) { forward(port, commands::currentLine()); }

void cmdTmc(Port &port, Args &)
{
    if (commands::refuseIfBusy(port))
        return;
    forward(port, commands::currentLine(), 30000);
}

// LOADCELL [TARE | CAL <grams>]: the load cell on the Octopus (reading, zero, calibration)
void cmdLoadcell(Port &port, Args &)
{
    forward(port, commands::currentLine(), 10000);
}

// SGTEST <x|y|e1> [mm] [speed=] [cur=]: StallGuard measurement on the Octopus (calibrating sensorless
// homing); the axis runs mm away from its home end and back
void cmdSgTest(Port &port, Args &)
{
    if (commands::refuseIfBusy(port))
        return;
    forward(port, commands::currentLine(), 120000);
    delay(60);
    motion.syncTargets();
}

// OCTO <line>: any command straight to the Octopus (service / debugging)
void cmdOcto(Port &port, Args &args)
{
    if (args.count() < 2)
    {
        out::error(port.io, PSTR("usage: OCTO <command line for the Octopus>"));
        return;
    }
    const std::string &l = commands::currentLine();
    size_t i = l.find_first_of(" \t");
    while (i < l.size() && (l[i] == ' ' || l[i] == '\t'))
        i++;
    forward(port, l.substr(i), 300000);
    motion.invalidateTargets();
}

void printOutputs(Print &p)
{
    const OctoStatus s = octo.status();
    out::num(p, PSTR("pump"), s.pump);
    out::num(p, PSTR("pump_used"), s.pumpUsed);
    out::num(p, PSTR("valve"), s.valve);
    out::num(p, PSTR("cam_fan"), s.camFan);
    out::num(p, PSTR("box_fan"), s.boxFan);
    out::num(p, PSTR("pi_fan"), s.piFan);
    out::num(p, PSTR("light_top"), s.lightTop);
    out::num(p, PSTR("light_bottom"), s.lightBottom);
}
