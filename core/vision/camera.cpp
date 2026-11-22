#include "camera.h"
#include "../compat/Arduino.h"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <random>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../third_party/stb_image_write.h"

Cameras cams;

// ---- simulated QR code (top camera) ----
extern "C"
{
#include "../third_party/LovyanGFX/src/lgfx/utility/lgfx_qrcode.h"
}
static std::mutex g_simQrM;
static std::string g_simQr;
void Cameras::setSimQr(const std::string &text)
{
    std::lock_guard<std::mutex> l(g_simQrM);
    g_simQr = text;
}

// Black modules on a white square, centred, `px` pixels per module
static bool drawQr(Gray &g, const std::string &text, int px)
{
    for (uint8_t version = 1; version <= 10; version++)
    {
        std::vector<uint8_t> buf(lgfx_qrcode_getBufferSize(version));
        QRCode qr;
        if (lgfx_qrcode_initText(&qr, buf.data(), version, ECC_LOW, text.c_str()) != 0)
            continue;
        const int quiet = 4, n = qr.size, side = (n + 2 * quiet) * px;
        const int x0 = (g.w - side) / 2, y0 = (g.h - side) / 2;
        for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++)
                if (x0 + x >= 0 && y0 + y >= 0 && x0 + x < g.w && y0 + y < g.h)
                {
                    const int mx = x / px - quiet, my = y / px - quiet;
                    const bool dark = mx >= 0 && my >= 0 && mx < n && my < n && lgfx_qrcode_getModule(&qr, mx, my);
                    g.at(x0 + x, y0 + y) = dark ? 20 : 235;
                }
        return true;
    }
    return false;
}

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do
        r = ioctl(fd, req, arg);
    while (r == -1 && errno == EINTR);
    return r;
}

// ---- JPEG ------------------------------------------------------------------------------------
static void jpegSink(void *ctx, void *data, int size) { ((std::string *)ctx)->append((const char *)data, (size_t)size); }

std::string encodeJpeg(const std::vector<uint8_t> &rgb, int w, int h, int quality)
{
    std::string out;
    if (w > 0 && h > 0 && rgb.size() >= (size_t)w * h * 3)
        stbi_write_jpg_to_func(jpegSink, &out, w, h, 3, rgb.data(), quality);
    return out;
}

std::string encodeJpegGray(const Gray &g, int quality)
{
    std::string out;
    if (!g.empty())
        stbi_write_jpg_to_func(jpegSink, &out, g.w, g.h, 1, g.px.data(), quality);
    return out;
}

static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

std::vector<uint8_t> frameRgb(const Frame &f, int w, int h)
{
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    const int sw = f.gray.w, sh = f.gray.h;
    const bool colour = f.yuyv.size() >= (size_t)sw * sh * 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const int sx = x * sw / w, sy = y * sh / h;
            uint8_t *p = &rgb[((size_t)y * w + x) * 3];
            if (!colour)
            {
                p[0] = p[1] = p[2] = f.gray.at(sx, sy);
                continue;
            }
            const size_t i = ((size_t)sy * sw + (sx & ~1)) * 2;
            const int Y = f.yuyv[i + ((sx & 1) ? 2 : 0)], U = f.yuyv[i + 1] - 128, V = f.yuyv[i + 3] - 128;
            p[0] = clamp8(Y + ((359 * V) >> 8));
            p[1] = clamp8(Y - ((88 * U + 183 * V) >> 8));
            p[2] = clamp8(Y + ((454 * U) >> 8));
        }
    return rgb;
}

// ---- one camera -----------------------------------------------------------------------------
bool CameraDev::open(const std::string &dev, int w, int h, float exposure, bool simulate, int simKind)
{
    close();
    dev_ = dev;
    w_ = w;
    h_ = h;
    exposure_ = exposure;
    simKind_ = simKind;
    sim_ = simulate;
    {
        std::lock_guard<std::mutex> l(m_);
        error_.clear();
        last_ = Frame();
    }
    if (!sim_ && !startV4l2())
    {
        stopV4l2();
        return false;
    }
    running_ = true;
    t_ = std::thread(&CameraDev::run, this);
    return true;
}

