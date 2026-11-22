// pap-core tests, no hardware: make test
//   unit: settings, .pap checking, image detection, calibration maths
//   end to end: a whole job against the simulated Octopus, cameras and vacuum sensor
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../ctrl/feeder_scan.h"
#include "../ctrl/mosfets.h"
#include "../ctrl/nozzles.h"
#include "../ctrl/job/runner.h"
#include "../ctrl/protocol/commands.h"
#include "../ctrl/protocol/ports.h"
#include "../ctrl/settings/feeder_store.h"
#include "../ctrl/settings/general_settings.h"
#include "../ctrl/settings/catalog.h"
#include <map>
#include <algorithm>
#include "../ctrl/settings/machine_store.h"
#include "../ctrl/settings/storage.h"
#include "../dev/joystick.h"
#include "../dev/sensors.h"
#include "../octo/octopus.h"
#include "../vision/camera.h"
#include "../vision/detect.h"
#include "../vision/vision.h"

// ---- tiny test framework --------------------------------------------------------------------
static int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                                                  \
    do                                                                                               \
    {                                                                                                \
        g_checks++;                                                                                  \
        if (!(cond))                                                                                 \
        {                                                                                            \
            g_failed++;                                                                              \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond << std::endl;        \
        }                                                                                            \
    } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((double)(a) - (double)(b)) <= (tol))

// The runner calls service() while it waits (main.cpp has the real one)
void service()
{
    if (octo.initPending() && octo.connected())
    {
        octo.initDone();
        octo.call("REPORT 50");
        motion.syncTargets();
    }
    ports.poll();
    if (runner.state() == RUN_MESSAGE) // a job's PAUSE "...": the operator presses OK
        runner.cont();
    std::this_thread::sleep_for(std::chrono::microseconds(300));
}

static std::string g_data;

// Run one command through a port and collect the answer lines (until ok / error)
static std::vector<std::string> cmd(const std::string &line, int timeoutMs = 20000)
{
    auto ch = std::make_shared<Channel>("test");
    ports.add(ch);
    ch->pushLine(line);
    std::vector<std::string> out;
    const uint32_t t0 = millis();
    while (millis() - t0 < (uint32_t)timeoutMs)
    {
        service();
        std::string l;
        while (ch->popLine(l, 0))
        {
            if (l[0] == '!')
                continue;
            out.push_back(l);
            if (l == "ok" || l.compare(0, 5, "error") == 0)
            {
                ch->close();
                service();
                return out;
            }
        }
    }
    ch->close();
    out.push_back("error test timeout");
    return out;
}

static bool lastOk(const std::vector<std::string> &v) { return !v.empty() && v.back() == "ok"; }

