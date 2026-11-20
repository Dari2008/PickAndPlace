#include "commands.h"
#include <cstring>
#include <strings.h>
#include <motion.h>
#include "../feeder_scan.h"
#include "../job/runner.h"

#include "../../octo/octopus.h"
#include "../mosfets.h"

struct Command
{
    char name[9];
    CommandHandler fn;
};

static std::string g_line;

static const Command TABLE[] PROGMEM = {
    {"HELP", cmdHelp}, {"INFO", cmdInfo}, {"STATUS", cmdStatus}, {"REPORT", cmdReport}, {"WAIT", cmdWait}, {"LOG", cmdLog},
    {"SD", cmdSd}, {"PWD", cmdPwd}, {"CD", cmdCd}, {"LS", cmdLs}, {"MKDIR", cmdMkdir}, {"RMDIR", cmdRmdir},
    {"RM", cmdRm}, {"MV", cmdMv}, {"CAT", cmdCat}, {"UPLOAD", cmdUpload},
    {"CHECK", cmdCheck}, {"RUN", cmdRun}, {"PAUSE", cmdPause}, {"RESUME", cmdResume},
    {"CONTINUE", cmdContinue}, {"SKIP", cmdSkip}, {"STOP", cmdStop}, {"JOB", cmdJob},
    {"HOME", cmdHome}, {"MOVE", cmdMove}, {"JOG", cmdJog}, {"JOGV", cmdJogV}, {"E1", cmdE1}, {"E0", cmdE0}, {"ROTATE", cmdRotate}, {"PASTE", cmdPaste},
    {"MOTOR", cmdMotor}, {"SETPOS", cmdSetPos}, {"POS", cmdPos}, {"MOTORS", cmdMotors},
    {"HALT", cmdHalt}, {"ESTOP", cmdEstop}, {"CLEAR", cmdClear}, {"SENSORS", cmdSensors},
    {"PUMP", cmdPump}, {"VALVE", cmdValve}, {"OUTPUTS", cmdOutputs},
    {"GET", cmdGet}, {"SET", cmdSet}, {"RELOAD", cmdReload}, {"DEFAULTS", cmdDefaults},
    {"FEEDERS", cmdFeeders}, {"FEEDER", cmdFeeder}, {"POCKET", cmdPocket}, {"SCAN", cmdScan}, {"NFC", cmdNfc},
    {"LIGHT", cmdLight}, {"TMC", cmdTmc}, {"OCTO", cmdOcto},
    {"CAM", cmdCam}, {"VACUUM", cmdVacuum}, {"JOY", cmdJoy}, {"CAL", cmdCal}, {"SCREEN", cmdScreen},
    {"NOZZLES", cmdNozzles}, {"SETTINGS", cmdSettings}, {"OCTOSD", cmdOctoSd}, {"LOADCELL", cmdLoadcell}, {"SGTEST", cmdSgTest}, {"FINDLEN", cmdFindLen}, {"NOZZLE", cmdNozzle},
};
static const uint8_t COUNT = sizeof(TABLE) / sizeof(TABLE[0]);

// Settings / feeders changed by any client (website, console, calibration): "!changed what=..."
// so the others (the display's copies) reload them.
static void announceChange(const char *name, const std::string &line)
{
    const char *what = nullptr;
    if (!strcmp(name, "SET") || !strcmp(name, "DEFAULTS") || !strcmp(name, "RELOAD") || !strcmp(name, "CAL"))
        what = "settings";
    else if ((!strcmp(name, "FEEDER") || !strcmp(name, "POCKET")) && line.find('=') != std::string::npos)
        what = "feeders";
    else if (!strcmp(name, "NOZZLE"))
        what = "nozzles";
    if (!what)
        return;
    Print &p = ports.all;
    p.print("!changed what=");
    p.println(what);
}

namespace commands
{
    const std::string &currentLine() { return g_line; }

    void execute(Port &port, char *line)
    {
        g_line = line;
        if (uploadActive(port))
        {
            uploadLine(port, line);
            return;
        }
        Args args(line);
        if (!args.count())
            return; // blank line or comment: no answer
        for (uint8_t i = 0; i < COUNT; i++)
        {
            if (!strcasecmp_P(args[0], TABLE[i].name))
            {
                const CommandHandler fn = (CommandHandler)pgm_read_ptr(&TABLE[i].fn);
                const char *name = TABLE[i].name;
                fn(port, args);
                announceChange(name, g_line);
                return;
            }
        }
        out::error2(port.io, PSTR("unknown command"), args[0]);
    }

