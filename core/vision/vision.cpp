#include "vision.h"
#include "camera.h"
#include "detect.h"
#include "../ctrl/settings/general_settings.h"
#include "../dev/sensors.h"
#include "../compat/SdFat.h"

extern "C"
{
#include "../third_party/quirc/quirc.h"
}
#include <cstring>

#include <cmath>
#include <cstdio>

namespace vision
{
    // Open / reopen the cameras with the current settings
    static void configure(Cam c)
    {
        const CamSettings &s = settings.cam[c];
        cams.configure(c, s.dev, s.w, s.h, s.exposure);
    }

    // Show what was found in the live picture (camera.h, Overlay)
    static void markCircle(Cam cam, const Frame &f, float x, float y, float radius)
    {
        Overlay o;
        o.kind = Overlay::CIRCLE;
        o.x = x;
        o.y = y;
        o.radius = radius;
        o.ms = f.ms;
        cams.setOverlay(cam, o);
    }

    static void markRect(Cam cam, const Frame &f, const RotRect &rr)
    {
        Overlay o;
        o.kind = Overlay::RECT;
        o.rect = rr;
        o.ms = f.ms;
        cams.setOverlay(cam, o);
    }

    bool available(Cam cam)
    {
        configure(cam);
        return cams.available(cam);
    }

    bool measureNozzle(float &dx, float &dy, std::string &err)
    {
        configure(BOTTOM);
        Frame f;
        if (!cams.frameAfter(BOTTOM, millis(), f, 2500))
        {
            err = "bottom camera: " + (cams.error(BOTTOM).empty() ? std::string("no picture") : cams.error(BOTTOM));
            return false;
        }
        const float *m = settings.cam[BOTTOM].m;
        const float nx = settings.camBottomCenter[0] >= 0 ? settings.camBottomCenter[0] : f.gray.w / 2.0f;
        const float ny = settings.camBottomCenter[1] >= 0 ? settings.camBottomCenter[1] : f.gray.h / 2.0f;
        const float searchPx = settings.visionMaxOffset * 2.0f / std::max(mmPerPx(m), 1e-4f);
        const Feature t = detectMark(f.gray, nx, ny, searchPx);
        if (!t.ok)
        {
            err = "nozzle tip not found: " + t.error;
            return false;
        }
        markCircle(BOTTOM, f, t.x, t.y, t.size * 0.6f); // size = sqrt(area): radius of that disc ~0.56 size
        pxToMm(m, t.x - nx, t.y - ny, dx, dy);
        if (hypotf(dx, dy) > settings.visionMaxOffset)
        {
            err = "nozzle tip too far off centre";
            return false;
        }
        return true;
    }

    bool readQr(std::string &text, std::string &err)
    {
        configure(TOP);
        struct quirc *q = quirc_new();
        if (!q)
            return err = "out of memory", false;
        bool found = false;
        err = "no QR code in the picture";
        for (int attempt = 0; attempt < 3 && !found; attempt++)
        {
            Frame f;
            if (!cams.frameAfter(TOP, millis(), f, 2500))
            {
                err = "top camera: " + (cams.error(TOP).empty() ? std::string("no picture") : cams.error(TOP));
                break;
            }
            if (quirc_resize(q, f.gray.w, f.gray.h) < 0)
            {
                err = "out of memory";
                break;
            }
            int w = 0, h = 0;
            uint8_t *buf = quirc_begin(q, &w, &h);
            memcpy(buf, f.gray.px.data(), (size_t)w * h);
            quirc_end(q);
            float best = 1e30f;
            for (int i = 0; i < quirc_count(q); i++)
            {
                struct quirc_code code;
                struct quirc_data data;
                quirc_extract(q, i, &code);
                if (quirc_decode(&code, &data) != QUIRC_SUCCESS)
                {
                    // a mirrored code (seen from behind glass) decodes after flipping
                    quirc_flip(&code);
                    if (quirc_decode(&code, &data) != QUIRC_SUCCESS)
                        continue;
                }
                float cx = 0, cy = 0;
                for (auto &c : code.corners)
                    cx += c.x / 4.0f, cy += c.y / 4.0f;
                const float d = hypotf(cx - w / 2.0f, cy - h / 2.0f);
                if (d < best)
                {
                    best = d;
                    text.assign((const char *)data.payload, (size_t)data.payload_len);
                    found = true;
                    Overlay o;
                    o.kind = Overlay::QUAD;
                    for (int k = 0; k < 4; k++)
                        o.quad[k] = {(float)code.corners[k].x, (float)code.corners[k].y};
                    o.ms = f.ms;
                    cams.setOverlay(TOP, o);
                }
            }
        }
        quirc_destroy(q);
        return found;
    }

