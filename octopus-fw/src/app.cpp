#include "toolbus.h"
#include "app.h"
#include "dfu.h"
#include "pap_board.h"
#include "motion/motion.h"
#include "motion/planner.h"
#include "motion/stepper.h"
#include "outputs.h"
#include "lights.h"
#include "loadcell.h"
#include "sdcard.h"
#include "proto/args.h"
#include "settings.h"
#include "tmc.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <strings.h>

namespace app
{
    namespace
    {
        Sink *out = nullptr;

        // ---- input line assembly (also used inside blocking waits) -------------------
        constexpr size_t LINE_MAX = 200;
        char lineBuf[LINE_MAX + 1];
        size_t lineLen = 0;
        bool lineOverflow = false;
        // Lines that arrived while a command was blocking (HOME, PROBE): handled afterwards
        constexpr uint8_t PENDING_MAX = 8;
        char pending[PENDING_MAX][LINE_MAX + 1];
        uint8_t pendingHead = 0, pendingCount = 0;
        bool inBlocking = false;
        bool abortRequested = false;

        uint32_t lastRxMs = 0;
        bool hostSeen = false;
        uint32_t reportMs = 0, lastReport = 0;
        bool waitPending = false;
        bool estopWasHit = false;
        const char *alarmText = "";

        // ---------------------------------------------------------------------------------
        void ok() { out->ok(); }
        void error(const std::string &t) { out->error(t); }
        void event(const std::string &l) { out->line(l); }

        void alarmEvent(const char *text)
        {
            alarmText = text;
            Line l("!alarm");
            l.str("text", text);
            event(l.s);
        }

        // Read USB bytes into lines. Returns a complete line (NUL-terminated) or nullptr.
        char *readLine()
        {
            while (SerialUSB.available())
            {
                const int c = SerialUSB.read();
                if (c < 0)
                    break;
                lastRxMs = millis();
                hostSeen = true;
                if (c == '\r')
                    continue;
                if (c == '\n')
                {
                    lineBuf[lineLen] = '\0';
                    const bool over = lineOverflow;
                    lineLen = 0;
                    lineOverflow = false;
                    if (over)
                    {
                        error("line too long");
                        continue;
                    }
                    return lineBuf;
                }
                if (lineLen < LINE_MAX)
                    lineBuf[lineLen++] = (char)c;
                else
                    lineOverflow = true;
            }
            return nullptr;
        }

        bool isWord(const char *line, const char *word)
        {
            while (*line == ' ' || *line == '\t')
                line++;
            const size_t n = strlen(word);
            return !strncasecmp(line, word, n) && (line[n] == '\0' || line[n] == ' ' || line[n] == '\t');
        }

        // e-stop button: edge to "hit" -> halt + outputs off + alarm
        void checkEstopInput()
        {
            const int n = (int)cfg::get(cfg::ESTOP_INPUT);
            if (n < 0)
                return;
            const bool hit = input_hit((uint8_t)n);
            if (hit && !estopWasHit)
            {
                outputs.allOff();
                lights::allOff();
                motion.raiseAlarm(ALARM_ESTOP);
                alarmEvent("e-stop button");
            }
            estopWasHit = hit;
        }

        // Polled by Motion while it blocks: ESTOP / HALT lines and the e-stop button abort.
        bool abortHook()
        {
            checkEstopInput();
            loadcell::poll();
            if (motion.alarm() != ALARM_NONE)
                return true;
            char *l = readLine();
            if (l)
            {
                if (isWord(l, "ESTOP") || isWord(l, "HALT"))
                    abortRequested = true;
                if (pendingCount < PENDING_MAX)
                {
                    strcpy(pending[(pendingHead + pendingCount) % PENDING_MAX], l);
                    pendingCount++;
                }
            }
            return abortRequested;
        }

        // ---- status ------------------------------------------------------------------------
        const char *stateName()
        {
            if (motion.alarm() != ALARM_NONE)
                return "alarm";
            if (inBlocking)
                return "homing";
            return motion.isBusy() ? "moving" : "idle";
        }

