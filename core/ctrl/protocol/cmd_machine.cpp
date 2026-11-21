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

// HOME as on the board. With visual homing on (vhome) and X + Y homed, the top camera then finds the
// bottom camera and corrects X / Y: "vhome dx= dy=" before the ok, or an error.
void cmdHome(Port &port, Args &args)
{
    if (commands::refuseIfBusy(port))
        return;
    const OctoReply r = octo.call(commands::currentLine(), 300000);
    for (const Record &rec : r.records)
        port.io.println(rec.text.c_str());
    delay(120);
    motion.syncTargets();
    if (!r.ok)
    {
        out::error(port.io, r.error.empty() ? "motion board offline" : r.error.c_str());
        return;
    }
    // X and Y both homed by this command: no axis words, or XY / ALL, or X and Y
    bool x = args.count() < 2, y = x;
    for (uint8_t i = 1; i < args.count(); i++)
    {
        x |= args.is(i, "X") || args.is(i, "XY") || args.is(i, "ALL");
        y |= args.is(i, "Y") || args.is(i, "XY") || args.is(i, "ALL");
    }
    if (x && y && settings.vhome)
    {
        float dx, dy;
        std::string err;
        if (!commands::visualHome(dx, dy, err))
        {
            out::error2(port.io, "visual homing:", err.c_str());
            return;
        }
        char line[64];
        snprintf(line, sizeof(line), "vhome dx=%.3f dy=%.3f", dx, dy);
        port.io.println(line);
    }
    out::ok(port.io);
}

// ---- crash guard -------------------------------------------------------------------------------
// Besides the board's own soft limits (and in case its firmware is older): once X + Y are homed, a MOVE
// outside 0 .. travel_x / _y is refused and a JOG is shortened to the edge (refused when it can't move at
// all); the same for E1 once the gantry is homed. Homing, SGTEST and FINDLEN go to the ends on purpose.
static bool limitFor(int axis, float &mx)
{
    const OctoStatus s = octo.status();
    if (!s.valid)
        return false;
    if ((axis == OX || axis == OY) && s.homed)
        mx = octo.valueNum(axis == OX ? "travel_x" : "travel_y", 0);
    else if (axis == OE1 && s.e1Homed)
        mx = octo.valueNum("travel_e1", 0);
    else
        return false;
    return mx > 0;
}

// The line with targets kept inside the limits, or false + err
static bool guardLine(Args &args, bool relative, std::string &line, std::string &err)
{
    static const struct
    {
        const char *key;
        int axis;
        const char *name;
    } AX[] = {{"x", OX, "X"}, {"y", OY, "Y"}, {"e1", OE1, "E1"}};
    const OctoStatus s = octo.status();
    line = args[0];
    bool moves = false, anyAxis = false;
    for (uint8_t i = 1; i < args.count(); i++)
    {
        std::string w = args[i];
        const size_t eq = w.find('=');
        for (const auto &a : AX)
        {
            float mx;
            if (eq == std::string::npos || strcasecmp(w.substr(0, eq).c_str(), a.key) != 0)
                continue;
            anyAxis = true;
            const float v = (float)atof(w.c_str() + eq + 1), from = s.pos[a.axis], to = relative ? from + v : v;
            if (!limitFor(a.axis, mx))
            {
                moves = true;
                break;
            }
            char t[96];
            if (to >= -0.001f && to <= mx + 0.001f)
            {
                moves = moves || fabsf(to - from) > 0.0005f;
                break;
            }
            if (!relative)
            {
                snprintf(t, sizeof(t), "%s=%.2f would crash: outside 0 .. %.1f (travel_%s)", a.name, (double)to, (double)mx, a.key);
                err = t;
                return false;
            }
            const float edge = to < 0 ? 0 : mx, nv = edge - from;
            if (fabsf(nv) > 0.0005f)
                moves = true;
            snprintf(t, sizeof(t), "%s=%.4f", a.key, (double)nv);
            w = t;
            break;
        }
        line += " " + w;
    }
    if (relative && anyAxis && !moves)
    {
        err = "at the end of travel: it would crash (soft limit)";
        return false;
    }
    return true;
}

static void forwardGuarded(Port &port, Args &args, bool relative)
{
    if (commands::refuseIfBusy(port))
        return;
    std::string line, err;
    if (!guardLine(args, relative, line, err))
    {
        out::error(port.io, err.c_str());
        return;
    }
    forward(port, line);
    motion.invalidateTargets();
}

void cmdMove(Port &port, Args &args) { forwardGuarded(port, args, false); }
void cmdJog(Port &port, Args &args) { forwardGuarded(port, args, true); }
// JOGV x= y= z= e1= (mm/s): velocity jog of the joystick, stops on JOGV 0 or 500 ms without a JOGV.
// Answered at once and passed on without waiting: a blocked command loop must not leave gaps in the
// stream (the board would stop and start again).
void cmdJogV(Port &port, Args &)
{
    if (commands::refuseIfBusy(port))
        return;
    octo.post(commands::currentLine());
    motion.invalidateTargets();
    out::ok(port.io);
}
void cmdE1(Port &port, Args &args)
{
    // E1 <mm> absolute (other forms passed on as they are)
    float v, mx;
    if (args.count() >= 2 && Args::number(args[1], v) && limitFor(OE1, mx) && (v < -0.001f || v > mx + 0.001f))
    {
        char t[96];
        snprintf(t, sizeof(t), "E1=%.2f would crash: outside 0 .. %.1f (travel_e1)", (double)v, (double)mx);
        out::error(port.io, t);
        return;
    }
    forwardMoving(port);
}
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

// FINDLEN <x|y|e1>: home -> far end until it stalls, answers the length (octopus-fw README)
void cmdFindLen(Port &port, Args &)
{
    if (commands::refuseIfBusy(port))
        return;
    forward(port, commands::currentLine(), 300000);
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