    bool measurePart(PartResult &r)
    {
        r = PartResult();
        configure(BOTTOM);
        Frame f;
        if (!cams.frameAfter(BOTTOM, millis(), f, 2500))
        {
            r.error = "bottom camera: " + (cams.error(BOTTOM).empty() ? std::string("no picture") : cams.error(BOTTOM));
            return false;
        }
        const float *m = settings.cam[BOTTOM].m;
        const float nx = settings.camBottomCenter[0] >= 0 ? settings.camBottomCenter[0] : f.gray.w / 2.0f;
        const float ny = settings.camBottomCenter[1] >= 0 ? settings.camBottomCenter[1] : f.gray.h / 2.0f;
        const float maxR = settings.visionMaxOffset * 2.0f / std::max(mmPerPx(m), 1e-4f);
        const PartPx p = detectPart(f.gray, nx, ny, (int)settings.cam[BOTTOM].thresh, settings.camBottomInvert, maxR);
        if (!p.ok)
        {
            r.error = p.error;
            return false;
        }
        markRect(BOTTOM, f, p.rect);
        pxToMm(m, p.cx - nx, p.cy - ny, r.dx, r.dy);
        r.rotSign = settings.camBottomRotSign >= 0 ? 1.0f : -1.0f;
        r.angle = r.rotSign * angleToMachine(m, p.angle);
        const float k = mmPerPx(m);
        r.widthMm = p.w * k;
        r.heightMm = p.h * k;
        r.ok = true;
        return true;
    }

    static bool topFeature(bool pocket, float &dx, float &dy, std::string &err)
    {
        configure(TOP);
        Frame f;
        if (!cams.frameAfter(TOP, millis(), f, 2500))
        {
            err = "top camera: " + (cams.error(TOP).empty() ? std::string("no picture") : cams.error(TOP));
            return false;
        }
        const float *m = settings.cam[TOP].m;
        const float k = std::max(mmPerPx(m), 1e-4f);
        const float cx = f.gray.w / 2.0f, cy = f.gray.h / 2.0f;
        Feature ft = pocket ? detectPocket(f.gray, cx, cy, settings.pocketSearch / k, (int)settings.cam[TOP].thresh)
                            : detectCircle(f.gray, cx, cy, settings.fidSearch / k, settings.fidDiameter / 2.0f / k,
                                           (int)settings.cam[TOP].thresh);
        if (!ft.ok)
        {
            err = ft.error;
            return false;
        }
        if (pocket)
        {
            RotRect rr; // size = sqrt(area): a square of that side
            rr.cx = ft.x;
            rr.cy = ft.y;
            rr.w = rr.h = ft.size;
            markRect(TOP, f, rr);
        }
        else
            markCircle(TOP, f, ft.x, ft.y, ft.size); // size = radius
        pxToMm(m, ft.x - cx, ft.y - cy, dx, dy);
        return true;
    }

    bool findFiducial(float &dx, float &dy, std::string &err) { return topFeature(false, dx, dy, err); }
    bool findPocket(float &dx, float &dy, std::string &err) { return topFeature(true, dx, dy, err); }

    bool capture(Cam cam, const std::string &dataPath)
    {
        configure(cam);
        const std::string jpg = cams.jpeg(cam, 85, 0, false, -1, -1, false);
        if (jpg.empty())
            return false;
        FILE *fp = fopen(sdfs_path(dataPath.c_str()).c_str(), "wb");
        if (!fp)
            return false;
        const bool ok = fwrite(jpg.data(), 1, jpg.size(), fp) == jpg.size();
        fclose(fp);
        return ok;
    }

    bool vacuumAvailable() { return sensors.bmpPresent() || sensors.simulated(); }
    float vacuumHpa() { return sensors.pressureHpa(); }
    float vacuumDropHpa() { return settings.vacBaseline - sensors.pressureHpa(); }

    bool partOnNozzle()
    {
        if (!vacuumAvailable() || std::isnan(sensors.pressureHpa()))
            return true; // can't tell: assume it is there
        return vacuumDropHpa() >= settings.vacPickMin;
    }
}
