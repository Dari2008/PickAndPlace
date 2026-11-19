// CAM VACUUM JOY CAL SCREEN - cameras, vision, vacuum sensor, joystick, calibration (Pi only)
//
//   CAM <bottom|top> FRAME [w=640] [q=70] [cross=1]   frame cam= w= h= jpeg=<base64>
//   CAM <bottom|top> INFO                             cam name= ok= dev= w= h= sim= error=
//   CAM bottom MEASURE                                part ok= dx= dy= angle= w= h= error=
//   CAM top FIDUCIAL | POCKET                         feature ok= dx= dy= error=   (mm from the image centre)
//   CAM <bottom|top> SAVE <path.jpg>                  saved path=
//   CAM top QR                                        qr text=   (QR code nearest the picture centre)
//   VACUUM                                            vacuum hpa= baseline= drop= part= sensor=
//   JOY                                               joy x= y= raw_x= raw_y= button=
//   SCREEN [q=80]                                     screen w= h= jpeg=<base64>  (display mirror)
//   SCREEN TOUCH <x> <y> | RELEASE | KEY <up|down|press|hold>   remote input (screen pixels)
//   SCREEN STICK <x> <y> [button]                    simulated joystick (-1 .. 1), when no ADS1115
//   SCREEN TURN [0|1]                                 screen turned=0|1  (picture 180 degrees, touch turns with it)
//   SCREEN CALIBRATE                                  opens the touch calibration on the display (tap 4 crosses there)
//   CAL TOP [step=]          top camera scale / rotation / skew (camera centred above a mark)
//   CAL TOPOFFSET MARK       remember the nozzle position above a mark (nozzle tip on the mark)
//   CAL TOPOFFSET            then: camera roughly above the same mark -> cam_top_off_x / _y
//   CAL BOTTOM [step=]       nozzle tip above the bottom camera: matrix, rotation centre, look position
//   CAL ROTSIGN              part on the nozzle above the bottom camera: turn direction
//   CAL VACUUM BASE | OPEN | BLOCKED   pump off / nozzle open / nozzle blocked -> vac_baseline, vac_pick_min
//   CAL JOY CENTER | RANGE [ms=5000]   joystick centre / full circles for min / max
#include "commands.h"
#include <motion.h>
#include "../../dev/joystick.h"
#include "../../dev/sensors.h"
#include "../../octo/octopus.h"
#include "../../vision/camera.h"
#include "../../vision/detect.h"
#include "../../vision/vision.h"
#include "../job/runner.h"
#include "../settings/general_settings.h"
#include "../nozzles.h"

#include <cmath>

std::string (*screenJpegHook)(int quality, int &w, int &h) = nullptr; // set by the display UI
bool (*screenInputHook)(const char *what, int x, int y) = nullptr;     // "touch" x y, "release", "up", "down", "press", "hold"
int (*screenTurnHook)(int want) = nullptr; // want -1 = ask, 0 / 1 = set; returns turned 0 / 1

