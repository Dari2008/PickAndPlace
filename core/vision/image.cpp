#include "image.h"

#include <algorithm>
#include <cmath>

static const float PI_F = 3.14159265f;

float Blob::circularity() const
{
    if (perimeter <= 0)
        return 0;
    // Boundary pixel count underestimates the perimeter by ~pi/4 on a grid: correct for it
    const float p = perimeter * 1.12f;
    return std::min(1.0f, 4.0f * PI_F * area / (p * p));
}

float Blob::majorAngleDeg() const { return 0.5f * atan2f(2.0f * mu11, mu20 - mu02) * 180.0f / PI_F; }

int otsu(const Gray &g, int x0, int y0, int x1, int y1)
{
    if (x1 < 0)
        x1 = g.w;
    if (y1 < 0)
        y1 = g.h;
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(g.w, x1);
    y1 = std::min(g.h, y1);
    long hist[256] = {0};
    long n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            hist[g.at(x, y)]++;
            n++;
        }
    if (!n)
        return 128;
    double sum = 0;
    for (int i = 0; i < 256; i++)
        sum += (double)i * hist[i];
    double sumB = 0, best = -1;
    long wB = 0;
    int t = 128;
    for (int i = 0; i < 256; i++)
    {
        wB += hist[i];
        if (!wB)
            continue;
        const long wF = n - wB;
        if (!wF)
            break;
        sumB += (double)i * hist[i];
        const double mB = sumB / wB, mF = (sum - sumB) / wF;
        const double between = (double)wB * wF * (mB - mF) * (mB - mF);
        if (between > best)
        {
            best = between;
            t = i;
        }
    }
    return t;
}

std::vector<Blob> findBlobs(const Gray &g, int thresh, bool dark, int minArea)
{
    std::vector<Blob> out;
    if (g.empty())
        return out;
    const int W = g.w, H = g.h;
    // Otsu splits into <= t and > t
    auto on = [&](int x, int y) { return dark ? g.at(x, y) <= thresh : g.at(x, y) > thresh; };
    std::vector<int32_t> label((size_t)W * H, 0);
    std::vector<int> stack;
    int next = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            if (label[(size_t)y * W + x] || !on(x, y))
                continue;
            next++;
            Blob b;
            b.minX = b.maxX = x;
            b.minY = b.maxY = y;
            double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
            stack.clear();
            stack.push_back(y * W + x);
            label[(size_t)y * W + x] = next;
            while (!stack.empty())
            {
                const int i = stack.back();
                stack.pop_back();
                const int px = i % W, py = i / W;
                b.area++;
                sx += px;
                sy += py;
                sxx += (double)px * px;
                syy += (double)py * py;
                sxy += (double)px * py;
                b.minX = std::min(b.minX, px);
                b.maxX = std::max(b.maxX, px);
                b.minY = std::min(b.minY, py);
                b.maxY = std::max(b.maxY, py);
                bool edge = false;
                const int nx[4] = {px - 1, px + 1, px, px};
                const int ny[4] = {py, py, py - 1, py + 1};
                for (int k = 0; k < 4; k++)
                {
                    if (nx[k] < 0 || ny[k] < 0 || nx[k] >= W || ny[k] >= H)
                    {
                        edge = true;
                        b.touchesBorder = true;
                        continue;
                    }
                    const size_t j = (size_t)ny[k] * W + nx[k];
                    if (!on(nx[k], ny[k]))
                    {
                        edge = true;
                        continue;
                    }
                    if (!label[j])
                    {
                        label[j] = next;
                        stack.push_back((int)j);
                    }
                }
                if (edge)
                {
                    b.perimeter++;
                    b.boundary.push_back({(float)px, (float)py});
                }
            }
            if (b.area < minArea)
                continue;
            b.cx = (float)(sx / b.area);
            b.cy = (float)(sy / b.area);
            b.mu20 = (float)(sxx / b.area - (double)b.cx * b.cx);
            b.mu02 = (float)(syy / b.area - (double)b.cy * b.cy);
            b.mu11 = (float)(sxy / b.area - (double)b.cx * b.cy);
            out.push_back(std::move(b));
        }
    std::sort(out.begin(), out.end(), [](const Blob &a, const Blob &b) { return a.area > b.area; });
    return out;
}