        void addStatus(Line &l)
        {
            l.str("state", stateName());
            l.flt("x", motion.getPosition(X_AXIS), 3);
            l.flt("y", motion.getPosition(Y_AXIS), 3);
            l.flt("z", motion.getPosition(Z_AXIS), 3);
            l.flt("e0", motion.getPosition(E0_AXIS), 2);
            l.flt("e1", motion.getPosition(E1_AXIS), 3);
            l.flt("rot", motion.getPosition(ROT_AXIS), 2);
            l.flt("paste", motion.getPosition(PASTE_AXIS), 3);
            l.flt("x_skew", motion.lastXSkew(), 2);
            l.num("homed", motion.isHomed());
            l.num("e1_homed", motion.isE1Homed());
            l.num("busy", motion.isBusy());
            l.num("alarm", motion.alarm());
            if (motion.alarm() != ALARM_NONE)
                l.str("alarm_text", alarmText);
            l.num("motors", motion.motorsOn());
            l.num("pump", lroundf(outputs.get(OUT_PUMP) * 100));
            l.num("pump_used", cfg::get(cfg::PUMP_USED) > 0.5f);
            l.num("valve", outputs.get(OUT_VALVE) > 0);
            l.num("cam_fan", lroundf(outputs.get(OUT_CAM_FAN) * 100));
            l.num("box_fan", lroundf(outputs.get(OUT_BOX_FAN) * 100));
            l.num("pi_fan", lroundf(outputs.get(OUT_PI_FAN) * 100));
            l.num("light_top", lroundf(lights::top() * 100));
            l.num("light_bottom", lroundf(lights::bottom() * 100));
            l.num("in", inputs_hit_mask());
            l.num("noz", toolbus_value());
            l.num("noz_ok", toolbus_fresh());
        }

        // ---- helpers -----------------------------------------------------------------------
        bool refuseAlarm()
        {
            if (motion.alarm() != ALARM_NONE)
            {
                error(std::string("alarm: ") + alarmText + " (CLEAR first)");
                return true;
            }
            return false;
        }

        // Speed along the path, Z share capped at its own max speed (lead screw)
        float zLimitedSpeed(const float *from, const float *to, float speed)
        {
            const float dz = fabsf(to[Z_AXIS] - from[Z_AXIS]);
            if (dz < 0.0005f)
                return speed;
            float len2 = 0;
            for (uint8_t a = 0; a < N_AXIS; a++)
                len2 += (to[a] - from[a]) * (to[a] - from[a]);
            const float zMax = cfg::axis(cfg::SPEED, Z_AXIS);
            return fminf(speed, zMax * sqrtf(len2) / dz);
        }

        // MOVE (absolute) / JOG (relative): any axis by name
        void moveCmd(Args &a, bool relative)
        {
            if (refuseAlarm())
                return;
            float from[N_AXIS], t[N_AXIS];
            motion.queuedTargets(from);
            memcpy(t, from, sizeof(t));
            bool bad = false, any = false;
            for (uint8_t ax = 0; ax < N_MOVE_AXES; ax++)
            {
                float v;
                if (a.kv(AXIS_NAME[ax], v, bad))
                {
                    t[ax] = relative ? from[ax] + v : v;
                    any = true;
                }
            }
            float speed = cfg::get(cfg::JOG_SPEED);
            a.kv("speed", speed, bad);
            if (bad || !any || speed <= 0)
            {
                error(relative ? "usage: JOG x= y= z= e0= e1= rot= paste= speed=" : "usage: MOVE x= y= z= e0= e1= rot= paste= speed=");
                return;
            }
            if (!motion.moveTo(t, zLimitedSpeed(from, t, speed)))
                error("move rejected (alarm?)");
            else
                ok();
        }

        bool oneAxisTo(uint8_t axis, float value, float speed)
        {
            float t[N_AXIS];
            motion.queuedTargets(t);
            t[axis] = value;
            return motion.moveTo(t, speed);
        }

        bool percentArg(const char *s, float &f)
        {
            if (!Args::number(s, f) || f < 0 || f > 100)
                return false;
            f /= 100.0f;
            return true;
        }

        void applySetting(int index)
        {
            cfg::AxisKey k;
            uint8_t ax;
            if (cfg::isAxisKey((size_t)index, k, ax))
            {
                if (k >= cfg::TMC_CUR)
                {
                    if (!motion.isBusy())
                    {
                        tmcs.configure(ax);
                        if (ax == X_AXIS && k == cfg::TMC_MS)
                            tmcs.configure(X2_AXIS); // X2 uses the microsteps of X
                    }
                }
                else
                    motion.applySettings();
            }
            else
            {
                motion.applySettings();
                outputs.fansOn(); // cam_fan / box_fan / pi_fan take effect at once
                if (cfg::get(cfg::PUMP_USED) < 0.5f)
                    outputs.set(OUT_PUMP, 0); // pump_used 0: off at once
            }
        }