static std::string base64(const std::string &in)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3)
    {
        const uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        out += T[v >> 18];
        out += T[(v >> 12) & 63];
        out += T[(v >> 6) & 63];
        out += T[v & 63];
    }
    if (i < in.size())
    {
        uint32_t v = (uint8_t)in[i] << 16;
        if (i + 1 < in.size())
            v |= (uint8_t)in[i + 1] << 8;
        out += T[v >> 18];
        out += T[(v >> 12) & 63];
        out += i + 1 < in.size() ? T[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

static int camArg(Args &args)
{
    if (args.is(1, PSTR("BOTTOM")))
        return vision::BOTTOM;
    if (args.is(1, PSTR("TOP")))
        return vision::TOP;
    return -1;
}

static bool setNum(const char *key, float v)
{
    char t[32];
    snprintf(t, sizeof(t), "%.6g", (double)v);
    const int i = settings.find(key);
    return i >= 0 && !settings.setValue((uint8_t)i, t);
}

void printExtraStatus(Print &p)
{
    if (vision::vacuumAvailable() && !std::isnan(vision::vacuumHpa()))
        out::flt(p, PSTR("vac"), vision::vacuumDropHpa(), 1);
    out::num(p, PSTR("cams"), (cams.available(0) ? 1 : 0) | (cams.available(1) ? 2 : 0));
    out::num(p, PSTR("nozzle"), nozzles.mounted()); // slot on the head, 0 = none
}

void cmdCam(Port &port, Args &args)
{
    Print &p = port.io;
    const int cam = camArg(args);
    if (cam < 0)
    {
        out::error(p, PSTR("usage: CAM <bottom|top> FRAME|INFO|MEASURE|FIDUCIAL|POCKET|SAVE"));
        return;
    }
    const char *name = cam == vision::BOTTOM ? "bottom" : "top";
    const bool avail = vision::available((vision::Cam)cam);
    if (cam == vision::TOP && args.is(2, PSTR("QR")))
    {
        std::string text, err;
        if (!vision::readQr(text, err))
        {
            out::error(p, err.c_str());
            return;
        }
        out::rec(p, PSTR("qr"));
        out::str(p, PSTR("text"), text.c_str());
        p.println();
        out::ok(p);
        return;
    }
    if (args.is(2, PSTR("INFO")))
    {
        Frame f;
        cams.latest(cam, f);
        out::rec(p, PSTR("cam"));
        out::str(p, PSTR("name"), name);
        out::num(p, PSTR("ok"), avail);
        out::str(p, PSTR("dev"), settings.cam[cam].dev);
        out::num(p, PSTR("w"), f.gray.w);
        out::num(p, PSTR("h"), f.gray.h);
        out::flt(p, PSTR("mm_per_px"), mmPerPx(settings.cam[cam].m), 5);
        out::str(p, PSTR("error"), cams.error(cam).c_str());
        p.println();
        out::ok(p);
        return;
    }
    if (!avail)
    {
        out::error2(p, PSTR("camera not available:"), cams.error(cam).c_str());
        return;
    }
    if (args.is(2, PSTR("FRAME")))
    {
        float w = 640, q = 70, cross = 1;
        bool bad = false;
        args.kv(PSTR("w"), w, bad);
        args.kv(PSTR("q"), q, bad);
        args.kv(PSTR("cross"), cross, bad);
        const float cx = cam == vision::BOTTOM ? settings.camBottomCenter[0] : -1;
        const float cy = cam == vision::BOTTOM ? settings.camBottomCenter[1] : -1;
        const std::string jpg = cams.jpeg(cam, (int)q, (int)w, cross != 0, cx, cy);
        if (jpg.empty())
        {
            out::error(p, PSTR("no picture yet"));
            return;
        }
        Frame f;
        cams.latest(cam, f);
        out::rec(p, PSTR("frame"));
        out::str(p, PSTR("cam"), name);
        out::num(p, PSTR("w"), f.gray.w);
        out::num(p, PSTR("h"), f.gray.h);
        p.print(" jpeg=");
        p.println(base64(jpg));
        out::ok(p);
        return;
    }
    if (args.is(2, PSTR("MEASURE")) && cam == vision::BOTTOM)
    {
        vision::PartResult r;
        const bool ok = vision::measurePart(r);
        out::rec(p, PSTR("part"));
        out::num(p, PSTR("ok"), ok);
        out::flt(p, PSTR("dx"), r.dx, 3);
        out::flt(p, PSTR("dy"), r.dy, 3);
        out::flt(p, PSTR("angle"), r.angle, 2);
        out::flt(p, PSTR("w"), r.widthMm, 2);
        out::flt(p, PSTR("h"), r.heightMm, 2);
        out::str(p, PSTR("error"), r.error.c_str());
        p.println();
        out::ok(p);
        return;
    }
    if ((args.is(2, PSTR("FIDUCIAL")) || args.is(2, PSTR("POCKET"))) && cam == vision::TOP)
    {
        float dx = 0, dy = 0;
        std::string err;
        const bool ok = args.is(2, PSTR("POCKET")) ? vision::findPocket(dx, dy, err) : vision::findFiducial(dx, dy, err);
        out::rec(p, PSTR("feature"));
        out::num(p, PSTR("ok"), ok);
        out::flt(p, PSTR("dx"), dx, 3);
        out::flt(p, PSTR("dy"), dy, 3);
        out::str(p, PSTR("error"), err.c_str());
        p.println();
        out::ok(p);
        return;
    }
    if (args.is(2, PSTR("SAVE")) && args.count() >= 4)
    {
        char path[PATH_MAX_LEN];
        if (!commands::resolvePath(port, args[3], path) || !vision::capture((vision::Cam)cam, path))
        {
            out::error(p, PSTR("can't save the picture"));
            return;
        }
        out::rec(p, PSTR("saved"));
        out::str(p, PSTR("path"), path);
        p.println();
        out::ok(p);
        return;
    }
    out::error(p, PSTR("usage: CAM <bottom|top> FRAME|INFO|MEASURE|FIDUCIAL|POCKET|SAVE"));
}

void cmdVacuum(Port &port, Args &)
{
    Print &p = port.io;
    out::rec(p, PSTR("vacuum"));
    out::num(p, PSTR("sensor"), vision::vacuumAvailable());
    out::num(p, PSTR("sim"), sensors.simulated());
    out::flt(p, PSTR("hpa"), vision::vacuumHpa(), 2);
    out::flt(p, PSTR("baseline"), settings.vacBaseline, 2);
    out::flt(p, PSTR("drop"), vision::vacuumDropHpa(), 2);
    out::flt(p, PSTR("pick_min"), settings.vacPickMin, 1);
    out::num(p, PSTR("part"), vision::partOnNozzle());
    p.println();
    out::ok(p);
}

void cmdJoy(Port &port, Args &)
{
    const JoyState j = joystick.read();
    Print &p = port.io;
    out::rec(p, PSTR("joy"));
    out::flt(p, PSTR("x"), j.x, 3);
    out::flt(p, PSTR("y"), j.y, 3);
    out::num(p, PSTR("raw_x"), j.rawX);
    out::num(p, PSTR("raw_y"), j.rawY);
    out::num(p, PSTR("button"), j.button);
    out::num(p, PSTR("adc"), sensors.adsPresent());
    p.println();
    out::ok(p);
}

void cmdScreen(Port &port, Args &args)
{
    // Remote input from the website's display mirror
    // Simulated joystick (preview / tests): x, y in -1 .. 1, button 0 / 1; ignored when the ADS1115 is there
    if (args.is(1, PSTR("STICK")) && args.count() >= 4)
    {
        joystick.setSimulated(std::max(-1.0f, std::min(1.0f, (float)atof(args[2]))), std::max(-1.0f, std::min(1.0f, (float)atof(args[3]))),
                              args.count() >= 5 && atoi(args[4]) != 0);
        out::ok(port.io);
        return;
    }
    if (args.is(1, PSTR("CALIBRATE")))
    {
        if (!screenInputHook || !screenInputHook("calibrate", 0, 0))
        {
            out::error(port.io, PSTR("no display"));
            return;
        }
        out::ok(port.io);
        return;
    }
    if (args.is(1, PSTR("TURN")))
    {
        if (!screenTurnHook)
        {
            out::error(port.io, PSTR("no display"));
            return;
        }
        const int turned = screenTurnHook(args.count() >= 3 ? (atoi(args[2]) != 0) : -1);
        out::rec(port.io, PSTR("screen"));
        out::num(port.io, PSTR("turned"), turned);
        port.io.println();
        out::ok(port.io);
        return;
    }
    if (args.is(1, PSTR("TOUCH")) || args.is(1, PSTR("RELEASE")) || args.is(1, PSTR("KEY")))
    {
        char what[16] = "";
        int x = 0, y = 0;
        if (args.is(1, PSTR("TOUCH")) && args.count() >= 4)
            strcpy(what, "touch"), x = atoi(args[2]), y = atoi(args[3]);
        else if (args.is(1, PSTR("RELEASE")))
            strcpy(what, "release");
        else if (args.is(1, PSTR("KEY")) && args.count() >= 3)
        {
            strncpy(what, args[2], sizeof(what) - 1);
            for (char *c = what; *c; c++)
                *c = (char)tolower(*c);
        }
        if (!*what)
        {
            out::error(port.io, PSTR("usage: SCREEN TOUCH <x> <y> | RELEASE | KEY <up|down|press|hold>"));
            return;
        }
        if (!screenInputHook || !screenInputHook(what, x, y))
        {
            out::error(port.io, PSTR("no display"));
            return;
        }
        out::ok(port.io);
        return;
    }
    float q = 80;
    bool bad = false;
    args.kv(PSTR("q"), q, bad);
    int w = 0, h = 0;
    const std::string jpg = screenJpegHook ? screenJpegHook((int)q, w, h) : std::string();
    if (jpg.empty())
    {
        out::error(port.io, PSTR("no display"));
        return;
    }
    out::rec(port.io, PSTR("screen"));
    out::num(port.io, PSTR("w"), w);
    out::num(port.io, PSTR("h"), h);
    port.io.print(" jpeg=");
    port.io.println(base64(jpg));
    out::ok(port.io);
}

// ---- calibration ------------------------------------------------------------------------------

static bool octoOk(const std::string &line, std::string &err)
{
    const OctoReply r = octo.call(line);
    if (!r.ok)
        err = r.error.empty() ? "motion board offline" : r.error;
    return r.ok;
}

// Jog, wait until still, settle, then find the mark (or the nozzle tip)
static bool jogAndMark(int cam, const char *jog, Feature &f, std::string &err)
{
    if (jog && (!octoOk(jog, err) || !octoOk("WAIT", err)))
        return false;
    delay(settings.camSettleMs);
    Frame fr;
    if (!cams.frameAfter(cam, millis(), fr, 3000))
    {
        err = "no picture";
        return false;
    }
    f = detectMark(fr.gray, fr.gray.w / 2.0f, fr.gray.h / 2.0f, std::min(fr.gray.w, fr.gray.h) / 2.5f);
    if (!f.ok)
        err = f.error;
    return f.ok;
}

static void calResult(Print &p, const char *what, const float m[4])
{
    out::rec(p, PSTR("cal"));
    out::str(p, PSTR("what"), what);
    out::flt(p, PSTR("m00"), m[0], 6);
    out::flt(p, PSTR("m01"), m[1], 6);
    out::flt(p, PSTR("m10"), m[2], 6);
    out::flt(p, PSTR("m11"), m[3], 6);
    out::flt(p, PSTR("mm_per_px"), mmPerPx(m), 5);
    // skew: angle between the two transformed pixel axes minus 90
    const float ax = atan2f(m[2], m[0]), ay = atan2f(m[3], m[1]);
    float skew = (ay - ax) * 180.0f / 3.14159265f;
    while (skew > 180)
        skew -= 360;
    while (skew < -180)
        skew += 360;
    out::flt(p, PSTR("skew_deg"), fabsf(skew) - 90.0f, 3);
    out::flt(p, PSTR("rot_deg"), ax * 180.0f / 3.14159265f, 3);
}

static bool matrixCal(int cam, float step, float sign, float m[4], std::string &err)
{
    char jog[64];
    Feature p0, px, py;
    if (!jogAndMark(cam, nullptr, p0, err))
        return false;
    snprintf(jog, sizeof(jog), "JOG x=%.4f speed=20", step);
    if (!jogAndMark(cam, jog, px, err))
        return false;
    snprintf(jog, sizeof(jog), "JOG x=%.4f y=%.4f speed=20", -step, step);
    if (!jogAndMark(cam, jog, py, err))
        return false;
    snprintf(jog, sizeof(jog), "JOG y=%.4f speed=20", -step);
    octoOk(jog, err);
    octoOk("WAIT", err);
    const float d[2][2] = {{px.x - p0.x, px.y - p0.y}, {py.x - p0.x, py.y - p0.y}};
    if (!matrixFromMoves(d, step, sign, m))
    {
        err = "the mark did not move: step too small or wrong mark";
        return false;
    }
    return true;
}

void cmdCal(Port &port, Args &args)
{
    Print &p = port.io;
    if (commands::refuseIfBusy(port))
        return;
    std::string err;
    float step = settings.calStep;
    bool bad = false;
    args.kv(PSTR("step"), step, bad);

    if (args.is(1, PSTR("TOP")) || args.is(1, PSTR("BOTTOM")))
    {
        const bool top = args.is(1, PSTR("TOP"));
        const int cam = top ? vision::TOP : vision::BOTTOM;
        if (!vision::available((vision::Cam)cam))
        {
            out::error2(p, PSTR("camera not available:"), cams.error(cam).c_str());
            return;
        }
        float m[4];
        // Top camera moves with the head (sign -1), the bottom camera sees the nozzle move (+1)
        if (!matrixCal(cam, step, top ? -1.0f : 1.0f, m, err))
        {
            out::error2(p, PSTR("calibration failed:"), err.c_str());
            return;
        }
        const char *pre = top ? "cam_top_m" : "cam_bottom_m";
        const char *suf[4] = {"00", "01", "10", "11"};
        for (int i = 0; i < 4; i++)
            setNum((std::string(pre) + suf[i]).c_str(), m[i]);
        if (!top)
        {
            // Rotation centre: the tip at 0 / 90 / 180 / 270 degrees, averaged
            float sx = 0, sy = 0;
            for (int a = 0; a < 4; a++)
            {
                char jog[48];
                snprintf(jog, sizeof(jog), "ROTATE %d", a * 90);
                Feature f;
                if (!jogAndMark(cam, jog, f, err))
                {
                    octoOk("ROTATE 0", err);
                    out::error2(p, PSTR("rotation centre not found:"), err.c_str());
                    return;
                }
                sx += f.x;
                sy += f.y;
            }
            octoOk("ROTATE 0", err);
            setNum("cam_bottom_cx", sx / 4);
            setNum("cam_bottom_cy", sy / 4);
            const OctoStatus s = octo.status();
            setNum("cam_bottom_x", s.pos[OX]);
            setNum("cam_bottom_y", s.pos[OY]);
            setNum("cam_bottom_z", s.pos[OZ]);
        }
        settings.save();
        calResult(p, top ? "top" : "bottom", m);
        if (!top)
        {
            out::flt(p, PSTR("cx"), settings.camBottomCenter[0], 2);
            out::flt(p, PSTR("cy"), settings.camBottomCenter[1], 2);
        }
        p.println();
        out::ok(p);
        return;
    }

    if (args.is(1, PSTR("TOPOFFSET")))
    {
        static float markX = NAN, markY = NAN;
        const OctoStatus s = octo.status();
        if (args.is(2, PSTR("MARK")))
        {
            markX = s.pos[OX];
            markY = s.pos[OY];
            out::rec(p, PSTR("cal"));
            out::str(p, PSTR("what"), "nozzle_mark");
            out::flt(p, PSTR("x"), markX, 3);
            out::flt(p, PSTR("y"), markY, 3);
            p.println();
            out::ok(p);
            return;
        }
        if (std::isnan(markX))
        {
            out::error(p, PSTR("first put the nozzle tip on a mark and send CAL TOPOFFSET MARK"));
            return;
        }
        if (!vision::available(vision::TOP))
        {
            out::error(p, PSTR("top camera not available"));
            return;
        }
        Feature f;
        if (!jogAndMark(vision::TOP, nullptr, f, err))
        {
            out::error2(p, PSTR("mark not found:"), err.c_str());
            return;
        }
        Frame fr;
        cams.latest(vision::TOP, fr);
        float dx, dy;
        pxToMm(settings.cam[vision::TOP].m, f.x - fr.gray.w / 2.0f, f.y - fr.gray.h / 2.0f, dx, dy);
        // mark = camera centre + d, camera centre = nozzle + offset -> offset = mark - d - nozzle
        setNum("cam_top_off_x", markX - dx - s.pos[OX]);
        setNum("cam_top_off_y", markY - dy - s.pos[OY]);
        settings.save();
        out::rec(p, PSTR("cal"));
        out::str(p, PSTR("what"), "top_offset");
        out::flt(p, PSTR("x"), settings.camTopOffset[0], 3);
        out::flt(p, PSTR("y"), settings.camTopOffset[1], 3);
        p.println();
        out::ok(p);
        return;
    }

    if (args.is(1, PSTR("ROTSIGN")))
    {
        Frame f0, f1;
        const float *m = settings.cam[vision::BOTTOM].m;
        if (!vision::available(vision::BOTTOM) || !cams.frameAfter(vision::BOTTOM, millis(), f0, 3000))
        {
            out::error(p, PSTR("bottom camera not available"));
            return;
        }
        const float nx = f0.gray.w / 2.0f, ny = f0.gray.h / 2.0f, r = std::min(f0.gray.w, f0.gray.h) / 2.5f;
        const PartPx a = detectPart(f0.gray, nx, ny, (int)settings.cam[0].thresh, settings.camBottomInvert, r);
        if (!a.ok || !octoOk("ROTATE 20", err) || !octoOk("WAIT", err))
        {
            out::error2(p, PSTR("no part seen:"), a.error.c_str());
            return;
        }
        delay(settings.camSettleMs);
        cams.frameAfter(vision::BOTTOM, millis(), f1, 3000);
        const PartPx b = detectPart(f1.gray, nx, ny, (int)settings.cam[0].thresh, settings.camBottomInvert, r);
        octoOk("ROTATE 0", err);
        if (!b.ok)
        {
            out::error2(p, PSTR("part lost after turning:"), b.error.c_str());
            return;
        }
        const float d = normalize45(angleToMachine(m, b.angle) - angleToMachine(m, a.angle));
        setNum("cam_bottom_rot_sign", d >= 0 ? 1 : -1);
        settings.save();
        out::rec(p, PSTR("cal"));
        out::str(p, PSTR("what"), "rot_sign");
        out::num(p, PSTR("sign"), d >= 0 ? 1 : -1);
        out::flt(p, PSTR("measured"), d, 2);
        p.println();
        out::ok(p);
        return;
    }

    if (args.is(1, PSTR("VACUUM")))
    {
        static float pOpen = NAN;
        if (!vision::vacuumAvailable())
        {
            out::error(p, PSTR("no vacuum sensor (BMP280)"));
            return;
        }
        if (args.is(2, PSTR("BASE")))
        {
            octoOk("PUMP OFF", err);
            octoOk("VALVE OFF", err);
            delay(800);
            setNum("vac_baseline", sensors.pressureHpa());
        }
        else if (args.is(2, PSTR("OPEN")) || args.is(2, PSTR("BLOCKED")))
        {
            octoOk("PUMP ON", err);
            octoOk("VALVE ON", err);
            delay(1000);
            const float pr = sensors.pressureHpa();
            if (args.is(2, PSTR("OPEN")))
                pOpen = pr;
            else
            {
                const float dOpen = std::isnan(pOpen) ? 0 : settings.vacBaseline - pOpen;
                setNum("vac_pick_min", std::max(1.0f, (dOpen + (settings.vacBaseline - pr)) / 2.0f));
                octoOk("VALVE OFF", err);
                octoOk("PUMP OFF", err);
            }
        }
        else
        {
            out::error(p, PSTR("usage: CAL VACUUM BASE|OPEN|BLOCKED"));
            return;
        }
        settings.save();
        out::rec(p, PSTR("cal"));
        out::str(p, PSTR("what"), "vacuum");
        out::flt(p, PSTR("hpa"), sensors.pressureHpa(), 2);
        out::flt(p, PSTR("baseline"), settings.vacBaseline, 2);
        out::flt(p, PSTR("pick_min"), settings.vacPickMin, 2);
        p.println();
        out::ok(p);
        return;
    }

    if (args.is(1, PSTR("JOY")))
    {
        if (args.is(2, PSTR("CENTER")))
        {
            setNum("joy_x_center", sensors.adc(0));
            setNum("joy_y_center", sensors.adc(1));
        }
        else if (args.is(2, PSTR("RANGE")))
        {
            float ms = 5000;
            args.kv(PSTR("ms"), ms, bad);
            int16_t mn[2] = {32767, 32767}, mx[2] = {-32768, -32768};
            const uint32_t t0 = millis();
            while (millis() - t0 < (uint32_t)ms)
            {
                for (int c = 0; c < 2; c++)
                {
                    mn[c] = std::min(mn[c], sensors.adc(c));
                    mx[c] = std::max(mx[c], sensors.adc(c));
                }
                delay(5);
            }
            setNum("joy_x_min", mn[0]);
            setNum("joy_x_max", mx[0]);
            setNum("joy_y_min", mn[1]);
            setNum("joy_y_max", mx[1]);
        }
        else
        {
            out::error(p, PSTR("usage: CAL JOY CENTER|RANGE [ms=]"));
            return;
        }
        settings.save();
        out::rec(p, PSTR("cal"));
        out::str(p, PSTR("what"), "joystick");
        for (int c = 0; c < 2; c++)
        {
            const char *ax = c ? "y" : "x";
            out::flt(p, (std::string(ax) + "_min").c_str(), settings.joyMin[c], 0);
            out::flt(p, (std::string(ax) + "_center").c_str(), settings.joyCenter[c], 0);
            out::flt(p, (std::string(ax) + "_max").c_str(), settings.joyMax[c], 0);
        }
        p.println();
        out::ok(p);
        return;
    }
    out::error(p, PSTR("usage: CAL TOP|BOTTOM|TOPOFFSET [MARK]|ROTSIGN|VACUUM ...|JOY ..."));
}