static void writeFile(const std::string &path, const std::string &text)
{
    FILE *f = fopen(sdfs_path(path.c_str()).c_str(), "wb");
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

// ---- unit tests -------------------------------------------------------------------------------
static void testSettings()
{
    settings.setDefaults();
    CHECK(settings.find("cam_bottom_dev") >= 0);
    char v[80];
    settings.valueAt((uint8_t)settings.find("cam_bottom_dev"), v, sizeof(v));
    CHECK(std::string(v) == "/dev/video0");
    CHECK(settings.setValue((uint8_t)settings.find("cam_top_dev"), "/dev/video4") == nullptr);
    CHECK(std::string(settings.cam[1].dev) == "/dev/video4");
    CHECK(settings.setValue((uint8_t)settings.find("joy_deadzone"), "99") != nullptr); // out of range
    CHECK(settings.setValue((uint8_t)settings.find("paste_output"), "aux") != nullptr); // removed 10 Nov 2026
    CHECK(settings.setValue((uint8_t)settings.find("paste_output"), "none") == nullptr);
    CHECK(settings.pasteOutput == PASTE_NONE);
    CHECK(settings.save());
    settings.setDefaults();
    CHECK(settings.load());
    CHECK(std::string(settings.cam[1].dev) == "/dev/video4");
    settings.setDefaults();
    settings.save();
}

static void testJoystickAxis()
{
    CHECK_NEAR(Joystick::axis(13200, 0, 13200, 26400, 8, false), 0, 1e-6);
    CHECK_NEAR(Joystick::axis(26400, 0, 13200, 26400, 8, false), 1, 1e-6);
    CHECK_NEAR(Joystick::axis(0, 0, 13200, 26400, 8, true), 1, 1e-6);
    CHECK_NEAR(Joystick::axis(13200 + 500, 0, 13200, 26400, 8, false), 0, 1e-6); // inside the dead zone
    CHECK(Joystick::axis(20000, 0, 13200, 26400, 8, false) > 0.4f);
}

static void testDetectPart()
{
    std::mt19937 rng(5);
    for (float ang : {0.0f, 7.5f, -12.0f, 33.0f})
    {
        Gray g(640, 480, 20);
        fillRotRect(g, 330, 230, 150, 80, ang, 220);
        for (auto &p : g.px)
            p = (uint8_t)std::max(0, std::min(255, p + (int)(rng() % 9) - 4));
        const PartPx p = detectPart(g, 320, 240, -1, false, 200);
        CHECK(p.ok);
        CHECK_NEAR(p.cx, 330, 1.0);
        CHECK_NEAR(p.cy, 230, 1.0);
        CHECK_NEAR(p.angle, ang, 1.0);
        CHECK_NEAR(p.w, 150, 4.0);
        CHECK_NEAR(p.h, 80, 4.0);
    }
    // A square part turned by 50 deg reads as -40 (90 deg symmetric)
    Gray g(400, 400, 10);
    fillRotRect(g, 200, 200, 100, 100, 50, 230);
    const PartPx p = detectPart(g, 200, 200, -1, false, 100);
    CHECK(p.ok);
    CHECK_NEAR(p.angle, -40, 1.0);
    // Dark part on a light background
    Gray d(320, 240, 220);
    fillRotRect(d, 150, 130, 60, 30, 10, 30);
    const PartPx q = detectPart(d, 160, 120, -1, true, 80);
    CHECK(q.ok);
    CHECK_NEAR(q.angle, 10, 1.5);
    // Nothing there
    Gray e(320, 240, 30);
    CHECK(!detectPart(e, 160, 120, -1, false, 80).ok);
    // The lit head behind the part: a bright disc (falling off outwards) around a brighter part on the nozzle.
    // One threshold for the picture would take head + part; the part alone is the rectangle.
    for (float ang : {12.0f, -25.0f})
    {
        Gray h(640, 480, 15);
        for (int y = 0; y < 480; y++)
            for (int x = 0; x < 640; x++)
            {
                const float d = hypotf(x - 315.0f, y - 245.0f);
                if (d < 170)
                    h.at(x, y) = (uint8_t)(165 - d * 0.25f);
            }
        fillRotRect(h, 322, 236, 130, 64, ang, 235);
        for (auto &px : h.px)
            px = (uint8_t)std::max(0, std::min(255, px + (int)(rng() % 9) - 4));
        const PartPx hp = detectPart(h, 320, 240, -1, false, 190);
        CHECK(hp.ok);
        CHECK_NEAR(hp.cx, 322, 1.5);
        CHECK_NEAR(hp.cy, 236, 1.5);
        CHECK_NEAR(hp.angle, ang, 1.0);
        CHECK_NEAR(hp.w, 130, 5.0);
        CHECK_NEAR(hp.h, 64, 5.0);
    }
}

static void testDetectFeatures()
{
    Gray g(640, 480, 40);
    fillCircle(g, 335, 228, 25, 230); // fiducial
    fillCircle(g, 100, 100, 8, 230);  // a via (too small)
    const Feature f = detectCircle(g, 320, 240, 100, 25, -1);
    CHECK(f.ok);
    CHECK_NEAR(f.x, 335, 0.5);
    CHECK_NEAR(f.y, 228, 0.5);
    CHECK(!detectCircle(g, 320, 240, 100, 60, -1).ok); // wrong size
    Gray tape(640, 480, 200);
    fillRotRect(tape, 300, 250, 70, 90, 0, 30); // dark pocket
    const Feature pk = detectPocket(tape, 320, 240, 80, -1);
    CHECK(pk.ok);
    CHECK_NEAR(pk.x, 300, 1.0);
    CHECK_NEAR(pk.y, 250, 1.0);
}

static void testCalibrationMaths()
{
    // A camera turned by 30 deg, 0.05 mm per pixel, mirrored in Y (bottom camera)
    const float a = 30.0f * 3.14159265f / 180.0f, k = 0.05f;
    const float M[4] = {k * cosf(a), k * sinf(a), k * sinf(a), -k * cosf(a)};
    // Where the nozzle would appear after moving (+3, 0) and (0, +3): solve M * d = move
    const float det = M[0] * M[3] - M[1] * M[2];
    auto inv = [&](float mx, float my, float &px, float &py) {
        px = (M[3] * mx - M[1] * my) / det;
        py = (-M[2] * mx + M[0] * my) / det;
    };
    float d[2][2];
    inv(3, 0, d[0][0], d[0][1]);
    inv(0, 3, d[1][0], d[1][1]);
    float m[4];
    CHECK(matrixFromMoves(d, 3, 1, m));
    for (int i = 0; i < 4; i++)
        CHECK_NEAR(m[i], M[i], 1e-5);
    CHECK_NEAR(mmPerPx(m), k, 1e-5);
    float x, y;
    pxToMm(m, 10, 0, x, y);
    CHECK_NEAR(hypotf(x, y), 0.5, 1e-4);
    // Top camera (it moves): the matrix has the opposite sign
    float mt[4];
    CHECK(matrixFromMoves(d, 3, -1, mt));
    CHECK_NEAR(mt[0], -M[0], 1e-5);
    CHECK_NEAR(normalize45(91), 1, 1e-4);
    CHECK_NEAR(normalize45(-46), 44, 1e-4);
}

static void testLensFit()
{
    // Synthetic camera 640 x 480: 0.04 mm/px turned 10 deg, barrel distortion k1 = -0.08, k2 = 0.01.
    // The mark is seen where the distorted lens puts it for a 3 x 3 grid of head moves (top camera).
    const float w = 640, h = 480, a = 10.0f * 3.14159265f / 180.0f, s = 0.04f;
    const float M[4] = {-s * cosf(a), s * sinf(a), -s * sinf(a), -s * cosf(a)}; // top camera: sign -1
    const float kTrue[2] = {-0.08f, 0.01f};
    const float det = M[0] * M[3] - M[1] * M[2];
    const float R2 = 320.0f * 320.0f + 240.0f * 240.0f;
    float moves[9][2], px[9][2];
    int n = 0;
    for (int iy = -1; iy <= 1; iy++)
        for (int ix = -1; ix <= 1; ix++)
        {
            const float mx = ix * 6.0f, my = iy * 4.5f;
            // undistorted pixel offset from the centre for a mark offset of -move (the camera moved)
            const float ux = (M[3] * -mx - M[1] * -my) / det, uy = (-M[2] * -mx + M[0] * -my) / det;
            // distort: q with q (1 + k1 r^2 + k2 r^4) = u (fixed-point steps)
            float qx = ux, qy = uy;
            for (int it = 0; it < 50; it++)
            {
                const float r2 = (qx * qx + qy * qy) / R2, f = 1 + kTrue[0] * r2 + kTrue[1] * r2 * r2;
                qx = ux / f, qy = uy / f;
            }
            moves[n][0] = mx, moves[n][1] = my;
            px[n][0] = 320 + qx, px[n][1] = 240 + qy;
            n++;
        }
    float m[4], k[2], rmsLin, rmsLens;
    CHECK(fitLens(moves, px, n, w, h, -1, m, k, rmsLin, rmsLens));
    CHECK_NEAR(k[0], kTrue[0], 0.01);
    CHECK_NEAR(k[1], kTrue[1], 0.02);
    for (int i = 0; i < 4; i++)
        CHECK_NEAR(m[i], M[i], 0.001);
    CHECK(rmsLens < 0.05f);
    CHECK(rmsLin > rmsLens * 10);
    // A corner measurement converts back to the right mm with the correction
    float dx, dy;
    camPxToMm(m, k, w, h, px[0][0], px[0][1], 320, 240, dx, dy);
    CHECK_NEAR(dx, -moves[0][0], 0.02);
    CHECK_NEAR(dy, -moves[0][1], 0.02);
}


// Synthetic camera picture of a checkerboard: scale s mm/px, turned a deg, barrel k1, squares sq mm.
// bleed: ink spreads into the white squares by this share of a square (dark squares touch at the corners);
// vignette: brightness at the picture corners (1 = even light); noise: +- grey levels; blur: box radius px.
struct BoardPic
{
    float s = 0.03f, a = 7.0f, sq = 1.5f, k1 = -0.1f, bleed = 0, vignette = 1, ox = 0.3f, oy = 0.2f;
    int noise = 0, blur = 0;
};

static Gray makeBoard(const BoardPic &b)
{
    const int W = 640, H = 480, SS = 3;
    const float a = b.a * 3.14159265f / 180.0f, R2 = 320.0f * 320.0f + 240.0f * 240.0f;
    Gray g(W, H, 0);
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> nd(-b.noise, b.noise);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            float sum = 0;
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++)
                {
                    // seen pixel -> undistorted -> board mm
                    const float qx = x + (sx + 0.5f) / SS - 320, qy = y + (sy + 0.5f) / SS - 240;
                    const float r2 = (qx * qx + qy * qy) / R2, f = 1 + b.k1 * r2;
                    const float ux = qx * f * b.s, uy = qy * f * b.s;
                    const float bx = (cosf(a) * ux + sinf(a) * uy + b.ox) / b.sq, by = (-sinf(a) * ux + cosf(a) * uy + b.oy) / b.sq;
                    const int ix = (int)floorf(bx), iy = (int)floorf(by);
                    const float fx = bx - ix, fy = by - iy;
                    bool white = (ix + iy) & 1;
                    if (white && (fx < b.bleed || fx > 1 - b.bleed || fy < b.bleed || fy > 1 - b.bleed))
                        white = false;
                    sum += white ? 230 : 25;
                }
            const float r2 = ((x - 320.0f) * (x - 320.0f) + (y - 240.0f) * (y - 240.0f)) / R2;
            const float v = sum / (SS * SS) * (1 - (1 - b.vignette) * r2) + (b.noise ? nd(rng) : 0);
            g.at(x, y) = (uint8_t)std::max(0.0f, std::min(255.0f, v));
        }
    for (int r = 0; r < b.blur; r++)
    {
        Gray o = g;
        for (int y = 1; y < H - 1; y++)
            for (int x = 1; x < W - 1; x++)
            {
                int t = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        t += g.at(x + dx, y + dy);
                o.at(x, y) = (uint8_t)(t / 9);
            }
        g = o;
    }
    return g;
}