        // ---- commands ----------------------------------------------------------------------
        void cmdHelp(Args &)
        {
            static const char *const NAMES[] = {"HELP", "INFO", "STATUS", "REPORT", "WAIT", "PING", "HOME", "MOVE", "JOG",
                                                "E1", "E0", "ROTATE", "PASTE", "MOTOR", "SETPOS", "POS", "MOTORS", "HALT",
                                                "ESTOP", "CLEAR", "SENSORS", "SDLS", "SDREAD", "PUMP", "VALVE", "LIGHT",
                                                "OUTPUTS", "GET", "SET", "DEFAULTS", "PROBE", "LOADCELL", "TMC", "TMCREG",
                                                "DFU", "SGTEST"};
            for (const char *n : NAMES)
            {
                Line l("cmd");
                l.str("name", n);
                out->line(l.s);
            }
            ok();
        }

        void cmdInfo(Args &)
        {
            Line l("info");
            l.str("firmware", FIRMWARE_NAME).str("version", FIRMWARE_VERSION).str("board", PAP_BOARD_NAME);
            l.num("axes", N_AXIS);
            out->line(l.s);
            ok();
        }

        void cmdStatus(Args &)
        {
            Line l("status");
            addStatus(l);
            out->line(l.s);
            ok();
        }

        void cmdReport(Args &a)
        {
            float ms;
            if (a.count() < 2 || !Args::number(a[1], ms) || ms < 0)
                return error("usage: REPORT <ms>");
            reportMs = ms == 0 ? 0 : (uint32_t)fminf(fmaxf(ms, 50), 60000);
            ok();
        }

        void cmdHome(Args &a)
        {
            if (refuseAlarm())
                return;
            uint8_t what = MASK_HOME_Z | MASK_HOME_XY;
            if (cfg::get(cfg::E1_HOME) >= 1)
                what |= MASK_HOME_E1;
            if (a.count() >= 2)
            {
                what = 0;
                for (uint8_t i = 1; i < a.count(); i++)
                {
                    if (a.is(i, "E1"))
                        what |= MASK_HOME_E1;
                    else if (a.is(i, "XY"))
                        what |= MASK_HOME_XY;
                    else if (a.is(i, "X"))
                        what |= MASK_HOME_X; // X alone (both X motors): testing / calibrating
                    else if (a.is(i, "Y"))
                        what |= MASK_HOME_Y;
                    else if (a.is(i, "Z"))
                        what |= MASK_HOME_Z;
                    else if (a.is(i, "ALL"))
                        what |= MASK_HOME_XY | MASK_HOME_Z | MASK_HOME_E1;
                    else
                        return error("usage: HOME [XY] [X] [Y] [Z] [E1] [ALL]");
                }
            }
            inBlocking = true;
            abortRequested = false;
            const char *why = "";
            const bool done = motion.home(what, why);
            inBlocking = false;
            if (done)
                ok();
            else
                error(std::string("homing failed: ") + why);
        }

        void cmdE1(Args &a)
        {
            if (refuseAlarm())
                return;
            float mm, speed = cfg::get(cfg::E1_SPEED);
            bool bad = false;
            a.kv("speed", speed, bad);
            if (bad || a.count() < 2 || !Args::number(a[1], mm) || speed <= 0)
                return error("usage: E1 <mm> [speed=]");
            if (oneAxisTo(E1_AXIS, mm, speed))
                ok();
            else
                error("move rejected (alarm?)");
        }

        void cmdE0(Args &a)
        {
            if (refuseAlarm())
                return;
            float deg, speed = cfg::get(cfg::E0_SPEED);
            bool bad = false;
            a.kv("speed", speed, bad);
            if (bad || a.count() < 2 || !Args::number(a[1], deg) || speed <= 0)
                return error("usage: E0 <deg> [speed=]");
            if (motion.moveAxisBy(E0_AXIS, deg, speed))
                ok();
            else
                error("move rejected (alarm?)");
        }

        void cmdRotate(Args &a)
        {
            if (refuseAlarm())
                return;
            float deg, speed = cfg::get(cfg::ROT_SPEED);
            bool bad = false;
            a.kv("speed", speed, bad);
            if (bad || a.count() < 2 || !Args::number(a[1], deg) || speed <= 0)
                return error("usage: ROTATE <deg> [speed=]");
            if (oneAxisTo(ROT_AXIS, deg, speed))
                ok();
            else
                error("move rejected (alarm?)");
        }

