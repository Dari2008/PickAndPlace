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
//   CAL HEADS NOZZLE [CAM] | PASTE [CAM] | CAMERA [MANUAL]   one reference (a mark, or with CAM the
//                            bottom camera) for nozzle, paste tip and top camera -> head1_offset_x / _y,
//                            cam_top_off_x / _y (+ vhome_ref_x / _y with the bottom camera); nozzle first
//   CAL TOPOFFSET MARK       remember the nozzle position above a mark (nozzle tip on the mark)
//   CAL TOPOFFSET            then: camera roughly above the same mark -> cam_top_off_x / _y
//   CAL BOTTOM [step=]       nozzle tip above the bottom camera: matrix, rotation centre, look position
//   CAL TOP LENS | BOTTOM LENS   after CAL TOP / BOTTOM: 3 x 3 grid across the picture -> lens distortion
//                            cam_*_k1 / _k2 and a refined matrix (result: rms before / after)
//   CAL TOP GRID | BOTTOM GRID [size=]   printed checkerboard (squares of size mm, default cal_grid) in the
//                            picture: lens k1 / k2, scale and skew from one picture; rotation from two small
//                            head moves (top) or the previous matrix (bottom)
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

// Where the head really is: wait until it stands (the joystick may still be braking), then POS
static bool headPosition(float &x, float &y)
{
    octo.call("WAIT", 60000);
    const OctoReply r = octo.call("POS");
    const Record *pos = r.first("pos");
    if (!pos)
        return false;
    x = pos->num("x"), y = pos->num("y");
    return true;
}

// Nozzle / paste tip seen by the bottom camera (bottom light on for it): d = tip position relative to
// the point on the optical axis (picture centre), mm
static bool tipInBottomCamera(float &dx, float &dy, std::string &err)
{
    if (!vision::available(vision::BOTTOM))
    {
        err = "bottom camera not available";
        return false;
    }
    std::string e2;
    octoOk("LIGHT bottom ON", e2);
    Feature f;
    const bool ok = jogAndMark(vision::BOTTOM, nullptr, f, err);
    octoOk("LIGHT bottom 0", e2);
    if (!ok)
        return false;
    Frame fr;
    cams.latest(vision::BOTTOM, fr);
    camPxToMm(settings.cam[vision::BOTTOM].m, settings.cam[vision::BOTTOM].k, fr.gray.w, fr.gray.h, f.x, f.y, fr.gray.w / 2.0f,
              fr.gray.h / 2.0f, dx, dy);
    return true;
}

// Mark (or the bottom camera lens) seen by the top camera: mark = camera centre + d, mm
static bool markInTopCamera(float &dx, float &dy, std::string &err)
{
    if (!vision::available(vision::TOP))
    {
        err = "top camera not available";
        return false;
    }
    std::string e2;
    octoOk("LIGHT bottom 0", e2); // it would shine into the top camera
    Feature f;
    if (!jogAndMark(vision::TOP, nullptr, f, err))
        return false;
    Frame fr;
    cams.latest(vision::TOP, fr);
    camPxToMm(settings.cam[vision::TOP].m, settings.cam[vision::TOP].k, fr.gray.w, fr.gray.h, f.x, f.y, fr.gray.w / 2.0f,
              fr.gray.h / 2.0f, dx, dy);
    return true;
}