static void checkBoard(const char *what, const BoardPic &b, size_t minPts)
{
    const Gray g = makeBoard(b);
    std::vector<GridPt> pts;
    std::string err;
    const bool ok = detectGrid(g, pts, err);
    CHECK(ok);
    if (!ok)
    {
        std::cout << "  grid (" << what << "): " << err << std::endl;
        return;
    }
    CHECK(pts.size() >= minPts);
    float k[2], A[4], rl, rk;
    CHECK(fitGrid(pts, b.sq, 640, 480, k, A, rl, rk));
    CHECK_NEAR(k[0], b.k1, 0.03);
    CHECK_NEAR(sqrtf(fabsf(A[0] * A[3] - A[1] * A[2])), 1.0f / b.s, 0.4);
    CHECK(rk < 0.6f);
    if (rk >= 0.6f || fabsf(k[0] - b.k1) >= 0.03f)
        std::cout << "  grid (" << what << "): " << pts.size() << " points, k1 " << k[0] << ", rms " << rk << std::endl;
}

static void testGridCal()
{
    BoardPic b;
    checkBoard("clean", b, 60);
    // printed: ink bleed, light falls off towards the corners, a bit soft, noisy
    BoardPic p;
    p.bleed = 0.06f, p.vignette = 0.45f, p.blur = 2, p.noise = 10;
    checkBoard("printed", p, 60);
    // strong bleed: the dark squares grow into one piece
    BoardPic q = p;
    q.bleed = 0.12f, q.a = 31;
    checkBoard("ink bleed", q, 60);
    // small squares (~20 px) and big ones (~100 px, few on the picture)
    BoardPic sm = p;
    sm.sq = 0.6f, sm.blur = 1;
    checkBoard("small squares", sm, 200);
    BoardPic bg = p;
    bg.sq = 3.0f, bg.k1 = -0.05f;
    checkBoard("big squares", bg, 15);
    float Q[4];
    const float M[4] = {0.9f, -0.5f, 0.45f, 0.95f};
    nearestOrthogonal(M, Q);
    CHECK_NEAR(Q[0] * Q[3] - Q[1] * Q[2], 1.0f, 0.001);
}