        // PASTE <ul>: plunger travel = volume / syringe area (1 ul = 1 mm^3)
        void cmdPaste(Args &a)
        {
            if (refuseAlarm())
                return;
            float ul;
            if (a.count() < 2 || !Args::number(a[1], ul))
                return error("usage: PASTE <microlitres>");
            const float d = cfg::get(cfg::SYRINGE_D);
            const float area = 3.14159265f * d * d / 4.0f;
            const float mmPerUl = 1.0f / area;
            const float speed = cfg::get(cfg::PASTE_SPEED) * mmPerUl;
            if (motion.moveAxisBy(PASTE_AXIS, ul * mmPerUl, speed))
                ok();
            else
                error("move rejected (alarm?)");
        }

        // MOTOR <axis> <deg>: turn the motor shaft by <deg> (200 full steps per turn x microsteps)
        void cmdMotor(Args &a)
        {
            if (refuseAlarm())
                return;
            const int ax = axisByName(a[1]);
            float deg;
            if (ax == X2_AXIS)
                return error("x2 follows x: use MOTOR x");
            if (ax < 0 || a.count() < 3 || !Args::number(a[2], deg))
                return error("usage: MOTOR <x|y|z|rot|paste|e0|e1> <deg>");
            const float stepsPerTurn = 200.0f * cfg::axis(cfg::TMC_MS, (uint8_t)ax);
            const float unitsPerDeg = stepsPerTurn / 360.0f / cfg::axis(cfg::STEPS, (uint8_t)ax);
            const float speed = cfg::axis(cfg::TEST_SPEED, (uint8_t)ax) * unitsPerDeg;
            if (motion.moveAxisBy((uint8_t)ax, deg * unitsPerDeg, speed))
                ok();
            else
                error("move rejected (alarm?)");
        }

        void cmdSetPos(Args &a)
        {
            bool bad = false, any = false;
            float v[N_AXIS];
            bool has[N_AXIS];
            for (uint8_t ax = 0; ax < N_MOVE_AXES; ax++)
            {
                has[ax] = a.kv(AXIS_NAME[ax], v[ax], bad);
                any |= has[ax];
            }
            if (bad || !any)
                return error("usage: SETPOS x= y= z= e0= e1= rot= paste=");
            for (uint8_t ax = 0; ax < N_MOVE_AXES; ax++)
                if (has[ax])
                    motion.setPosition(ax, v[ax]);
            ok();
        }

        void cmdPos(Args &)
        {
            Line l("pos");
            for (uint8_t ax = 0; ax < N_AXIS; ax++)
                l.flt(AXIS_NAME[ax], motion.getPosition(ax == X2_AXIS ? (uint8_t)X_AXIS : ax), 3);
            out->line(l.s);
            ok();
        }

        void cmdMotors(Args &a)
        {
            const bool on = a.is(1, "ON");
            if (!on && !a.is(1, "OFF"))
                return error("usage: MOTORS ON|OFF");
            if (!on && motion.isBusy())
                motion.stop();
            motion.enableMotors(on);
            ok();
        }

        void cmdHalt(Args &)
        {
            motion.stop();
            motion.cancelProbe();
            ok();
        }

        void cmdEstop(Args &)
        {
            motion.emergencyStop();
            outputs.allOff();
            lights::allOff();
            ok();
        }

        void cmdClear(Args &)
        {
            const int n = (int)cfg::get(cfg::ESTOP_INPUT);
            if (n >= 0 && input_hit((uint8_t)n))
                return error("e-stop button still pressed");
            motion.clearAlarm();
            alarmText = "";
            lights::setTopIdle(); // the top light is always on again
            ok();
        }

        // SDLS [dir] / SDREAD <path> [offset]: the Octopus microSD card (sdcard.h), only while standing still
        void cmdSdls(Args &a)
        {
            if (motion.isBusy())
                return error("busy: wait until the motion has stopped");
            std::string err;
            const bool done = sdcard::list(a.count() > 1 ? a[1] : "/", [](const char *name, uint32_t size, bool isDir) {
                Line l("file");
                l.str("name", name).num("size", (long)size).num("dir", isDir);
                out->line(l.s);
            }, err);
            if (!done)
                return error(err);
            ok();
        }