namespace commands
{
    bool visualHome(float &dx, float &dy, std::string &err)
    {
        dx = dy = 0;
        if (!settings.vhome)
        {
            err = "visual homing is off (vhome)";
            return false;
        }
        const float rx = settings.vhomeRef[0], ry = settings.vhomeRef[1];
        if (rx < 0 || ry < 0)
        {
            err = "no reference yet: Calibration > Head offsets with the bottom camera";
            return false;
        }
        // Top camera over the reference: head = reference - camera offset
        char line[96];
        snprintf(line, sizeof(line), "MOVE x=%.3f y=%.3f speed=%.1f", rx - settings.camTopOffset[0], ry - settings.camTopOffset[1],
                 settings.travelSpeed);
        if (!octoOk(line, err))
            return false;
        float mx, my;
        if (!markInTopCamera(mx, my, err))
            return false;
        float hx, hy;
        if (!headPosition(hx, hy))
        {
            err = "no position from the motion board";
            return false;
        }
        // Seen at head + offset + m; it really is at the reference -> the position is off by that much
        dx = hx + settings.camTopOffset[0] + mx - rx;
        dy = hy + settings.camTopOffset[1] + my - ry;
        if (fabsf(dx) > settings.vhomeMax || fabsf(dy) > settings.vhomeMax)
        {
            snprintf(line, sizeof(line), "correction %.2f / %.2f mm is more than vhome_max", dx, dy);
            err = line;
            return false;
        }
        snprintf(line, sizeof(line), "SETPOS x=%.4f y=%.4f", hx - dx, hy - dy);
        if (!octoOk(line, err))
            return false;
        motion.syncTargets();
        return true;
    }
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
    // With a lens calibration: measure in undistorted pixels
    Frame fr;
    if (cams.latest(cam, fr))
        for (Feature *f : {&p0, &px, &py})
            undistortPx(settings.cam[cam].k, fr.gray.w, fr.gray.h, f->x, f->y);
    const float d[2][2] = {{px.x - p0.x, px.y - p0.y}, {py.x - p0.x, py.y - p0.y}};
    if (!matrixFromMoves(d, step, sign, m))
    {
        err = "the mark did not move: step too small or wrong mark";
        return false;
    }
    return true;
}

// Lens: the mark (top) or the nozzle tip (bottom) at a 3 x 3 grid reaching ~70 % to the picture edge,
// then matrix + k1 / k2 fitted together (fitLens). Needs a first CAL TOP / BOTTOM for the scale.
static bool lensCal(int cam, float sign, float m[4], float k[2], float &rmsLin, float &rmsLens, std::string &err)
{
    Feature f0;
    if (!jogAndMark(cam, nullptr, f0, err))
        return false;
    Frame fr;
    if (!cams.latest(cam, fr))
    {
        err = "no picture";
        return false;
    }
    const float w = fr.gray.w, h = fr.gray.h, scale = mmPerPx(settings.cam[cam].m);
    if (scale <= 0)
    {
        err = "calibrate the camera first (scale)";
        return false;
    }
    // Reach: from where the mark is now, about 70 % of the way to the nearer edge
    const float dxMm = 0.35f * w * scale, dyMm = 0.35f * h * scale;
    float start[2];
    {
        const OctoReply r = octo.call("POS");
        const Record *pos = r.first("pos");
        if (!pos)
        {
            err = "no position from the motion board";
            return false;
        }
        start[0] = pos->num("x"), start[1] = pos->num("y");
    }
    float moves[9][2], px[9][2];
    int n = 0;
    bool ok = true;
    for (int iy = -1; iy <= 1 && ok; iy++)
        for (int ix = -1; ix <= 1 && ok; ix++)
        {
            char line[80];
            snprintf(line, sizeof(line), "MOVE x=%.4f y=%.4f speed=20", start[0] + ix * dxMm, start[1] + iy * dyMm);
            Feature f;
            if (!jogAndMark(cam, line, f, err))
            {
                err = "at " + std::to_string(ix) + " / " + std::to_string(iy) + ": " + err;
                ok = false;
                break;
            }
            moves[n][0] = ix * dxMm, moves[n][1] = iy * dyMm;
            px[n][0] = f.x, px[n][1] = f.y;
            n++;
        }
    char back[80];
    snprintf(back, sizeof(back), "MOVE x=%.4f y=%.4f speed=20", start[0], start[1]);
    std::string e2;
    octoOk(back, e2);
    octoOk("WAIT", e2);
    if (!ok)
        return false;
    if (!fitLens(moves, px, n, w, h, sign, m, k, rmsLin, rmsLens))
    {
        err = "fit failed: the mark did not move as expected";
        return false;
    }
    return true;
}