    void urgentLine(const std::string &line)
    {
        auto is = [&](const char *w) {
            const size_t n = strlen(w);
            return line.size() >= n && strncasecmp(line.c_str(), w, n) == 0 && (line.size() == n || line[n] == ' ');
        };
        if (is("ESTOP"))
            octo.urgent("ESTOP");
        else if (is("HALT") && !runner.active() && !feederScan.active()) // as cmdHalt: not during a job
            octo.urgent("HALT");
    }

    bool refuseIfBusy(Port &port)
    {
        if (runner.active())
        {
            out::error(port.io, PSTR("busy: a job is running"));
            return true;
        }
        if (feederScan.active())
        {
            out::error(port.io, PSTR("busy: auto detect is running"));
            return true;
        }
        return false;
    }

    // Join `arg` onto the working folder and resolve "." and "..".
    bool resolvePath(const Port &port, const char *arg, char *outPath)
    {
        char tmp[PATH_MAX_LEN * 2];
        if (arg[0] == '/')
            tmp[0] = '\0';
        else
        {
            strcpy(tmp, port.cwd);
            strcat_P(tmp, PSTR("/"));
        }
        if (strlen(tmp) + strlen(arg) >= sizeof(tmp))
            return false;
        strcat(tmp, arg);

        // Rebuild segment by segment.
        char *o = outPath;
        *o = '\0';
        char *seg = tmp;
        while (seg)
        {
            char *slash = strchr(seg, '/');
            if (slash)
                *slash = '\0';
            if (!*seg || !strcmp_P(seg, PSTR(".")))
            {
            }
            else if (!strcmp_P(seg, PSTR("..")))
            {
                char *last = strrchr(outPath, '/');
                if (last)
                    *last = '\0';
                o = outPath + strlen(outPath);
            }
            else
            {
                const uint8_t len = strlen(seg);
                if ((o - outPath) + len + 2 > PATH_MAX_LEN)
                    return false;
                *o++ = '/';
                memcpy(o, seg, len);
                o += len;
                *o = '\0';
            }
            seg = slash ? slash + 1 : nullptr;
        }
        if (!outPath[0])
            strcpy_P(outPath, PSTR("/"));
        return true;
    }

    void printStatus(Print &p)
    {
        PGM_P state = PSTR("idle");
        const OctoStatus os = octo.status();
        if (!octo.connected())
            state = PSTR("offline");
        else if (motion.alarm())
            state = PSTR("alarm");
        else if (runner.active())
            state = runner.stateName();
        else if (feederScan.active())
            state = PSTR("scanning");
        out::strP(p, PSTR("state"), state);
        out::flt(p, PSTR("x"), motion.getPosition(X_AXIS), 2);
        out::flt(p, PSTR("y"), motion.getPosition(Y_AXIS), 2);
        out::flt(p, PSTR("z"), motion.getPosition(Z_AXIS), 2);
        out::flt(p, PSTR("e0"), motion.getPosition(R_AXIS), 1);
#if HAS_E1
        out::flt(p, PSTR("e1"), motion.getPosition(E1_AXIS), 2);
#endif
#if HAS_TOOL
        out::flt(p, PSTR("rot"), nozzleAngle(), 1);
#endif
        out::num(p, PSTR("homed"), motion.isHomed());
#if HAS_E1
        out::num(p, PSTR("e1_homed"), motion.isE1Homed());
#endif
        out::num(p, PSTR("busy"), motion.isBusy());
        out::num(p, PSTR("alarm"), motion.alarm());
        if (os.alarm)
            out::str(p, PSTR("alarm_text"), os.alarmText.c_str());
        out::flt(p, PSTR("paste"), os.pos[OPASTE], 3);
        out::num(p, PSTR("motors"), os.motors);
        out::num(p, PSTR("in"), (long)os.inputs);
        printOutputs(p);
        out::num(p, PSTR("sd"), sdMounted());
        out::num(p, PSTR("octo"), octo.connected());
        printExtraStatus(p); // vacuum, cameras (cmd_vision.cpp)
    }

    void report(Print &p)
    {
        p.print(F("!status"));
        printStatus(p);
        p.println();
        if (runner.active())
        {
            p.print(F("!job"));
            runner.printJob(p);
            p.println();
        }
    }
}