static float cross(const Pt &o, const Pt &a, const Pt &b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

std::vector<Pt> convexHull(std::vector<Pt> p)
{
    if (p.size() < 3)
        return p;
    std::sort(p.begin(), p.end(), [](const Pt &a, const Pt &b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::vector<Pt> h(2 * p.size());
    size_t k = 0;
    for (size_t i = 0; i < p.size(); i++)
    {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0)
            k--;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i-- > 0;)
    {
        while (k >= t && cross(h[k - 2], h[k - 1], p[i]) <= 0)
            k--;
        h[k++] = p[i];
    }
    h.resize(k - 1);
    return h;
}

RotRect minAreaRect(const Blob &b)
{
    RotRect best;
    // Pixel centres -> use the pixel corners so a 1-pixel-wide blob still has a size
    std::vector<Pt> pts;
    pts.reserve(b.boundary.size() * 4);
    for (const Pt &p : b.boundary)
    {
        pts.push_back({p.x - 0.5f, p.y - 0.5f});
        pts.push_back({p.x + 0.5f, p.y - 0.5f});
        pts.push_back({p.x - 0.5f, p.y + 0.5f});
        pts.push_back({p.x + 0.5f, p.y + 0.5f});
    }
    const std::vector<Pt> hull = convexHull(pts);
    if (hull.size() < 3)
        return best;
    float bestArea = 1e30f;
    for (size_t i = 0; i < hull.size(); i++)
    {
        const Pt &a = hull[i], &c = hull[(i + 1) % hull.size()];
        float ex = c.x - a.x, ey = c.y - a.y;
        const float len = hypotf(ex, ey);
        if (len < 1e-6f)
            continue;
        ex /= len;
        ey /= len;
        float minU = 1e30f, maxU = -1e30f, minV = 1e30f, maxV = -1e30f;
        for (const Pt &p : hull)
        {
            const float u = p.x * ex + p.y * ey, v = -p.x * ey + p.y * ex;
            minU = std::min(minU, u);
            maxU = std::max(maxU, u);
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }
        const float area = (maxU - minU) * (maxV - minV);
        if (area < bestArea)
        {
            bestArea = area;
            const float cu = (minU + maxU) / 2, cv = (minV + maxV) / 2;
            best.cx = cu * ex - cv * ey;
            best.cy = cu * ey + cv * ex;
            float w = maxU - minU, h = maxV - minV;
            float ang = atan2f(ey, ex) * 180.0f / PI_F;
            if (h > w)
            {
                std::swap(w, h);
                ang += 90.0f;
            }
            while (ang > 90.0f)
                ang -= 180.0f;
            while (ang <= -90.0f)
                ang += 180.0f;
            best.w = w;
            best.h = h;
            best.angleDeg = ang;
        }
    }
    return best;
}

Gray boxBlur3(const Gray &g)
{
    Gray o(g.w, g.h);
    for (int y = 0; y < g.h; y++)
        for (int x = 0; x < g.w; x++)
        {
            int s = 0, n = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                {
                    const int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < g.w && yy < g.h)
                    {
                        s += g.at(xx, yy);
                        n++;
                    }
                }
            o.at(x, y) = (uint8_t)(s / n);
        }
    return o;
}

Gray scaleDown(const Gray &g, int f)
{
    if (f <= 1)
        return g;
    Gray o(g.w / f, g.h / f);
    for (int y = 0; y < o.h; y++)
        for (int x = 0; x < o.w; x++)
        {
            int s = 0;
            for (int dy = 0; dy < f; dy++)
                for (int dx = 0; dx < f; dx++)
                    s += g.at(x * f + dx, y * f + dy);
            o.at(x, y) = (uint8_t)(s / (f * f));
        }
    return o;
}

void fillRotRect(Gray &g, float cx, float cy, float w, float h, float angleDeg, uint8_t v)
{
    const float a = angleDeg * PI_F / 180.0f, c = cosf(a), s = sinf(a);
    const float r = hypotf(w, h) / 2 + 1;
    for (int y = std::max(0, (int)(cy - r)); y < std::min(g.h, (int)(cy + r) + 1); y++)
        for (int x = std::max(0, (int)(cx - r)); x < std::min(g.w, (int)(cx + r) + 1); x++)
        {
            const float dx = x - cx, dy = y - cy;
            const float u = dx * c + dy * s, vv = -dx * s + dy * c;
            if (fabsf(u) <= w / 2 && fabsf(vv) <= h / 2)
                g.at(x, y) = v;
        }
}

void fillCircle(Gray &g, float cx, float cy, float r, uint8_t v)
{
    for (int y = std::max(0, (int)(cy - r - 1)); y < std::min(g.h, (int)(cy + r) + 2); y++)
        for (int x = std::max(0, (int)(cx - r - 1)); x < std::min(g.w, (int)(cx + r) + 2); x++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
                g.at(x, y) = v;
}

static void putRgb(std::vector<uint8_t> &rgb, int w, int h, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || y < 0 || x >= w || y >= h)
        return;
    uint8_t *p = &rgb[((size_t)y * w + x) * 3];
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

static void lineRgb(std::vector<uint8_t> &rgb, int w, int h, float x0, float y0, float x1, float y1, uint8_t r, uint8_t g,
                    uint8_t b)
{
    const int n = (int)std::max(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
    for (int i = 0; i <= n; i++)
    {
        const float t = (float)i / n;
        const int x = (int)lroundf(x0 + (x1 - x0) * t), y = (int)lroundf(y0 + (y1 - y0) * t);
        putRgb(rgb, w, h, x, y, r, g, b);
        putRgb(rgb, w, h, x + 1, y, r, g, b);
    }
}

void drawCross(std::vector<uint8_t> &rgb, int w, int h, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b)
{
    lineRgb(rgb, w, h, (float)cx - size, (float)cy, (float)cx + size, (float)cy, r, g, b);
    lineRgb(rgb, w, h, (float)cx, (float)cy - size, (float)cx, (float)cy + size, r, g, b);
}

void drawRotRect(std::vector<uint8_t> &rgb, int w, int h, const RotRect &rr, uint8_t r, uint8_t g, uint8_t b)
{
    const float a = rr.angleDeg * PI_F / 180.0f, c = cosf(a), s = sinf(a);
    Pt p[4];
    const float hw = rr.w / 2, hh = rr.h / 2;
    const float du[4] = {-hw, hw, hw, -hw}, dv[4] = {-hh, -hh, hh, hh};
    for (int i = 0; i < 4; i++)
        p[i] = {rr.cx + du[i] * c - dv[i] * s, rr.cy + du[i] * s + dv[i] * c};
    drawPoly(rgb, w, h, p, 4, r, g, b);
}

void drawCircle(std::vector<uint8_t> &rgb, int w, int h, float cx, float cy, float radius, uint8_t r, uint8_t g, uint8_t b)
{
    const int n = std::max(16, (int)(radius * 0.8f)); // segments: short enough to look round
    Pt prev = {cx + radius, cy};
    for (int i = 1; i <= n; i++)
    {
        const float a = 2.0f * PI_F * i / n;
        const Pt p = {cx + radius * cosf(a), cy + radius * sinf(a)};
        lineRgb(rgb, w, h, prev.x, prev.y, p.x, p.y, r, g, b);
        prev = p;
    }
}

void drawPoly(std::vector<uint8_t> &rgb, int w, int h, const Pt *p, int n, uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < n; i++)
        lineRgb(rgb, w, h, p[i].x, p[i].y, p[(i + 1) % n].x, p[(i + 1) % n].y, r, g, b);
}