static void testCrashGuard()
{
    CHECK(lastOk(cmd("HOME", 60000)));
    cmd("WAIT", 60000);
    CHECK(lastOk(cmd("SET travel_y 250")));
    const float ty = octo.valueNum("travel_y", 0);
    CHECK(ty > 0);
    // absolute beyond travel: refused; relative: stops at the edge; at the edge: refused
    CHECK(!lastOk(cmd("MOVE y=" + std::to_string(ty + 50))));
    CHECK(!lastOk(cmd("MOVE y=-5")));
    CHECK(lastOk(cmd("JOG y=" + std::to_string(ty + 500) + " speed=200", 60000)));
    cmd("WAIT", 60000);
    delay(300);
    CHECK_NEAR(octo.status().pos[OY], ty, 0.05);
    CHECK(!lastOk(cmd("JOG y=10")));
    CHECK(lastOk(cmd("JOG y=-10 speed=200", 60000)));
    cmd("WAIT", 60000);
}

static void testPapCheck()
{
    feeders.scan();
    nozzles.load();
    // The feeders the example job names (by part ID or name)
    for (const char *what : {"part=C25804", "part=C1525", "part=C45783", "name=\"LED red 0805\""})
    {
        const auto r = cmd("FEEDER ADD");
        CHECK(lastOk(r));
        const Record rec = Record::parse(r[0]);
        const std::string n = rec.str("num");
        CHECK(lastOk(cmd("FEEDER SET " + n + " " + what)));
        CHECK(lastOk(cmd("FEEDER SET " + n + " pos=" + std::to_string(40 + 70 * (atoi(n.c_str()) - 1)))));
        CHECK(lastOk(cmd("POCKET " + n + " ADD x=" + std::to_string(150 + 10 * atoi(n.c_str())) + " y=40")));
        CHECK(lastOk(cmd("POCKET " + n + " ADD x=" + std::to_string(150 + 10 * atoi(n.c_str())) + " y=44")));
    }
    FILE *src = nullptr;
    for (const char *p : {"../../../pap-syntax/test-board_v2.pap", "../../pap-syntax/test-board_v2.pap",
                          "../pap-syntax/test-board_v2.pap", "pap-syntax/test-board_v2.pap"})
        if (!src)
            src = fopen(p, "rb");
    CHECK(src != nullptr);
    if (!src)
        return;
    std::string job;
    if (src)
    {
        char b[4096];
        size_t n;
        while ((n = fread(b, 1, sizeof(b), src)) > 0)
            job.append(b, n);
        fclose(src);
    }
    writeFile("/jobs/board.pap", job);
    auto r = cmd("CHECK /jobs/board.pap");
    CHECK(lastOk(r));
    bool summary = false;
    for (auto &l : r)
        if (l.compare(0, 7, "summary") == 0)
        {
            summary = true;
            const Record rec = Record::parse(l);
            CHECK(rec.integer("parts") == 6);
            CHECK(rec.integer("funcs") == 6);
        }
    CHECK(summary);
    // A broken file reports its problems with line numbers
    writeFile("/jobs/bad.pap", "[job]\nname = bad\n[board]\n[part R9]\nat = 1\nfeeder = nope\n[func f]\nMOVE Q3\n");
    r = cmd("CHECK /jobs/bad.pap");
    int problems = 0;
    for (auto &l : r)
        if (l.compare(0, 7, "problem") == 0)
            problems++;
    CHECK(problems >= 4);
    // Fiducials in [board]
    const size_t at = job.find("\n[feeders]") + 1; // the section header, not the comment above it
    writeFile("/jobs/fid.pap", job.substr(0, at) + "fiducial = 2, 2\nfiducial = 60, 40\n" + job.substr(at));
    CHECK(lastOk(cmd("CHECK /jobs/fid.pap")));
}

// ---- end to end ---------------------------------------------------------------------------------
static void testJobRun()
{
    CHECK(lastOk(cmd("HOME", 60000)));
    // Vision: bottom camera on, fiducials on (the job gets two), vacuum check on
    CHECK(lastOk(cmd("SET vision_parts 1")));
    CHECK(lastOk(cmd("SET vision_fiducials 1")));
    CHECK(lastOk(cmd("SET vac_check 1")));
    CHECK(lastOk(cmd("SET cam_settle_ms 50")));
    CHECK(lastOk(cmd("SET pick_ms 20")));
    CHECK(lastOk(cmd("SET place_ms 20")));

    auto ch = std::make_shared<Channel>("job");
    ports.add(ch);
    ch->pushLine("RUN /jobs/fid.pap");
    bool done = false, sawPart = false, sawFid = false, failed = false;
    int continues = 0;
    std::string lastJob;
    const uint32_t t0 = millis();
    // The main loop of pap-core, in short
    while (!done && millis() - t0 < 180000)
    {
        service();
        feederScan.update();
        runner.update();
        std::string l;
        while (ch->popLine(l, 0))
        {
            if (l.compare(0, 8, "!message") == 0)
            {
                ch->pushLine("CONTINUE");
                continues++;
            }
            if (l.compare(0, 7, "!vision") == 0)
            {
                const Record r = Record::parse(l);
                if (r.str("what") == "part")
                    sawPart = true;
                if (r.str("what") == "fiducials")
                    sawFid = true;
            }
            if (l.compare(0, 4, "!job") == 0)
            {
                lastJob = l;
                const Record r = Record::parse(l);
                const std::string st = r.str("state");
                if (st == "done" || st == "failed" || st == "stopped")
                {
                    done = true;
                    failed = st != "done";
                }
            }
            if (l.compare(0, 5, "error") == 0)
            {
                std::cerr << "  job: " << l << std::endl;
                failed = done = true;
            }
        }
        // A job's PAUSE waits in runner.update(); it calls service() itself, so the loop above
        // only sees the messages between parts. Answer messages there too:
        if (runner.state() == RUN_MESSAGE)
            runner.cont();
    }
    if (failed || !done)
        std::cerr << "  last job state: " << lastJob << std::endl;
    CHECK(done);
    CHECK(!failed);
    const Record r = Record::parse(lastJob);
    CHECK(r.integer("done") == 6);
    CHECK(r.integer("skipped") == 0);
    CHECK(sawPart);
    CHECK(sawFid);
    ch->close();
}