        void cmdSdread(Args &a)
        {
            if (a.count() < 2)
                return error("usage: SDREAD <path> [offset]");
            if (motion.isBusy())
                return error("busy: wait until the motion has stopped");
            const uint32_t offset = a.count() > 2 ? (uint32_t)strtoul(a[2], nullptr, 10) : 0;
            uint32_t next = 0, size = 0;
            bool eof = false;
            std::string err;
            const bool done = sdcard::read(a[1], offset, 1024, [](const std::string &line) { out->line("|" + line); },
                                         next, size, eof, err);
            if (!done)
                return error(err);
            Line l("sdread");
            l.num("offset", (long)next).num("size", (long)size).num("eof", eof);
            out->line(l.s);
            ok();
        }

        void cmdSensors(Args &)
        {
            Line l("sensors");
            for (uint8_t i = 0; i < N_INPUTS; i++)
                l.num(INPUT_NAME[i], input_hit(i));
            const uint8_t noz = toolbus_value();
            l.num("noz_ok", toolbus_fresh());
            for (uint8_t i = 0; i < N_NOZZLE_INPUTS; i++)
            {
                char k[8];
                snprintf(k, sizeof(k), "noz%u", (unsigned)(i + 1));
                l.num(k, (noz >> i) & 1);
            }
            l.num("lc", loadcell::present());
            if (loadcell::present())
                l.flt("load", loadcell::grams(), 1);
            out->line(l.s);
            ok();
        }

        void cmdPump(Args &a)
        {
            float f;
            if (a.is(1, "ON"))
                f = cfg::get(cfg::PUMP_POWER) / 100.0f;
            else if (a.is(1, "OFF"))
                f = 0;
            else if (!percentArg(a[1], f))
                return error("usage: PUMP <0-100|ON|OFF>");
            outputs.set(OUT_PUMP, f);
            ok();
        }

        void cmdValve(Args &a)
        {
            if (a.is(1, "ON"))
                outputs.set(OUT_VALVE, 1);
            else if (a.is(1, "OFF"))
                outputs.set(OUT_VALVE, 0);
            else
                return error("usage: VALVE ON|OFF");
            ok();
        }


        void cmdLight(Args &a)
        {
            uint8_t which;
            if (a.is(1, "TOP"))
                which = OUT_LIGHT_T;
            else if (a.is(1, "BOTTOM"))
                which = OUT_LIGHT_B;
            else
                return error("usage: LIGHT <top|bottom> <0-100|ON|OFF>");
            float f;
            if (which == OUT_LIGHT_T && a.is(2, "IDLE"))
            {
                lights::setTopIdle();
                return ok();
            }
            if (a.is(2, "ON"))
                f = which == OUT_LIGHT_T ? cfg::get(cfg::LIGHT_TOP_IDLE) / 100.0f : 1;
            else if (a.is(2, "OFF"))
                f = 0;
            else if (!percentArg(a[2], f))
                return error("usage: LIGHT <top|bottom> <0-100|ON|OFF>  (top also IDLE)");
            if (which == OUT_LIGHT_T)
                lights::setTop(f);
            else
                lights::setBottom(f);
            ok();
        }

        void cmdOutputs(Args &)
        {
            Line l("outputs");
            for (uint8_t i = 0; i < N_OUTPUTS; i++)
                l.num(OUTPUT_NAME[i], lroundf(outputs.get(i) * 100));
            out->line(l.s);
            ok();
        }

        void cmdGet(Args &a)
        {
            if (a.count() >= 2)
            {
                const int i = cfg::find(a[1]);
                if (i < 0)
                    return error(std::string("unknown key ") + a[1]);
                Line l("set");
                l.str("key", cfg::name((size_t)i)).str("value", cfg::text((size_t)i));
                out->line(l.s);
                return ok();
            }
            for (size_t i = 0; i < cfg::count(); i++)
            {
                Line l("set");
                l.str("key", cfg::name(i)).str("value", cfg::text(i));
                out->line(l.s);
            }
            ok();
        }

        // SET key value | SET key=value [key=value ...]
        void cmdSet(Args &a)
        {
            if (a.count() < 2)
                return error("usage: SET <key> <value> | SET key=value ...");
            std::string err;
            int changed = -1;
            if (!strchr(a[1], '='))
            {
                if (a.count() < 3)
                    return error("usage: SET <key> <value>");
                if (!cfg::set(a[1], a[2], err, &changed))
                    return error(err);
                applySetting(changed);
                return ok();
            }
            for (uint8_t i = 1; i < a.count(); i++)
            {
                char kv[LINE_MAX + 1];
                strncpy(kv, a[i], LINE_MAX);
                kv[LINE_MAX] = '\0';
                char *eq = strchr(kv, '=');
                if (!eq)
                    return error(std::string("bad word ") + a[i]);
                *eq = '\0';
                if (!cfg::set(kv, eq + 1, err, &changed))
                    return error(err);
                applySetting(changed);
            }
            ok();
        }