// Checkerboard in a fresh picture; points undistorted with k
static bool gridPicture(int cam, const float k[2], std::vector<GridPt> &pts, float &w, float &h, std::string &err)
{
    delay(settings.camSettleMs);
    Frame fr;
    if (!cams.frameAfter(cam, millis(), fr, 3000))
    {
        err = "no picture";
        return false;
    }
    w = fr.gray.w, h = fr.gray.h;
    if (!detectGrid(fr.gray, pts, err))
    {
        // keep what the camera saw, to look at it (website: Files)
        const char *pic = cam == 0 ? "/grid_fail_bottom.jpg" : "/grid_fail_top.jpg";
        if (vision::capture((vision::Cam)cam, pic))
            err += std::string(" - picture: ") + pic;
        return false;
    }
    if (k)
        for (GridPt &g : pts)
            undistortPx(k, w, h, g.x, g.y);
    return true;
}

// Mean shift of the grid between two pictures (squares matched by nearest, shift < half a square)
static bool gridShift(const std::vector<GridPt> &a, const std::vector<GridPt> &b, float squarePx, float &dx, float &dy)
{
    double sx = 0, sy = 0;
    int n = 0;
    for (const GridPt &p : a)
    {
        const GridPt *best = nullptr;
        float bd = 0.45f * squarePx * 0.45f * squarePx;
        for (const GridPt &q : b)
        {
            const float d = (q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y);
            if (d < bd)
                bd = d, best = &q;
        }
        if (best)
            sx += best->x - p.x, sy += best->y - p.y, n++;
    }
    if (n < 6)
        return false;
    dx = (float)(sx / n), dy = (float)(sy / n);
    return true;
}