// Feeder advance in teeth of the feed wheel: teeth x 360 / feeder_wheel_teeth
static void testFeederTeeth()
{
    auto r = cmd("FEEDER SET 1 teeth=2");
    CHECK(lastOk(r));
    Record rec = Record::parse(r[0]);
    CHECK(rec.integer("teeth") == 2);
    CHECK_NEAR(rec.num("advance"), 36, 1e-3); // 20-tooth wheel
    CHECK(lastOk(cmd("SET feeder_wheel_teeth 24")));
    r = cmd("FEEDER 1");
    CHECK(lastOk(r));
    CHECK_NEAR(Record::parse(r[0]).num("advance"), 30, 1e-3);
    // Degrees again: advance= clears the teeth count
    r = cmd("FEEDER SET 1 advance=17.5");
    CHECK(lastOk(r));
    rec = Record::parse(r[0]);
    CHECK(rec.integer("teeth") == 0);
    CHECK_NEAR(rec.num("advance"), 17.5, 1e-3);
    CHECK(lastOk(cmd("SET feeder_wheel_teeth 20")));
    CHECK(lastOk(cmd("FEEDER SET 1 teeth=1")));
    // Survives a reload from the file
    Feeder f;
    CHECK(feeders.load(1, f));
    CHECK(f.teeth == 1);
    CHECK_NEAR(FeederStore::advanceDeg(f), 18, 1e-3);
}

// Feeders by QR code: text format, real QR picture -> decoder, and a whole auto detect (SCAN)
static void testFeederQr()
{
    char id[FEEDER_TAG_LEN + 1];
    FeederStore::qrId("F07;name=100n 0402;part=C1525;teeth=2", id);
    CHECK(std::string(id) == "F07");
    FeederStore::qrId("name=x;id=ABC", id);
    CHECK(std::string(id) == "ABC");
    Feeder f;
    memset(&f, 0, sizeof(f));
    FeederStore::applyQr(f, "F09; name = LED red ; part=C84256; teeth=3");
    CHECK(std::string(f.tag) == "F09");
    CHECK(std::string(f.name) == "LED red");
    CHECK(std::string(f.part) == "C84256");
    CHECK(f.teeth == 3);

    // The simulated top camera shows a real QR code; the decoder reads it back
    Cameras::setSimQr("F42;name=Test feeder");
    std::string text, err;
    CHECK(vision::readQr(text, err));
    CHECK(text == "F42;name=Test feeder");
    Cameras::setSimQr("");
    CHECK(!vision::readQr(text, err)); // the fiducial is no QR code

    // Auto detect: the gantry finds the 4 simulated feeders, the top camera reads each code
    auto ch = std::make_shared<Channel>("scan");
    ports.add(ch);
    ch->pushLine("SCAN");
    bool done = false;
    int found = 0, withId = 0;
    const uint32_t t0 = millis();
    while (!done && millis() - t0 < 120000)
    {
        service();
        feederScan.update();
        std::string l;
        while (ch->popLine(l, 0))
        {
            if (l.compare(0, 11, "!scan found") == 0)
            {
                found++;
                withId += !Record::parse(l).str("id").empty();
            }
            if (l.compare(0, 5, "!scan") == 0)
            {
                const std::string st = Record::parse(l).str("state");
                if (st == "done" || st == "failed")
                    done = true;
            }
        }
    }
    ch->close();
    CHECK(done);
    CHECK(found == 4);
    CHECK(withId == 4);
    // The feeders taught before (at 40 / 110 / 180 / 250) got their codes, no duplicates
    auto r = cmd("FEEDERS");
    int named = 0, total = 0;
    for (auto &l : r)
        if (l.compare(0, 7, "feeder ") == 0)
        {
            total++;
            const Record rec = Record::parse(l);
            named += rec.str("name").rfind("Sim feeder", 0) == 0 && rec.integer("teeth") == 2;
        }
    CHECK(total == 4);
    CHECK(named == 4);
    // FEEDER QR reads one feeder again
    r = cmd("FEEDER QR 2", 60000);
    CHECK(lastOk(r));
}

// Top light is always on, dark while the bottom (NeoPixel) light is on
static int lightAfter(const char *line, const char *key)
{
    cmd(line);
    const uint32_t t0 = millis();
    while (millis() - t0 < 300) // a few status reports
        service();
    auto r = cmd("STATUS");
    return r.empty() ? -1 : (int)Record::parse(r[0]).integer(key, -1);
}

static void testLights()
{
    CHECK(lightAfter("LIGHT top IDLE", "light_top") == 100);
    CHECK(lightAfter("LIGHT bottom 60", "light_top") == 0);
    CHECK(lightAfter("LIGHT bottom 60", "light_bottom") == 60);
    CHECK(lightAfter("LIGHT bottom OFF", "light_top") == 100);
}

// pump_used 0: no pump connected, PUMP keeps the output off (jobs and buttons); 1 again: it runs
static void testPumpUsed()
{
    CHECK(lightAfter("PUMP ON", "pump_used") == 1);
    CHECK(lightAfter("PUMP ON", "pump") == 100);
    CHECK(lightAfter("SET pump_used 0", "pump") == 0); // off at once
    CHECK(lightAfter("PUMP ON", "pump") == 0);
    CHECK(lightAfter("PUMP 50", "pump_used") == 0);
    mosfets.enablePump(1.0f); // the job runner's path
    CHECK(lightAfter("STATUS", "pump") == 0);
    CHECK(lightAfter("SET pump_used 1", "pump") == 0);
    CHECK(lightAfter("PUMP ON", "pump") == 100);
    CHECK(lightAfter("PUMP OFF", "pump") == 0);
}