        void cmdDefaults(Args &)
        {
            cfg::init();
            motion.applySettings();
            outputs.fansOn();
            if (!motion.isBusy())
                for (uint8_t ax = 0; ax < N_AXIS; ax++)
                    tmcs.configure(ax);
            ok();
        }

        // PROBE e1=<to> [speed=]: drive the gantry until the feeder switch closes (SCAN step)
        // PROBE z=<to> [speed=]:  lower the seesaw until the nozzle tip presses the load cell (touch-off)
        void cmdProbe(Args &a)
        {
            if (refuseAlarm())
                return;
            float to, speed = 5;
            bool bad = false;
            const bool z = a.kv("z", to, bad);
            const bool has = z || a.kv("e1", to, bad);
            a.kv("speed", speed, bad);
            if (bad || !has || speed <= 0)
                return error("usage: PROBE e1=<mm> | z=<z> [speed=]");
            if (z)
            {
                if (!motion.isHomed() && cfg::get(cfg::Z_HOME) >= 1)
                    return error("not homed");
                if (!loadcell::present())
                    return error("no load cell (HX711 on EXP1)");
                if (!motion.waitUntilIdle() || !loadcell::tare())
                    return error("load cell: no samples");
                loadcell::arm();
            }
            const uint8_t axis = z ? Z_AXIS : E1_AXIS;
            inBlocking = true;
            abortRequested = false;
            if (!motion.startProbe(axis, z ? IN_LOADCELL : IN_FEEDER, to, speed))
            {
                inBlocking = false;
                loadcell::disarm();
                return error("probe rejected");
            }
            int8_t r;
            while ((r = motion.pollProbe()) == 0)
                if (abortHook())
                {
                    motion.cancelProbe();
                    loadcell::disarm();
                    inBlocking = false;
                    return error("aborted");
                }
            inBlocking = false;
            Line l("probe");
            l.num("hit", r == 1);
            l.flt(z ? "z" : "e1", motion.getPosition(axis), 3);
            if (z)
                l.flt("g", loadcell::peak(), 1);
            loadcell::disarm();
            out->line(l.s);
            ok();
        }

        // SGTEST <x|y|e1> [mm] [speed=] [cur=]: StallGuard while the axis runs mm (default 50) away from its home
        // end and back, at the homing speed and current. Answers SG_RESULT min / avg / max (X: also X2) and a
        // threshold suggestion: DIAG fires at SG_RESULT <= 2 * tmc_sg, so tmc_sg = 35 % of the free-running minimum.
        void cmdSgTest(Args &a)
        {
            if (refuseAlarm())
                return;
            const int ax = a.count() >= 2 ? axisByName(a[1]) : -1;
            if (ax != X_AXIS && ax != Y_AXIS && ax != E1_AXIS)
                return error("usage: SGTEST <x|y|e1> [mm] [speed=] [cur=]");
            float mm = 50, speed = ax == E1_AXIS ? cfg::get(cfg::E1_HOME_SPEED) : cfg::get(cfg::HOME_SPEED);
            float cur = cfg::get(cfg::HOME_CUR);
            bool bad = false;
            a.kv("speed", speed, bad);
            a.kv("cur", cur, bad);
            if (a.count() >= 3 && a[2][0] != '\0' && !strchr(a[2], '=') && !Args::number(a[2], mm))
                bad = true;
            if (bad || mm <= 0 || speed <= 0 || cur < 10 || cur > 100)
                return error("usage: SGTEST <x|y|e1> [mm] [speed=] [cur=10..100]");
            if (mm <= 2 * cfg::get(cfg::HOME_BLANK) + 1)
                return error("mm too short: more than 2 x home_blank + 1 mm");
            Motion::SgStats s, s2;
            inBlocking = true;
            abortRequested = false;
            const bool done = motion.sgTest((uint8_t)ax, mm, speed, cur, s, s2);
            inBlocking = false;
            if (!done)
                return error("SGTEST stopped (aborted or move refused)");
            if (!s.n)
                return error("no StallGuard readings (driver not answering? TMC)");
            auto rec = [&](const char *name, const Motion::SgStats &st) {
                Line l("sgtest");
                l.str("axis", name).num("n", st.n).num("min", st.min).num("avg", (long)(st.sum / st.n)).num("max", st.max);
                l.num("suggest", (long)lroundf(st.min * 0.35f)).num("cur", lroundf(cur)).flt("speed", speed, 1);
                out->line(l.s);
            };
            rec(AXIS_NAME[ax], s);
            if (ax == X_AXIS && s2.n)
                rec(AXIS_NAME[X2_AXIS], s2);
            ok();
        }