void CameraDev::setExposure(float exposure)
{
    exposure_ = exposure;
    if (fd_ < 0)
        return;
    v4l2_control c{};
    c.id = V4L2_CID_EXPOSURE_AUTO;
    c.value = exposure < 0 ? V4L2_EXPOSURE_APERTURE_PRIORITY : V4L2_EXPOSURE_MANUAL;
    xioctl(fd_, VIDIOC_S_CTRL, &c);
    if (exposure >= 0)
    {
        c.id = V4L2_CID_EXPOSURE_ABSOLUTE;
        c.value = (int)exposure;
        xioctl(fd_, VIDIOC_S_CTRL, &c);
    }
}

void CameraDev::close()
{
    running_ = false;
    cv_.notify_all();
    if (t_.joinable())
        t_.join();
    stopV4l2();
}

std::string CameraDev::error() const
{
    std::lock_guard<std::mutex> l(m_);
    return error_;
}

bool CameraDev::startV4l2()
{
    fd_ = ::open(dev_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0)
    {
        error_ = "can't open " + dev_;
        return false;
    }
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = (uint32_t)w_;
    fmt.fmt.pix.height = (uint32_t)h_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0 || fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV)
    {
        error_ = dev_ + ": no YUYV mode";
        return false;
    }
    w_ = (int)fmt.fmt.pix.width; // the driver picks the nearest size
    h_ = (int)fmt.fmt.pix.height;

    v4l2_control c{};
    c.id = V4L2_CID_EXPOSURE_AUTO;
    c.value = exposure_ < 0 ? V4L2_EXPOSURE_APERTURE_PRIORITY : V4L2_EXPOSURE_MANUAL;
    xioctl(fd_, VIDIOC_S_CTRL, &c);
    if (exposure_ >= 0)
    {
        c.id = V4L2_CID_EXPOSURE_ABSOLUTE;
        c.value = (int)exposure_;
        xioctl(fd_, VIDIOC_S_CTRL, &c);
    }

    v4l2_requestbuffers req{};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0 || req.count < 2)
    {
        error_ = dev_ + ": no capture buffers";
        return false;
    }
    for (uint32_t i = 0; i < req.count; i++)
    {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &b) < 0)
            return false;
        void *p = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, b.m.offset);
        if (p == MAP_FAILED)
            return false;
        bufs_.push_back({p, b.length});
        if (xioctl(fd_, VIDIOC_QBUF, &b) < 0)
            return false;
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0)
    {
        error_ = dev_ + ": stream on failed";
        return false;
    }
    return true;
}

void CameraDev::stopV4l2()
{
    if (fd_ >= 0)
    {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(fd_, VIDIOC_STREAMOFF, &type);
    }
    for (auto &b : bufs_)
        munmap(b.first, b.second);
    bufs_.clear();
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
}

// Simulated picture. Bottom: dark background, bright part on the nozzle, offset and turned
// (PAP_SIM_PART="dx_px,dy_px,angle,w_px,h_px", default 12,-8,4,160,90).
// Top: dark board, bright fiducial disc (PAP_SIM_FID="dx_px,dy_px,r_px", default 6,4,25).
void CameraDev::simFrame(Frame &f)
{
    static std::mt19937 rng(1);
    std::uniform_int_distribution<int> noise(-6, 6);
    Gray g(w_, h_, simKind_ == 0 ? 25 : 45);
    float a[5];
    const char *env = getenv(simKind_ == 0 ? "PAP_SIM_PART" : "PAP_SIM_FID");
    const char *def = simKind_ == 0 ? "12,-8,4,160,90" : "6,4,25";
    int n = sscanf(env ? env : def, "%f,%f,%f,%f,%f", &a[0], &a[1], &a[2], &a[3], &a[4]);
    if (n < 3)
        sscanf(def, "%f,%f,%f,%f,%f", &a[0], &a[1], &a[2], &a[3], &a[4]);
    const float cx = w_ / 2.0f + a[0], cy = h_ / 2.0f + a[1];
    std::string qr;
    if (simKind_ == 1)
    {
        std::lock_guard<std::mutex> l(g_simQrM);
        qr = g_simQr;
    }
    if (simKind_ == 0)
        fillRotRect(g, cx, cy, a[3], a[4], a[2], 210);
    else if (qr.empty() || !drawQr(g, qr, std::max(3, std::min(w_, h_) / 90)))
        fillCircle(g, cx, cy, a[2], 220);
    for (auto &p : g.px)
        p = clamp8(p + noise(rng));
    f.gray = std::move(g);
    f.yuyv.clear();
}