// Settings catalog: every Pi setting once, every entry in a known group, search
static void testSettingsCatalog()
{
    const auto &cat = settingsCatalog();
    const auto &groups = settingsGroups();
    std::map<std::string, int> seen;
    for (const auto &m : cat)
    {
        seen[m.key]++;
        CHECK(std::find(groups.begin(), groups.end(), m.group) != groups.end());
        CHECK(!m.label.empty());
    }
    for (auto &kv : seen)
        if (kv.second != 1)
            std::cerr << "  duplicate in the catalog: " << kv.first << std::endl;
    for (uint8_t i = 0; i < settings.count(); i++)
    {
        char key[40];
        settings.keyAt(i, key);
        if (seen[key] != 1)
            std::cerr << "  missing in the catalog: " << key << std::endl;
        CHECK(seen[key] == 1);
    }
    CHECK(seen["steps_e1"] == 1 && seen["tmc_cur_rot"] == 1 && seen["home_xy"] == 1);
    auto r = cmd("SETTINGS GROUPS");
    CHECK(lastOk(r));
    CHECK(r.size() == groups.size() + 1);
    r = cmd("SETTINGS FIND nozzle");
    CHECK(lastOk(r));
    CHECK(r.size() > 15);
    r = cmd("SETTINGS");
    CHECK(r.size() == cat.size() + 1);
    const Record rec = Record::parse(r[0]);
    CHECK(rec.type == "setting");
    CHECK(!rec.str("group").empty());
}

// Octopus microSD card: list it, copy a job into the Pi's /jobs folder
static void testOctoSd()
{
    auto r = cmd("OCTOSD LS /");
    CHECK(lastOk(r));
    bool demo = false, jobsDir = false;
    for (auto &l : r)
    {
        const Record rec = Record::parse(l);
        demo |= rec.str("name") == "demo.pap" && !rec.flag("dir");
        jobsDir |= rec.str("name") == "jobs" && rec.flag("dir");
    }
    CHECK(demo && jobsDir);
    r = cmd("OCTOSD COPY /jobs/blinky.pap");
    CHECK(lastOk(r));
    CHECK(Record::parse(r[0]).str("path") == "/jobs/blinky.pap");
    std::string text;
    FILE *f = fopen(sdfs_path("/jobs/blinky.pap").c_str(), "rb");
    CHECK(f != nullptr);
    if (f)
    {
        char b[256];
        size_t n;
        while ((n = fread(b, 1, sizeof(b), f)) > 0)
            text.append(b, n);
        fclose(f);
    }
    CHECK(text == "[job]\nname = blinky\n");
    CHECK(!lastOk(cmd("OCTOSD COPY /nope.pap")));
    CHECK(lastOk(cmd("OCTOSD COPY /demo.pap /jobs/from-octopus.pap")));
}

// Tool changer board: which sensor bit is which slot
static void testNozzleLearn()
{
    NozzleLearn l;
    l.start(0x1F, 3);
    CHECK(l.active() && l.next() == 1);
    CHECK(l.feed(0x1F) == NozzleLearn::WAITING);
    CHECK(l.feed(0x1B) == NozzleLearn::ASSIGNED); // bit 2 dropped: slot 1
    CHECK(l.bit(1) == 2 && l.next() == 2);
    CHECK(l.feed(0x1F) == NozzleLearn::WAITING);  // the same nozzle back in: already taken, not slot 2
    CHECK(l.next() == 2);
    CHECK(l.feed(0x1C) == NozzleLearn::TWO_AT_ONCE); // bits 0 and 1 together: neither counts
    CHECK(l.next() == 2);
    CHECK(l.feed(0x1E) == NozzleLearn::ASSIGNED); // only bit 1 changes now: slot 2
    CHECK(l.bit(2) == 1);
    CHECK(l.feed(0x1F) == NozzleLearn::FINISHED); // bit 0 last
    CHECK(l.bit(3) == 0 && !l.active());
    CHECK(l.feed(0x00) == NozzleLearn::WAITING);  // finished: nothing moves any more

    NozzleLearn off;
    off.start(0, 0);
    CHECK(!off.active());

    // Through the commands, on the simulated board: a wrong order ends up in nozzle_bit_n
    CHECK(lastOk(cmd("SET nozzle_count 3")));
    CHECK(lastOk(cmd("NOZZLE LEARN START")));
    CHECK(lastOk(cmd("NOZZLE LEARN")));
    nozzles.simFlipBit(4);
    CHECK(lastOk(cmd("NOZZLE LEARN")));
    nozzles.simFlipBit(0);
    CHECK(lastOk(cmd("NOZZLE LEARN")));
    CHECK(settings.nozzleBit[0] == 0);            // default until it is finished
    nozzles.simFlipBit(3);
    CHECK(lastOk(cmd("NOZZLE LEARN")));
    CHECK(settings.nozzleBit[0] == 4 && settings.nozzleBit[1] == 0 && settings.nozzleBit[2] == 3);
    CHECK(!lastOk(cmd("NOZZLE LEARN")));          // finished: not learning any more
    // Cancel keeps what was stored
    CHECK(lastOk(cmd("NOZZLE LEARN START")));
    nozzles.simFlipBit(1);
    CHECK(lastOk(cmd("NOZZLE LEARN")));
    CHECK(lastOk(cmd("NOZZLE LEARN CANCEL")));
    CHECK(settings.nozzleBit[0] == 4);
    nozzles.simFlipBit(1);
    for (int i = 0; i < 8; i++)
        settings.nozzleBit[i] = (uint8_t)i;
    CHECK(lastOk(cmd("SET nozzle_count 0")));
    CHECK(!lastOk(cmd("NOZZLE LEARN START")));    // no rack
}