        // LOADCELL | LOADCELL TARE | LOADCELL CAL <grams>
        void cmdLoadcell(Args &a)
        {
            if (a.is(1, "TARE") || a.is(1, "CAL"))
            {
                if (motion.isBusy())
                    return error("busy");
                if (!loadcell::present())
                    return error("no load cell (HX711 on EXP1)");
                if (a.is(1, "TARE"))
                {
                    if (!loadcell::tare())
                        return error("load cell: no samples");
                }
                else
                {
                    const float g = a.count() > 2 ? strtof(a[2], nullptr) : 0;
                    if (g <= 0)
                        return error("usage: LOADCELL CAL <grams> (TARE first, then put the weight on)");
                    const int32_t zero = loadcell::tareRaw();
                    uint32_t t0 = millis();
                    int64_t sum = 0;
                    int n = 0;
                    int32_t seen = loadcell::raw();
                    while (millis() - t0 < 500)
                    {
                        loadcell::poll();
                        if (loadcell::raw() != seen)
                            seen = loadcell::raw(), sum += seen, n++;
                    }
                    if (!n)
                        return error("load cell: no samples");
                    const float scale = (float)(sum / n - zero) / g;
                    if (fabsf(scale) < 0.01f)
                        return error("no change: is the weight on the cell?");
                    char v[24];
                    snprintf(v, sizeof(v), "%.3f", scale);
                    std::string err;
                    cfg::set("lc_scale", v, err);
                }
            }
            else if (a.count() > 1)
                return error("usage: LOADCELL [TARE | CAL <grams>]");
            Line l("loadcell");
            l.num("ok", loadcell::present());
            l.flt("g", loadcell::grams(), 1);
            l.num("raw", loadcell::raw());
            l.num("tare", loadcell::tareRaw());
            l.flt("scale", cfg::get(cfg::LC_SCALE), 3);
            l.flt("rate", loadcell::rate(), 0);
            out->line(l.s);
            ok();
        }

        // TMC [axis] : driver status; TMC INIT [axis] : write the configuration again
        void cmdTmc(Args &a)
        {
            if (motion.isBusy())
                return error("busy: TMC only while the machine stands still");
            uint8_t first = 0, last = N_AXIS - 1;
            uint8_t argi = 1;
            const bool init = a.is(1, "INIT");
            if (init)
                argi = 2;
            if (a.count() > argi)
            {
                const int ax = axisByName(a[argi]);
                if (ax < 0)
                    return error("usage: TMC [INIT] [axis]");
                first = last = (uint8_t)ax;
            }
            for (uint8_t ax = first; ax <= last; ax++)
            {
                if (init)
                    tmcs.configure(ax);
                TmcStatus s;
                tmcs.status(ax, s);
                Line l("tmc");
                l.str("axis", AXIS_NAME[ax]).num("ok", s.ok).num("configured", tmcs.present(ax));
                l.num("version", s.version).num("cur", lroundf(s.currentMa)).num("ms", s.microsteps);
                l.num("sg", s.sgResult).num("stealth", s.stealth).num("standstill", s.standstill);
                l.num("ot", s.overTemp).num("otpw", s.preWarn).num("s2g", s.shortA || s.shortB);
                l.num("ola", s.openA).num("olb", s.openB);
                out->line(l.s);
            }
            ok();
        }

        // TMCREG <axis> <reg> [value] : raw register read / write (hex or decimal)
        void cmdTmcReg(Args &a)
        {
            const int ax = axisByName(a[1]);
            if (ax < 0 || a.count() < 3)
                return error("usage: TMCREG <axis> <reg> [value]");
            const uint8_t reg = (uint8_t)strtoul(a[2], nullptr, 0);
            if (a.count() >= 4)
            {
                tmcs.writeReg((uint8_t)ax, reg, (uint32_t)strtoul(a[3], nullptr, 0));
                return ok();
            }
            uint32_t v;
            if (!tmcs.readReg((uint8_t)ax, reg, v))
                return error("no answer from the driver");
            char hex[16];
            snprintf(hex, sizeof(hex), "0x%08lX", (unsigned long)v);
            Line l("tmcreg");
            l.str("axis", AXIS_NAME[ax]).num("reg", reg).str("value", hex);
            out->line(l.s);
            ok();
        }