void CameraDev::run()
{
    uint64_t seq = 0;
    while (running_)
    {
        Frame f;
        if (sim_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(66));
            simFrame(f);
        }
        else
        {
            pollfd p{fd_, POLLIN, 0};
            const int r = ::poll(&p, 1, 500);
            if (r <= 0)
                continue;
            v4l2_buffer b{};
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            if (xioctl(fd_, VIDIOC_DQBUF, &b) < 0)
            {
                if (errno != EAGAIN)
                {
                    std::lock_guard<std::mutex> l(m_);
                    error_ = dev_ + ": capture failed (unplugged?)";
                    running_ = false;
                }
                continue;
            }
            const uint8_t *data = (const uint8_t *)bufs_[b.index].first;
            const size_t need = (size_t)w_ * h_ * 2;
            if (b.bytesused >= need)
            {
                f.yuyv.assign(data, data + need);
                f.gray = Gray(w_, h_);
                for (size_t i = 0; i < (size_t)w_ * h_; i++)
                    f.gray.px[i] = data[i * 2];
            }
            xioctl(fd_, VIDIOC_QBUF, &b);
            if (f.gray.empty())
                continue;
        }
        f.ms = millis();
        f.seq = ++seq;
        {
            std::lock_guard<std::mutex> l(m_);
            last_ = std::move(f);
        }
        cv_.notify_all();
    }
}

bool CameraDev::frameAfter(uint32_t ms, Frame &out, int timeoutMs)
{
    std::unique_lock<std::mutex> l(m_);
    // A frame whose capture started after `ms`: wait one more frame time to be safe
    if (!cv_.wait_for(l, std::chrono::milliseconds(timeoutMs),
                      [&] { return !running_ || (!last_.gray.empty() && (int32_t)(last_.ms - ms) > 40); }))
        return false;
    if (last_.gray.empty())
        return false;
    out = last_;
    return true;
}

bool CameraDev::latest(Frame &out)
{
    std::lock_guard<std::mutex> l(m_);
    if (last_.gray.empty())
        return false;
    out = last_;
    return true;
}

// ---- both cameras -------------------------------------------------------------------------------
void Cameras::configure(int cam, const std::string &dev, int w, int h, float exposure)
{
    if (cam < 0 || cam > 1)
        return;
    std::lock_guard<std::mutex> l(m_);
    if (dev == cfgDev_[cam] && w == cfgW_[cam] && h == cfgH_[cam] && exposure == cfgExp_[cam] && dev_[cam].isOpen())
        return;
    cfgDev_[cam] = dev;
    cfgW_[cam] = w;
    cfgH_[cam] = h;
    cfgExp_[cam] = exposure;
    const bool simulate = sim_ && access(dev.c_str(), F_OK) != 0;
    dev_[cam].open(dev, w, h, exposure, simulate, cam);
}

bool Cameras::available(int cam) { return cam >= 0 && cam <= 1 && dev_[cam].isOpen(); }

float meanLevel(const Gray &g)
{
    if (g.empty())
        return 0;
    uint64_t sum = 0;
    size_t n = 0;
    for (size_t i = 0; i < g.px.size(); i += 4, n++)
        sum += g.px[i];
    return n ? (float)sum / n : 0;
}

void Cameras::setExposure(int cam, float exposure)
{
    if (cam < 0 || cam > 1)
        return;
    std::lock_guard<std::mutex> l(m_);
    cfgExp_[cam] = exposure;
    dev_[cam].setExposure(exposure);
}