// Load cell: touch-off measures each nozzle's tip height (simulated: slot n reaches simTipDz(n) further)
static void testLoadcell()
{
    CHECK(lastOk(cmd("HOME", 60000)));
    CHECK(lastOk(cmd("SET loadcell 0")));
    CHECK(!lastOk(cmd("NOZZLE TOUCH", 60000))); // no load cell
    CHECK(lastOk(cmd("SET loadcell 1")));
    CHECK(lastOk(cmd("SET loadcell_x 250")));
    CHECK(lastOk(cmd("SET loadcell_y 40")));
    CHECK(lastOk(cmd("SET loadcell_height 5")));
    CHECK(lastOk(cmd("SET nozzle_touchoff 1")));
    CHECK(lastOk(cmd("SENSORS")));
    CHECK(lastOk(cmd("SET nozzle_count 3")));
    CHECK(lastOk(cmd("NOZZLE MOUNTED 0")));
    nozzles.load(); // simulated rack: all slots full again

    // The change plan ends with the touch-off
    auto plan = nozzles.plan(nozzles.mounted() == 1 ? 2 : 1);
    CHECK(!plan.empty() && plan.back().kind == NozzleStep::TOUCHOFF);

    // A change measures the new nozzle
    CHECK(lastOk(cmd("NOZZLE CHANGE 0", 60000)));
    CHECK(lastOk(cmd("NOZZLE 1 dz=0")));
    CHECK(lastOk(cmd("NOZZLE CHANGE 1", 60000)));
    CHECK(nozzles.mounted() == 1);
    CHECK_NEAR(nozzles.slot(1).dz, Nozzles::simTipDz(1), 0.01);
    CHECK_NEAR(nozzles.offsetZ(), Nozzles::simTipDz(1), 0.01);

    // NOZZLE TOUCH on its own, with a wrong stored height to start from
    CHECK(lastOk(cmd("NOZZLE 1 dz=-1.5")));
    auto r = cmd("NOZZLE TOUCH", 60000);
    CHECK(lastOk(r));
    CHECK_NEAR(Record::parse(r[0]).num("dz"), Nozzles::simTipDz(1), 0.01);
    CHECK_NEAR(nozzles.slot(1).dz, Nozzles::simTipDz(1), 0.01);

    // Stored height 3 mm off and a short search: no contact, an error, the head goes back up
    CHECK(lastOk(cmd("SET loadcell_depth 0.5")));
    CHECK(lastOk(cmd("NOZZLE 1 dz=-3")));
    r = cmd("NOZZLE TOUCH", 60000);
    CHECK(!lastOk(r));
    CHECK(r.back().find("no contact") != std::string::npos);
    CHECK(lastOk(cmd("LOADCELL")));
    CHECK(lastOk(cmd("SET loadcell_depth 4")));
    CHECK(lastOk(cmd("NOZZLE CHANGE 0", 60000)));
    CHECK(lastOk(cmd("SET loadcell 0")));
    CHECK(lastOk(cmd("SET nozzle_count 0")));
}

// Run a job to its end (the pap-core main loop, in short); returns the last "!job" line
static std::string runJobFile(const std::string &path, bool &failed)
{
    auto ch = std::make_shared<Channel>("job2");
    ports.add(ch);
    ch->pushLine("RUN " + path);
    bool done = false;
    failed = false;
    std::string lastJob;
    const uint32_t t0 = millis();
    while (!done && millis() - t0 < 180000)
    {
        service();
        feederScan.update();
        runner.update();
        std::string l;
        while (ch->popLine(l, 0))
        {
            if (l.compare(0, 8, "!message") == 0)
                ch->pushLine("CONTINUE");
            if (l.compare(0, 4, "!job") == 0)
            {
                lastJob = l;
                const std::string st = Record::parse(l).str("state");
                if (st == "done" || st == "failed" || st == "stopped")
                    done = true, failed = st != "done";
            }
            if (l.compare(0, 5, "error") == 0)
            {
                std::cerr << "  job: " << l << std::endl;
                failed = done = true;
            }
        }
        if (runner.state() == RUN_MESSAGE)
            runner.cont();
    }
    ch->close();
    if (!done)
        failed = true;
    return lastJob;
}