        struct Cmd
        {
            const char *name;
            void (*fn)(Args &);
        };
        void cmdMove(Args &a) { moveCmd(a, false); }
        void cmdJog(Args &a) { moveCmd(a, true); }
        void cmdPing(Args &) { ok(); }
        void cmdWait(Args &) { waitPending = true; }
        void cmdLog(Args &) { ok(); }

        // DFU: everything off, answer ok, then restart into the ROM USB bootloader (dfu.h)
        void cmdDfu(Args &)
        {
            motion.emergencyStop();
            outputs.allOff();
            lights::allOff();
            ok();
            SerialUSB.flush();
            delay(100); // let the host read the answer before USB goes away
            dfu_enter();
        }

        const Cmd CMDS[] = {
            {"HELP", cmdHelp}, {"INFO", cmdInfo}, {"STATUS", cmdStatus}, {"REPORT", cmdReport}, {"WAIT", cmdWait},
            {"PING", cmdPing}, {"LOG", cmdLog}, {"HOME", cmdHome}, {"MOVE", cmdMove}, {"JOG", cmdJog}, {"E1", cmdE1},
            {"E0", cmdE0}, {"ROTATE", cmdRotate}, {"PASTE", cmdPaste}, {"MOTOR", cmdMotor}, {"SETPOS", cmdSetPos},
            {"POS", cmdPos}, {"MOTORS", cmdMotors}, {"HALT", cmdHalt}, {"ESTOP", cmdEstop}, {"CLEAR", cmdClear},
            {"SENSORS", cmdSensors}, {"SDLS", cmdSdls}, {"SDREAD", cmdSdread}, {"PUMP", cmdPump}, {"VALVE", cmdValve},
            {"LIGHT", cmdLight}, {"OUTPUTS", cmdOutputs}, {"GET", cmdGet}, {"SET", cmdSet}, {"DEFAULTS", cmdDefaults},
            {"PROBE", cmdProbe}, {"LOADCELL", cmdLoadcell}, {"TMC", cmdTmc}, {"TMCREG", cmdTmcReg},
            {"DFU", cmdDfu}, {"SGTEST", cmdSgTest},
        };
    }

    void handleLine(char *line)
    {
        Args a(line);
        if (a.count() == 0)
            return; // empty line or comment: no answer
        for (const Cmd &c : CMDS)
            if (!strcasecmp(a[0], c.name))
            {
                c.fn(a);
                return;
            }
        error(std::string("unknown command ") + a[0]);
    }

    void begin(Sink &sink)
    {
        out = &sink;
        motion.abortHook = abortHook;
        Line l("!ready");
        l.str("firmware", FIRMWARE_NAME).str("version", FIRMWARE_VERSION).str("board", PAP_BOARD_NAME);
        event(l.s);
    }

    void loop()
    {
        // Lines that came in while a blocking command ran
        while (pendingCount)
        {
            char l[LINE_MAX + 1];
            strcpy(l, pending[pendingHead]);
            pendingHead = (uint8_t)((pendingHead + 1) % PENDING_MAX);
            pendingCount--;
            handleLine(l);
        }
        abortRequested = false;

        char *l = readLine();
        if (l)
            handleLine(l);

        if (waitPending && !motion.isBusy())
        {
            waitPending = false;
            ok();
        }

        checkEstopInput();
        lights::loop();
        loadcell::poll();

        const uint32_t now = millis();
        // Link watchdog: moving or vacuum / valve on, and the Pi went silent
        const uint32_t wd = (uint32_t)cfg::get(cfg::WATCHDOG_MS);
        if (wd && hostSeen && now - lastRxMs > wd && (motion.isBusy() || outputs.anyOn()))
        {
            motion.stop();
            outputs.allOff();
            lights::allOff();
            motion.raiseAlarm(ALARM_LINK);
            alarmEvent("link lost");
            hostSeen = false; // until the Pi talks again
        }

        if (reportMs && now - lastReport >= reportMs)
        {
            lastReport = now;
            Line r("!status");
            addStatus(r);
            event(r.s);
        }
    }
}
