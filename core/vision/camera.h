/**
 * camera.h - USB cameras over V4L2 (YUYV), a capture thread each, newest frame kept
 * -----------------------------------------------------------------------------
 *   cams.configure(BOTTOM, "/dev/video0", 1280, 720, -1);   // from the settings
 *   Frame f;  cams.frameAfter(BOTTOM, millis(), f, 1500);    // a frame taken after now
 *   std::string jpg = cams.jpeg(TOP, 70, 640, true);         // live view (with crosshair)
 *
 * YUYV gives the grayscale image directly (Y bytes); no JPEG decoder is needed.
 * Simulated cameras (pap-core --sim, or the device is missing in simulation mode)
 * draw a part on the nozzle (bottom) or a fiducial (top), see camera.cpp.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "image.h"

struct Frame
{
    Gray gray;
    std::vector<uint8_t> yuyv; // raw (for colour JPEGs), empty for simulated frames
    uint32_t ms = 0;           // millis() when captured
    uint64_t seq = 0;
};

class CameraDev
{
public:
    ~CameraDev() { close(); }
    bool open(const std::string &dev, int w, int h, float exposure, bool simulate, int simKind);
    void close();
    void setExposure(float exposure); // -1 = auto; while running
    bool isOpen() const { return running_; }
    bool frameAfter(uint32_t ms, Frame &out, int timeoutMs);
    bool latest(Frame &out);
    std::string error() const;
    int width() const { return w_; }
    int height() const { return h_; }
    bool simulated() const { return sim_; }

private:
    void run();
    bool startV4l2();
    void stopV4l2();
    void simFrame(Frame &f);

    std::string dev_;
    int w_ = 0, h_ = 0;
    float exposure_ = -1;
    bool sim_ = false;
    int simKind_ = 0; // 0 bottom, 1 top
    int fd_ = -1;
    std::vector<std::pair<void *, size_t>> bufs_;
    std::thread t_;
    std::atomic<bool> running_{false};
    mutable std::mutex m_;
    std::condition_variable cv_;
    Frame last_;
    std::string error_;
};

// What a detection found, drawn into the live picture for OVERLAY_MS (image pixels of the full frame)
struct Overlay
{
    enum Kind : uint8_t
    {
        NONE,
        CIRCLE, // fiducial, nozzle tip: x, y, radius
        RECT,   // part on the nozzle, tape pocket: rect (rotated)
        QUAD    // QR code: quad corners
    } kind = NONE;
    float x = 0, y = 0, radius = 0;
    RotRect rect;
    Pt quad[4];
    uint32_t ms = 0; // Frame::ms of the picture it was found in
};

#define OVERLAY_MS 4000

class Cameras
{
public:
    // (Re)open when the device or size changed
    void configure(int cam, const std::string &dev, int w, int h, float exposure);
    void setSimulation(bool on) { sim_ = on; }
    bool available(int cam);
    bool frameAfter(int cam, uint32_t ms, Frame &out, int timeoutMs = 2000);
    // A new frame; with automatic exposure it waits (up to STEADY_MS more) until the brightness stops
    // changing, so a light just switched on or a new scene doesn't give a dark / washed-out picture.
    bool steadyFrame(int cam, Frame &out, int timeoutMs = 2500);
    void setExposure(int cam, float exposure); // -1 = auto, without reopening (CAL EXPOSURE)
    bool latest(int cam, Frame &out);
    // JPEG of the newest frame, scaled to maxWidth, optional crosshair at the nozzle / image centre.
    // With overlay, the last detection (setOverlay) is drawn while it is younger than OVERLAY_MS.
    std::string jpeg(int cam, int quality, int maxWidth, bool crosshair, float cx = -1, float cy = -1,
                     bool overlay = true);
    void setOverlay(int cam, const Overlay &o);
    // Draw the last detection into `rgb` (frame `f` scaled to w x h), if it is still fresh
    void drawOverlay(int cam, const Frame &f, std::vector<uint8_t> &rgb, int w, int h);
    std::string error(int cam);
    void closeAll();
    // Simulation: the top camera shows this text as a QR code ("" = the fiducial again)
    static void setSimQr(const std::string &text);

private:
    CameraDev dev_[2];
    std::string cfgDev_[2];
    int cfgW_[2] = {0, 0}, cfgH_[2] = {0, 0};
    float cfgExp_[2] = {-2, -2};
    bool sim_ = false;
    std::mutex m_;
    Overlay overlay_[2];
    std::mutex om_;
};

extern Cameras cams;

// RGB from YUYV (for JPEGs); gray-only frames are expanded
std::vector<uint8_t> frameRgb(const Frame &f, int w, int h);
float meanLevel(const Gray &g); // average brightness 0..255 (every 4th pixel)
#define STEADY_MS 2000
std::string encodeJpeg(const std::vector<uint8_t> &rgb, int w, int h, int quality);
std::string encodeJpegGray(const Gray &g, int quality);