bool Cameras::steadyFrame(int cam, Frame &out, int timeoutMs)
{
    if (!frameAfter(cam, millis(), out, timeoutMs))
        return false;
    bool autoExp;
    {
        std::lock_guard<std::mutex> l(m_);
        autoExp = cfgExp_[cam] < 0 && !dev_[cam].simulated();
    }
    if (!autoExp)
        return true;
    // brightness change below 1 / 255 twice in a row = the automatic exposure has settled
    const uint32_t t0 = millis();
    float prev = meanLevel(out.gray);
    int calm = 0;
    while (calm < 2 && millis() - t0 < STEADY_MS)
    {
        Frame f;
        if (!frameAfter(cam, out.ms, f, 1000))
            break; // keep the last one
        const float m = meanLevel(f.gray);
        calm = fabsf(m - prev) < 1.0f ? calm + 1 : 0;
        prev = m;
        out = f;
    }
    return true;
}

bool Cameras::frameAfter(int cam, uint32_t ms, Frame &out, int timeoutMs)
{
    return available(cam) && dev_[cam].frameAfter(ms, out, timeoutMs);
}

bool Cameras::latest(int cam, Frame &out) { return available(cam) && dev_[cam].latest(out); }

std::string Cameras::error(int cam)
{
    if (cam < 0 || cam > 1)
        return "no such camera";
    if (dev_[cam].isOpen())
        return "";
    const std::string e = dev_[cam].error();
    return e.empty() ? "camera not configured" : e;
}

void Cameras::setOverlay(int cam, const Overlay &o)
{
    if (cam < 0 || cam > 1)
        return;
    std::lock_guard<std::mutex> lock(om_);
    overlay_[cam] = o;
}

std::string Cameras::jpeg(int cam, int quality, int maxWidth, bool crosshair, float cx, float cy, bool overlay)
{
    Frame f;
    if (!latest(cam, f))
        return "";
    int w = f.gray.w, h = f.gray.h;
    if (maxWidth > 0 && w > maxWidth)
    {
        h = h * maxWidth / w;
        w = maxWidth;
    }
    std::vector<uint8_t> rgb = frameRgb(f, w, h);
    if (crosshair)
    {
        const float sx = (float)w / f.gray.w, sy = (float)h / f.gray.h;
        const int px = (int)((cx >= 0 ? cx : f.gray.w / 2.0f) * sx), py = (int)((cy >= 0 ? cy : f.gray.h / 2.0f) * sy);
        drawCross(rgb, w, h, px, py, w / 12, 255, 40, 40);
    }
    if (overlay)
        drawOverlay(cam, f, rgb, w, h);
    return encodeJpeg(rgb, w, h, quality);
}

void Cameras::drawOverlay(int cam, const Frame &f, std::vector<uint8_t> &rgb, int w, int h)
{
    Overlay o;
    if (cam >= 0 && cam <= 1)
    {
        std::lock_guard<std::mutex> lock(om_);
        o = overlay_[cam];
    }
    if (o.kind != Overlay::NONE && f.ms - o.ms < OVERLAY_MS && !f.gray.empty())
    {
        // green outline a few pixels outside what was found (so its edge stays visible), small cross at its centre
        const float sx = (float)w / f.gray.w, sy = (float)h / f.gray.h;
        const float pad = 4.0f;
        const uint8_t r = 40, g = 230, b = 90;
        switch (o.kind)
        {
        case Overlay::CIRCLE:
            drawCircle(rgb, w, h, o.x * sx, o.y * sy, o.radius * sx + pad, r, g, b);
            drawCross(rgb, w, h, (int)(o.x * sx), (int)(o.y * sy), 4, r, g, b);
            break;
        case Overlay::RECT:
        {
            RotRect rr = o.rect;
            rr.cx *= sx;
            rr.cy *= sy;
            rr.w = rr.w * sx + 2 * pad;
            rr.h = rr.h * sy + 2 * pad;
            drawRotRect(rgb, w, h, rr, r, g, b);
            drawCross(rgb, w, h, (int)rr.cx, (int)rr.cy, 4, r, g, b);
            break;
        }
        case Overlay::QUAD:
        {
            Pt q[4];
            for (int i = 0; i < 4; i++)
                q[i] = {o.quad[i].x * sx, o.quad[i].y * sy};
            drawPoly(rgb, w, h, q, 4, r, g, b);
            break;
        }
        default:
            break;
        }
    }
}

void Cameras::closeAll()
{
    dev_[0].close();
    dev_[1].close();
}