static void testNozzles()
{
    CHECK(lastOk(cmd("SET nozzle_count 3")));
    CHECK(lastOk(cmd("SET nozzle_first_x 10")));
    CHECK(lastOk(cmd("SET nozzle_first_y 200")));
    CHECK(lastOk(cmd("SET nozzle_last_x 30")));
    CHECK(lastOk(cmd("SET nozzle_last_y 210")));
    float x, y;
    CHECK(nozzles.slotXY(2, x, y));
    CHECK_NEAR(x, 20, 1e-4);
    CHECK_NEAR(y, 205, 1e-4);
    CHECK(!nozzles.slotXY(4, x, y));

    CHECK(lastOk(cmd("NOZZLE 1 name=CN040")));
    CHECK(lastOk(cmd("NOZZLE 2 name=CN100 dx=0.1")));
    CHECK(!lastOk(cmd("NOZZLE 2 name=\"two words\"")));
    CHECK(nozzles.find("cn100") == 2);
    CHECK(nozzles.find("3") == 3);
    CHECK(nozzles.find("nope") == 0);

    auto r = cmd("NOZZLES");
    CHECK(lastOk(r));
    int slots = 0;
    for (auto &l : r)
        if (l.compare(0, 7, "nozzle ") == 0)
        {
            slots++;
            const Record rec = Record::parse(l);
            CHECK(rec.integer("present") == 1); // all in the rack
            CHECK(rec.integer("mounted") == 0);
        }
    CHECK(slots == 3);

    // Plan: nothing on the head -> only the pick-up part (expect present, down, wait, up, mounted, expect empty)
    CHECK(lastOk(cmd("SET nozzle_calibrate 0")));
    auto plan = nozzles.plan(1);
    int moves = 0, expects = 0;
    for (auto &s : plan)
        moves += s.kind == NozzleStep::MOVE, expects += s.kind == NozzleStep::EXPECT;
    CHECK(moves == 3);
    CHECK(expects == 2);
    // With calibration: 3 more moves (over the bottom camera, down, up) and the measuring step
    CHECK(lastOk(cmd("SET nozzle_calibrate 1")));
    plan = nozzles.plan(1);
    moves = 0;
    int cals = 0;
    for (auto &s : plan)
        moves += s.kind == NozzleStep::MOVE, cals += s.kind == NozzleStep::CALIBRATE;
    CHECK(moves == 6);
    CHECK(cals == 1);

    CHECK(lastOk(cmd("NOZZLE CHANGE CN040", 60000)));
    CHECK(nozzles.mounted() == 1);
    // The simulated bottom camera shows its "tip" 12 / -8 px off centre: 0.24 / -0.16 mm at 0.02 mm/px
    CHECK_NEAR(nozzles.slot(1).dx, 0.24, 0.03);
    CHECK_NEAR(nozzles.slot(1).dy, -0.16, 0.03);
    CHECK(lastOk(cmd("NOZZLE CHANGE 2", 60000)));
    CHECK(nozzles.mounted() == 2);
    unsigned mask = 0;
    CHECK(nozzles.readPresent(mask));
    CHECK(mask & 1u);         // nozzle 1 is back in its slot
    CHECK(!(mask & 2u));      // nozzle 2 is on the head
    CHECK(!lastOk(cmd("NOZZLE CHANGE nope")));

    // Sensors disagree: the head "has" nozzle 3, but slot 3 still shows one -> refused before moving
    CHECK(lastOk(cmd("NOZZLE MOUNTED 3")));
    r = cmd("NOZZLE CHANGE 1", 60000);
    CHECK(!lastOk(r));
    CHECK(r.back().find("slot 3") != std::string::npos);
    CHECK(lastOk(cmd("NOZZLE MOUNTED 2"))); // back to what is really on the head

    // A job: [job] nozzle = CN040 -> changes from 2 to 1 before the first pick; a bad name is a problem
    std::string job;
    FILE *f = fopen(sdfs_path("/jobs/board.pap").c_str(), "rb");
    CHECK(f != nullptr);
    if (!f)
        return;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0)
        job.append(b, n);
    fclose(f);
    const size_t at = job.find("[job]\n");
    CHECK(at != std::string::npos);
    if (at == std::string::npos)
        return;
    writeFile("/jobs/noz.pap", job.substr(0, at + 6) + "nozzle = CN040\n" + job.substr(at + 6));
    writeFile("/jobs/noz-bad.pap", job.substr(0, at + 6) + "nozzle = CN999\n" + job.substr(at + 6));
    r = cmd("CHECK /jobs/noz-bad.pap");
    bool problem = false;
    for (auto &l : r)
        problem |= l.compare(0, 7, "problem") == 0 && l.find("nozzle") != std::string::npos;
    CHECK(problem);
    CHECK(lastOk(cmd("CHECK /jobs/noz.pap")));
    bool failed = true;
    const std::string last = runJobFile("/jobs/noz.pap", failed);
    if (failed)
        std::cerr << "  nozzle job: " << last << std::endl;
    CHECK(!failed);
    CHECK(Record::parse(last).integer("done") == 6);
    CHECK(nozzles.mounted() == 1);
    CHECK(lastOk(cmd("SET nozzle_count 0")));
}

int main()
{
    char tmpl[] = "/tmp/pap-test-XXXXXX";
    g_data = mkdtemp(tmpl);
    sdfs_set_root(g_data);
    CHECK(storage.ready());
    setenv("PAP_SIM_FEEDERS", "40,110,180,250", 1);
    setenv("PAP_SIM_SPEED", "25", 1); // simulated moves 25 x faster
    octo.start("sim");
    sensors.start("/nonexistent", true);
    cams.setSimulation(true);
    const uint32_t t0 = millis();
    while (!octo.connected() && millis() - t0 < 3000)
        service();
    CHECK(octo.connected());

    struct T
    {
        const char *name;
        void (*fn)();
    } tests[] = {
        {"settings", testSettings}, {"settings catalog", testSettingsCatalog},       {"joystick axis", testJoystickAxis},   {"detect part", testDetectPart},
        {"detect features", testDetectFeatures}, {"calibration maths", testCalibrationMaths}, {"lens distortion fit", testLensFit}, {"checkerboard calibration", testGridCal}, {"crash guard (soft limits)", testCrashGuard},
        {"nozzle sensor order", testNozzleLearn}, {"pap check", testPapCheck},      {"job run (simulated machine)", testJobRun}, {"feeder teeth", testFeederTeeth}, {"nozzle changer", testNozzles}, {"feeder QR codes", testFeederQr}, {"camera lights", testLights}, {"Octopus SD card", testOctoSd}, {"load cell touch-off", testLoadcell}, {"pump connected or not", testPumpUsed},
    };
    for (auto &t : tests)
    {
        const int before = g_failed;
        t.fn();
        std::cout << (g_failed == before ? "PASS " : "FAIL ") << t.name << std::endl;
    }
    std::cout << g_checks << " checks, " << g_failed << " failed" << std::endl;
    cams.closeAll();
    sensors.stop();
    octo.stop();
    std::string rm = "rm -rf " + g_data;
    if (system(rm.c_str()) != 0)
        std::cerr << "could not remove " << g_data << std::endl;
    return g_failed ? 1 : 0;
}
