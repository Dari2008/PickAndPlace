// HELP INFO STATUS REPORT WAIT LOG
#include "commands.h"
#include "../PINS.h"

#include "../../octo/octopus.h"

#define FIRMWARE_VERSION "1.0"

void cmdHelp(Port &port, Args &)
{
    static const char NAMES[] PROGMEM =
        "HELP INFO STATUS REPORT WAIT LOG SD PWD CD LS MKDIR RMDIR RM MV CAT UPLOAD "
        "CHECK RUN PAUSE RESUME CONTINUE SKIP STOP JOB HOME MOVE JOG E1 E0 ROTATE PASTE MOTOR SETPOS POS "
        "MOTORS HALT ESTOP CLEAR SENSORS PUMP VALVE OUTPUTS GET SET RELOAD DEFAULTS "
        "FEEDERS FEEDER POCKET SCAN NFC LIGHT TMC OCTO CAM VACUUM JOY CAL SCREEN NOZZLES NOZZLE SETTINGS OCTOSD LOADCELL";
    Print &p = port.io;
    PGM_P s = NAMES;
    char c;
    p.print(F("cmd name="));
    while ((c = pgm_read_byte(s++)) != '\0')
    {
        if (c == ' ')
            p.print(F("\ncmd name="));
        else
            p.write(c);
    }
    p.println();
    p.println(F("info see protocol.md"));
    out::ok(p);
}

void cmdInfo(Port &port, Args &)
{
    Print &p = port.io;
    out::rec(p, PSTR("info"));
    out::strP(p, PSTR("firmware"), PSTR("pap-core"));
    out::strP(p, PSTR("version"), PSTR(FIRMWARE_VERSION));
    out::strP(p, PSTR("board"), PSTR("Raspberry Pi 5 + BTT Octopus"));
    out::str(p, PSTR("octopus"), octo.device().c_str());
    out::num(p, PSTR("octo"), octo.connected());
    p.println();
    out::ok(p);
}

void cmdStatus(Port &port, Args &)
{
    out::rec(port.io, PSTR("status"));
    commands::printStatus(port.io);
    port.io.println();
    out::ok(port.io);
}

void cmdReport(Port &port, Args &args)
{
    float ms;
    if (args.count() < 2 || !Args::number(args[1], ms) || ms < 0)
    {
        out::error(port.io, PSTR("usage: REPORT <ms>"));
        return;
    }
    port.reportMs = ms == 0 ? 0 : (uint16_t)constrain(ms, 100.0f, 60000.0f);
    out::ok(port.io);
}

// Answer once all queued moves are done (other ports keep working).
void cmdWait(Port &port, Args &)
{
    port.answerWhenIdle();
}

// LOG [0|1|2]: copy the ESP32 link traffic to USB (see ports.h)
void cmdLog(Port &port, Args &args)
{
    if (args.count() >= 2)
    {
        float v;
        if (!Args::number(args[1], v) || v < 0 || v > 2)
        {
            out::error(port.io, PSTR("usage: LOG [0|1|2]"));
            return;
        }
        linkLogLevel = (uint8_t)v;
    }
    port.io.print(F("log level="));
    port.io.println(linkLogLevel);
    out::ok(port.io);
}

// ---- the "!ready" event at start-up ------------------------------------------
void announceReady()
{
    ports.all.print(F("!ready"));
    out::strP(ports.all, PSTR("firmware"), PSTR("pap-core"));
    out::strP(ports.all, PSTR("version"), PSTR(FIRMWARE_VERSION));
    ports.all.println();
}
