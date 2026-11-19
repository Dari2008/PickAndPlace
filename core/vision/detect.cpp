#include "detect.h"

#include <algorithm>
#include <cmath>

static const float PI_F = 3.14159265f;

float normalize45(float a)
{
    while (a > 45.0f)
        a -= 90.0f;
    while (a <= -45.0f)
        a += 90.0f;
    return a;
}

static int thresholdFor(const Gray &g, int thresh)
{
    return thresh >= 0 ? thresh : otsu(g);
}

PartPx detectPart(const Gray &g, float nx, float ny, int thresh, bool dark, float maxRadiusPx)
{
    PartPx r;
    if (g.empty())
    {
        r.error = "no image";
        return r;
    }
    const int t = thresholdFor(g, thresh);
    const std::vector<Blob> blobs = findBlobs(g, t, dark, 30);
    const Blob *best = nullptr;
    float bestD = 1e30f;
    for (const Blob &b : blobs)
    {
        if (b.touchesBorder)
            continue;
        // Contains or is near the nozzle centre
        const float d = hypotf(b.cx - nx, b.cy - ny);
        if (d > maxRadiusPx)
            continue;
        if (d < bestD)
        {
            bestD = d;
            best = &b;
        }
    }
    if (!best)
    {
        r.error = blobs.empty() ? "no part in the picture" : "no part near the nozzle";
        return r;
    }
    const RotRect rr = minAreaRect(*best);
    r.ok = true;
    r.cx = rr.cx;
    r.cy = rr.cy;
    r.w = rr.w;
    r.h = rr.h;
    r.angle = normalize45(rr.angleDeg);
    r.rect = rr;
    return r;
}

Feature detectCircle(const Gray &g, float cx, float cy, float searchPx, float radiusPx, int thresh)
{
    Feature f;
    if (g.empty())
    {
        f.error = "no image";
        return f;
    }
    const int t = thresholdFor(g, thresh);
    const std::vector<Blob> blobs = findBlobs(g, t, false, 8);
    const float want = PI_F * radiusPx * radiusPx;
    float bestD = 1e30f;
    for (const Blob &b : blobs)
    {
        if (b.touchesBorder || b.area < want * 0.4f || b.area > want * 2.2f || b.circularity() < 0.55f)
            continue;
        const float d = hypotf(b.cx - cx, b.cy - cy);
        if (d <= searchPx && d < bestD)
        {
            bestD = d;
            f.ok = true;
            f.x = b.cx;
            f.y = b.cy;
            f.size = sqrtf(b.area / PI_F);
        }
    }
    if (!f.ok)
        f.error = "no fiducial of that size near the expected spot";
    return f;
}

Feature detectPocket(const Gray &g, float cx, float cy, float searchPx, int thresh)
{
    Feature f;
    if (g.empty())
    {
        f.error = "no image";
        return f;
    }
    const int t = thresholdFor(g, thresh);
    const std::vector<Blob> blobs = findBlobs(g, t, true, 30);
    float bestD = 1e30f;
    for (const Blob &b : blobs)
    {
        if (b.touchesBorder)
            continue;
        const float d = hypotf(b.cx - cx, b.cy - cy);
        if (d <= searchPx && d < bestD)
        {
            bestD = d;
            f.ok = true;
            f.x = b.cx;
            f.y = b.cy;
            f.size = sqrtf((float)b.area);
        }
    }
    if (!f.ok)
        f.error = "no pocket near the expected spot";
    return f;
}

Feature detectMark(const Gray &g, float cx, float cy, float searchPx)
{
    Feature best;
    if (g.empty())
    {
        best.error = "no image";
        return best;
    }
    const int t = otsu(g);
    float bestScore = -1;
    for (int dark = 0; dark <= 1; dark++)
    {
        const std::vector<Blob> blobs = findBlobs(g, t, dark != 0, 15);
        for (const Blob &b : blobs)
        {
            if (b.touchesBorder)
                continue;
            const float d = hypotf(b.cx - cx, b.cy - cy);
            if (d > searchPx)
                continue;
            // compact, near the centre
            const float score = b.circularity() / (1.0f + d / std::max(1.0f, searchPx));
            if (score > bestScore)
            {
                bestScore = score;
                best.ok = true;
                best.x = b.cx;
                best.y = b.cy;
                best.size = sqrtf((float)b.area);
            }
        }
    }
    if (!best.ok)
        best.error = "no mark near the image centre";
    return best;
}

void pxToMm(const float m[4], float dxPx, float dyPx, float &dxMm, float &dyMm)
{
    dxMm = m[0] * dxPx + m[1] * dyPx;
    dyMm = m[2] * dxPx + m[3] * dyPx;
}

float angleToMachine(const float m[4], float angleImgDeg)
{
    const float a = angleImgDeg * PI_F / 180.0f;
    float vx, vy;
    pxToMm(m, cosf(a), sinf(a), vx, vy);
    return normalize45(atan2f(vy, vx) * 180.0f / PI_F);
}

float mmPerPx(const float m[4]) { return sqrtf(fabsf(m[0] * m[3] - m[1] * m[2])); }

bool matrixFromMoves(const float dpx[2][2], float step, float sign, float m[4])
{
    // M * [dpx0 dpx1] = sign * step * I  ->  M = sign * step * inverse([dpx0 dpx1])
    const float a = dpx[0][0], b = dpx[1][0], c = dpx[0][1], d = dpx[1][1]; // columns = the two shifts
    const float det = a * d - b * c;
    if (fabsf(det) < 1e-6f)
        return false;
    const float k = sign * step / det;
    m[0] = k * d;
    m[1] = -k * b;
    m[2] = -k * c;
    m[3] = k * a;
    return true;
}