// CAL TOP / BOTTOM GRID: k from the board; A_b = undistorted px per mm along the board. The machine
// matrix is A_b turned by the board rotation Q: A_move = A_b Q (px per mm of head move), m = sign A_move^-1.
static bool gridCal(int cam, float size, float sign, float m[4], float k[2], float &rmsLin, float &rmsLens,
                    float &moveScale, int &squares, std::string &err)
{
    std::vector<GridPt> pts;
    float w, h, A[4];
    if (!gridPicture(cam, nullptr, pts, w, h, err))
        return false;
    squares = (int)pts.size();
    if (!fitGrid(pts, size, w, h, k, A, rmsLin, rmsLens))
    {
        err = "fit failed";
        return false;
    }
    const float detA = A[0] * A[3] - A[1] * A[2];
    const float Ai[4] = {A[3] / detA, -A[1] / detA, -A[2] / detA, A[0] / detA};
    float Am[4]; // px per mm of head move, measured (top) or from the old matrix (bottom)
    moveScale = 0;
    if (sign < 0)
    {
        // Top camera: two small moves (under half a square, so the squares can be matched)
        const float d = 0.3f * size, squarePx = sqrtf(fabsf(detA)) * size;
        std::vector<GridPt> p0, px, py;
        float sh[2][2];
        char jog[64];
        bool ok = gridPicture(cam, k, p0, w, h, err);
        snprintf(jog, sizeof(jog), "JOG x=%.4f speed=20", d);
        ok = ok && octoOk(jog, err) && octoOk("WAIT", err) && gridPicture(cam, k, px, w, h, err);
        snprintf(jog, sizeof(jog), "JOG x=%.4f y=%.4f speed=20", -d, d);
        ok = ok && octoOk(jog, err) && octoOk("WAIT", err) && gridPicture(cam, k, py, w, h, err);
        std::string e2;
        snprintf(jog, sizeof(jog), "JOG y=%.4f speed=20", -d);
        octoOk(jog, e2);
        octoOk("WAIT", e2);
        if (!ok)
            return false;
        if (!gridShift(p0, px, squarePx, sh[0][0], sh[0][1]) || !gridShift(p0, py, squarePx, sh[1][0], sh[1][1]))
        {
            err = "the grid did not follow the head moves";
            return false;
        }
        Am[0] = sh[0][0] / d, Am[1] = sh[1][0] / d, Am[2] = sh[0][1] / d, Am[3] = sh[1][1] / d;
        moveScale = sqrtf(fabsf(Am[0] * Am[3] - Am[1] * Am[2]) / fabsf(detA));
    }
    else
    {
        const float *o = settings.cam[cam].m, det = o[0] * o[3] - o[1] * o[2];
        if (fabsf(det) < 1e-12f)
        {
            err = "calibrate the camera first (rotation)";
            return false;
        }
        // A = sign m^-1
        Am[0] = sign * o[3] / det, Am[1] = sign * -o[1] / det, Am[2] = sign * -o[2] / det, Am[3] = sign * o[0] / det;
    }
    // Q = A_b^-1 A_move, the nearest pure rotation / mirror
    const float M[4] = {Ai[0] * Am[0] + Ai[1] * Am[2], Ai[0] * Am[1] + Ai[1] * Am[3], Ai[2] * Am[0] + Ai[3] * Am[2],
                        Ai[2] * Am[1] + Ai[3] * Am[3]};
    float Q[4];
    nearestOrthogonal(M, Q);
    const float B[4] = {A[0] * Q[0] + A[1] * Q[2], A[0] * Q[1] + A[1] * Q[3], A[2] * Q[0] + A[3] * Q[2],
                        A[2] * Q[1] + A[3] * Q[3]};
    const float detB = B[0] * B[3] - B[1] * B[2];
    m[0] = sign * B[3] / detB, m[1] = sign * -B[1] / detB, m[2] = sign * -B[2] / detB, m[3] = sign * B[0] / detB;
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
        if (args.is(2, PSTR("GRID")))
        {
            float size = settings.calGrid;
            args.kv(PSTR("size"), size, bad);
            const char *pre = top ? "cam_top_" : "cam_bottom_";
            float m[4], k[2], rmsLin, rmsLens, moveScale;
            int squares = 0;
            if (!top)
                octoOk("LIGHT bottom ON", err);
            const bool ok = gridCal(cam, size, top ? -1.0f : 1.0f, m, k, rmsLin, rmsLens, moveScale, squares, err);
            if (!top)
                octoOk("LIGHT bottom 0", err);
            if (!ok)
            {
                out::error2(p, PSTR("grid calibration failed:"), err.c_str());
                return;
            }
            const char *suf[4] = {"m00", "m01", "m10", "m11"};
            for (int i = 0; i < 4; i++)
                setNum((std::string(pre) + suf[i]).c_str(), m[i]);
            setNum((std::string(pre) + "k1").c_str(), k[0]);
            setNum((std::string(pre) + "k2").c_str(), k[1]);
            settings.save();
            calResult(p, top ? "top_grid" : "bottom_grid", m);
            out::num(p, PSTR("squares"), squares);
            out::flt(p, PSTR("size"), size, 3);
            out::flt(p, PSTR("k1"), k[0], 5);
            out::flt(p, PSTR("k2"), k[1], 5);
            out::flt(p, PSTR("rms_px_before"), rmsLin, 3);
            out::flt(p, PSTR("rms_px_after"), rmsLens, 3);
            out::flt(p, PSTR("rms_mm_after"), rmsLens * mmPerPx(m), 4);
            if (top)
                out::flt(p, PSTR("move_scale"), moveScale, 4); // head moves / printed squares, 1 = steps/mm right
            p.println();
            out::ok(p);
            return;
        }
        if (args.is(2, PSTR("LENS")))
        {
            const char *pre = top ? "cam_top_" : "cam_bottom_";
            float m[4], k[2], rmsLin, rmsLens;
            if (!top)
                octoOk("LIGHT bottom ON", err); // the tip has to stand out
            const bool ok = lensCal(cam, top ? -1.0f : 1.0f, m, k, rmsLin, rmsLens, err);
            if (!top)
                octoOk("LIGHT bottom 0", err);
            if (!ok)
            {
                out::error2(p, PSTR("lens calibration failed:"), err.c_str());
                return;
            }
            const char *suf[4] = {"m00", "m01", "m10", "m11"};
            for (int i = 0; i < 4; i++)
                setNum((std::string(pre) + suf[i]).c_str(), m[i]);
            setNum((std::string(pre) + "k1").c_str(), k[0]);
            setNum((std::string(pre) + "k2").c_str(), k[1]);
            settings.save();
            calResult(p, top ? "top_lens" : "bottom_lens", m);
            out::flt(p, PSTR("k1"), k[0], 5);
            out::flt(p, PSTR("k2"), k[1], 5);
            out::flt(p, PSTR("rms_px_before"), rmsLin, 3);
            out::flt(p, PSTR("rms_px_after"), rmsLens, 3);
            out::flt(p, PSTR("rms_mm_after"), rmsLens * mmPerPx(m), 4);
            p.println();
            out::ok(p);
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

    // CAL HEADS NOZZLE | PASTE [CAM] | CAMERA [MANUAL]: one reference point for nozzle, paste tip and top
    // camera. Reference: a mark on the bed (tips put on it by eye), or with CAM the bottom camera (it finds
    // the tip; the reference is then the point on its optical axis).
    //   NOZZLE [CAM]   nozzle tip on the mark / over the bottom camera -> reference N
    //   PASTE [CAM]    paste tip on the mark / over the bottom camera  -> head1_offset = N (+ tip in picture) - head
    //   CAMERA         top camera above the mark / bottom camera lens, finds it -> cam_top_off = N - mark in picture - head
    //   CAMERA MANUAL  mark centred on the cross by eye
    // A reference from the bottom camera also becomes the visual homing reference (vhome_ref_x / _y).
    if (args.is(1, PSTR("HEADS")))
    {
        static float nX = NAN, nY = NAN;
        static bool nFromCam = false;
        float hx = 0, hy = 0;
        if (!headPosition(hx, hy))
        {
            out::error(p, PSTR("no position from the motion board"));
            return;
        }
        auto result = [&](const char *what, float x, float y) {
            out::rec(p, PSTR("cal"));
            out::str(p, PSTR("what"), what);
            out::flt(p, PSTR("x"), x, 3);
            out::flt(p, PSTR("y"), y, 3);
        };
        const bool cam = args.is(3, PSTR("CAM"));
        if (args.is(2, PSTR("NOZZLE")))
        {
            float dx = 0, dy = 0;
            if (cam && !tipInBottomCamera(dx, dy, err))
            {
                out::error2(p, PSTR("nozzle tip not found:"), err.c_str());
                return;
            }
            // tip = reference + d (bottom camera sees the object move) -> reference = head - d
            nX = hx - dx, nY = hy - dy;
            nFromCam = cam;
            result("reference", nX, nY);
            p.println();
            out::ok(p);
            return;
        }
        if (std::isnan(nX))
        {
            out::error(p, PSTR("first the nozzle: CAL HEADS NOZZLE [CAM]"));
            return;
        }
        if (args.is(2, PSTR("PASTE")))
        {
            float dx = 0, dy = 0;
            if (cam && !tipInBottomCamera(dx, dy, err))
            {
                out::error2(p, PSTR("paste tip not found:"), err.c_str());
                return;
            }
            // paste tip = head + offset = reference + d -> offset = reference + d - head
            setNum("head1_offset_x", nX + dx - hx);
            setNum("head1_offset_y", nY + dy - hy);
            settings.save();
            result("paste_offset", settings.head1Offset[0], settings.head1Offset[1]);
            p.println();
            out::ok(p);
            return;
        }
        if (args.is(2, PSTR("CAMERA")))
        {
            float dx = 0, dy = 0; // mark position in the top picture, mm from the centre
            if (!args.is(3, PSTR("MANUAL")))
            {
                if (!markInTopCamera(dx, dy, err))
                {
                    out::error2(p, PSTR("mark not found (centre it on the cross and use MANUAL):"), err.c_str());
                    return;
                }
                headPosition(hx, hy);
            }
            // mark = camera centre + d, camera centre = head + offset, mark = N -> offset = N - d - head
            setNum("cam_top_off_x", nX - dx - hx);
            setNum("cam_top_off_y", nY - dy - hy);
            const bool ref = nFromCam && !args.is(3, PSTR("MANUAL"));
            if (ref)
            {
                setNum("vhome_ref_x", nX);
                setNum("vhome_ref_y", nY);
            }
            settings.save();
            result("top_offset", settings.camTopOffset[0], settings.camTopOffset[1]);
            out::num(p, PSTR("vhome_ref"), ref ? 1 : 0);
            p.println();
            out::ok(p);
            return;
        }
        out::error(p, PSTR("usage: CAL HEADS NOZZLE [CAM] | PASTE [CAM] | CAMERA [MANUAL]"));
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
        camPxToMm(settings.cam[vision::TOP].m, settings.cam[vision::TOP].k, fr.gray.w, fr.gray.h, f.x, f.y, fr.gray.w / 2.0f,
              fr.gray.h / 2.0f, dx, dy);
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
