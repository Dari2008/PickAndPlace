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

// ---- lens distortion ---------------------------------------------------------------------------
// Radial model about the picture centre: undistorted = c + (p - c) * (1 + k1 r^2 + k2 r^4),
// r = |p - c| / half diagonal (so k1 / k2 don't depend on the resolution).

void undistortPx(const float k[2], float w, float h, float &x, float &y)
{
    if (k[0] == 0 && k[1] == 0)
        return;
    const float cx = w / 2.0f, cy = h / 2.0f, R2 = cx * cx + cy * cy;
    const float qx = x - cx, qy = y - cy, r2 = (qx * qx + qy * qy) / R2;
    const float f = 1.0f + k[0] * r2 + k[1] * r2 * r2;
    x = cx + qx * f;
    y = cy + qy * f;
}

void camPxToMm(const float m[4], const float k[2], float w, float h, float px, float py, float refX, float refY, float &dxMm,
               float &dyMm)
{
    undistortPx(k, w, h, px, py);
    undistortPx(k, w, h, refX, refY);
    pxToMm(m, px - refX, py - refY, dxMm, dyMm);
}

// Gaussian elimination with partial pivoting, n <= 8
static bool solveLinear(double A[8][8], double b[8], int n, double x[8])
{
    for (int c = 0; c < n; c++)
    {
        int piv = c;
        for (int r = c + 1; r < n; r++)
            if (fabs(A[r][c]) > fabs(A[piv][c]))
                piv = r;
        if (fabs(A[piv][c]) < 1e-12)
            return false;
        if (piv != c)
        {
            for (int j = 0; j < n; j++)
                std::swap(A[c][j], A[piv][j]);
            std::swap(b[c], b[piv]);
        }
        for (int r = c + 1; r < n; r++)
        {
            const double f = A[r][c] / A[c][c];
            for (int j = c; j < n; j++)
                A[r][j] -= f * A[c][j];
            b[r] -= f * b[c];
        }
    }
    for (int r = n - 1; r >= 0; r--)
    {
        double s = b[r];
        for (int j = r + 1; j < n; j++)
            s -= A[r][j] * x[j];
        x[r] = s / A[r][r];
    }
    return true;
}

// Least squares: (p - c) = a + A * move - k1 v1 - k2 v2, with v = (p - c) r^2 / r^4. Linear in the 8
// unknowns k1 k2 a0 a1 A00 A01 A10 A11 (lens = false: k fixed at 0, 6 unknowns). rms in px.
static bool fitAffine(const float (*mv)[2], const float (*px)[2], int n, float w, float h, bool lens, double u[8], float &rms)
{
    const float cx = w / 2.0f, cy = h / 2.0f, R2 = cx * cx + cy * cy;
    const int nu = lens ? 8 : 6, o = lens ? 2 : 0; // index of a0 among the unknowns
    double N[8][8] = {}, rhs[8] = {};
    for (int i = 0; i < n; i++)
    {
        const double qx = px[i][0] - cx, qy = px[i][1] - cy, r2 = (qx * qx + qy * qy) / R2;
        for (int axis = 0; axis < 2; axis++)
        {
            double row[8] = {};
            const double q = axis ? qy : qx;
            if (lens)
            {
                row[0] = -q * r2;
                row[1] = -q * r2 * r2;
            }
            row[o + axis] = 1; // a0 / a1
            row[o + 2 + axis * 2] = mv[i][0];
            row[o + 3 + axis * 2] = mv[i][1];
            for (int a = 0; a < nu; a++)
            {
                rhs[a] += row[a] * q;
                for (int b = 0; b < nu; b++)
                    N[a][b] += row[a] * row[b];
            }
        }
    }
    double x[8] = {};
    if (!solveLinear(N, rhs, nu, x))
        return false;
    for (int i = 0; i < 8; i++)
        u[i] = 0;
    for (int i = 0; i < nu; i++)
        u[i + (lens ? 0 : 2)] = x[i];
    // residual in undistorted px
    double sum = 0;
    for (int i = 0; i < n; i++)
    {
        const double qx = px[i][0] - cx, qy = px[i][1] - cy, r2 = (qx * qx + qy * qy) / R2;
        const double fk = 1 + u[0] * r2 + u[1] * r2 * r2;
        const double ex = qx * fk - (u[2] + u[4] * mv[i][0] + u[5] * mv[i][1]);
        const double ey = qy * fk - (u[3] + u[6] * mv[i][0] + u[7] * mv[i][1]);
        sum += ex * ex + ey * ey;
    }
    rms = (float)sqrt(sum / n);
    return true;
}

bool fitLens(const float (*moves)[2], const float (*px)[2], int n, float w, float h, float sign, float m[4], float k[2],
             float &rmsLinear, float &rmsLens)
{
    if (n < 5)
        return false;
    double lin[8], len[8];
    if (!fitAffine(moves, px, n, w, h, false, lin, rmsLinear) || !fitAffine(moves, px, n, w, h, true, len, rmsLens))
        return false;
    // A = undistorted px per mm of head move; px -> mm = sign * inverse(A) (as matrixFromMoves)
    const double a = len[4], b = len[5], c = len[6], d = len[7], det = a * d - b * c;
    if (fabs(det) < 1e-9)
        return false;
    m[0] = (float)(sign * d / det);
    m[1] = (float)(sign * -b / det);
    m[2] = (float)(sign * -c / det);
    m[3] = (float)(sign * a / det);
    k[0] = (float)len[0];
    k[1] = (float)len[1];
    return true;
}

// ---- checkerboard -------------------------------------------------------------------------------
// Two ways: the corners where four squares meet (X corners, found locally: uneven light, ink that makes the dark
// squares touch and soft focus don't matter much), and as fallback the squares themselves as blobs. Either way the
// points are put on a grid (i, j) by growing from a point near the picture centre.

// Grows a grid from start point o: each point predicts its 4 neighbours with its own local steps
static void growGrid(const std::vector<Pt> &p, int o, std::vector<GridPt> &pts)
{
    pts.clear();
    const int n = (int)p.size();
    auto d2 = [&](const Pt &a, const Pt &b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); };
    // Board axes at the start: nearest neighbour u, then the nearest one about square to it
    int iu = -1, iv = -1;
    for (int i = 0; i < n; i++)
        if (i != o && (iu < 0 || d2(p[i], p[o]) < d2(p[iu], p[o])))
            iu = i;
    if (iu < 0)
        return;
    const float ux = p[iu].x - p[o].x, uy = p[iu].y - p[o].y, ul = sqrtf(ux * ux + uy * uy);
    for (int i = 0; i < n; i++)
    {
        if (i == o || i == iu)
            continue;
        const float vx = p[i].x - p[o].x, vy = p[i].y - p[o].y, vl = sqrtf(vx * vx + vy * vy);
        if (vl > 1.4f * ul || fabsf(ux * vx + uy * vy) > 0.4f * ul * vl)
            continue;
        if (iv < 0 || vl < sqrtf(d2(p[iv], p[o])))
            iv = i;
    }
    if (iv < 0)
        return;
    struct Node
    {
        int i, j;
        float u[2], v[2];
    };
    std::vector<int> owner(n, -1);
    std::vector<Node> node(n);
    std::vector<int> queue;
    owner[o] = 0;
    node[o] = {0, 0, {ux, uy}, {p[iv].x - p[o].x, p[iv].y - p[o].y}};
    queue.push_back(o);
    for (size_t q = 0; q < queue.size(); q++)
    {
        const int a = queue[q];
        const Node na = node[a];
        const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++)
        {
            const float sx = di[k] * na.u[0] + dj[k] * na.v[0], sy = di[k] * na.u[1] + dj[k] * na.v[1];
            const Pt want = {p[a].x + sx, p[a].y + sy};
            const float tol2 = 0.35f * 0.35f * (sx * sx + sy * sy);
            int best = -1;
            for (int i = 0; i < n; i++)
                if (d2(p[i], want) < tol2 && (best < 0 || d2(p[i], want) < d2(p[best], want)))
                    best = i;
            if (best < 0 || owner[best] >= 0)
                continue;
            owner[best] = 0;
            Node nb = na;
            nb.i += di[k], nb.j += dj[k];
            const float step[2] = {(p[best].x - p[a].x) * (di[k] + dj[k]), (p[best].y - p[a].y) * (di[k] + dj[k])};
            if (di[k])
                nb.u[0] = step[0], nb.u[1] = step[1];
            else
                nb.v[0] = step[0], nb.v[1] = step[1];
            node[best] = nb;
            queue.push_back(best);
        }
    }
    for (int a : queue)
        pts.push_back({p[a].x, p[a].y, node[a].i, node[a].j});
}

// The biggest grid grown from the few points nearest the picture centre (one stray point there doesn't spoil it)
static bool gridFromPoints(const std::vector<Pt> &p, int w, int h, std::vector<GridPt> &pts, std::string &err, const char *noun)
{
    pts.clear();
    const int n = (int)p.size();
    const float cx = w / 2.0f, cy = h / 2.0f;
    std::vector<int> order(n);
    for (int i = 0; i < n; i++)
        order[i] = i;
    auto dc = [&](int i) { return (p[i].x - cx) * (p[i].x - cx) + (p[i].y - cy) * (p[i].y - cy); };
    std::sort(order.begin(), order.end(), [&](int a, int b) { return dc(a) < dc(b); });
    std::vector<GridPt> g;
    for (int t = 0; t < std::min(n, 6); t++)
    {
        growGrid(p, order[t], g);
        if (g.size() > pts.size())
            pts = g;
    }
    if (pts.size() < 12)
    {
        err = "only " + std::to_string(pts.size()) + " " + noun + " on the grid (need 12)";
        return false;
    }
    return true;
}

static inline float bilinear(const Gray &g, float x, float y)
{
    const int x0 = (int)floorf(x), y0 = (int)floorf(y);
    const float fx = x - x0, fy = y - y0;
    const float a = g.at(x0, y0) + (g.at(x0 + 1, y0) - g.at(x0, y0)) * fx;
    const float b = g.at(x0, y0 + 1) + (g.at(x0 + 1, y0 + 1) - g.at(x0, y0 + 1)) * fx;
    return a + (b - a) * fy;
}

// X corners at ring radius r (ChESS response: opposite ring points alike, quarter-turned ones different),
// refined to a fraction of a pixel on the gradients
static void xCorners(const Gray &g, int r, int contrast, std::vector<Pt> &out)
{
    out.clear();
    const int w = g.w, h = g.h, m = r + 2;
    if (w <= 2 * m || h <= 2 * m)
        return;
    int ox[16], oy[16];
    for (int i = 0; i < 16; i++)
    {
        const float a = i * 3.14159265f / 8;
        ox[i] = (int)lrintf(r * cosf(a)), oy[i] = (int)lrintf(r * sinf(a));
    }
    std::vector<int> R((size_t)w * h, 0);
    int rmax = 0;
    for (int y = m; y < h - m; y++)
        for (int x = m; x < w - m; x++)
        {
            int v[16], ring = 0;
            for (int i = 0; i < 16; i++)
                ring += v[i] = g.at(x + ox[i], y + oy[i]);
            int sum = 0, diff = 0;
            for (int i = 0; i < 4; i++)
                sum += abs(v[i] + v[i + 8] - v[i + 4] - v[i + 12]);
            for (int i = 0; i < 8; i++)
                diff += abs(v[i] - v[i + 8]);
            int local = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    local += g.at(x + dx, y + dy);
            // ring mean vs. centre (x16), only a quarter of it: with ink bleed the centre of a corner is dark
            const int mean = abs(ring * 9 - local * 16) / 36;
            const int resp = sum - diff - mean;
            if (resp > 0)
            {
                R[(size_t)y * w + x] = resp;
                rmax = std::max(rmax, resp);
            }
        }
    // A clean corner gives about 8 x its contrast; keep the clear ones, also where the light is weaker
    const int thr = std::max(rmax / 6, contrast);
    const int nms = std::max(2, r / 2);
    for (int y = m; y < h - m; y++)
        for (int x = m; x < w - m; x++)
        {
            const int c = R[(size_t)y * w + x];
            if (c < thr)
                continue;
            bool top = true;
            for (int dy = -nms; dy <= nms && top; dy++)
                for (int dx = -nms; dx <= nms; dx++)
                {
                    const int yy = y + dy, xx = x + dx;
                    if (yy < 0 || yy >= h || xx < 0 || xx >= w || (dx == 0 && dy == 0))
                        continue;
                    const int o = R[(size_t)yy * w + xx];
                    if (o > c || (o == c && (dy < 0 || (dy == 0 && dx < 0))))
                    {
                        top = false;
                        break;
                    }
                }
            if (!top)
                continue;
            // Sub-pixel: a checkerboard corner looks the same turned by 180 deg (also with ink bleed and blur), so
            // move q until I(q + d) = I(q - d) around it (Gauss-Newton on bilinear samples)
            float qx = (float)x, qy = (float)y;
            const int win = std::max(3, r);
            for (int it = 0; it < 10; it++)
            {
                if (qx - win < 2 || qy - win < 2 || qx + win >= w - 3 || qy + win >= h - 3)
                    break;
                double a11 = 0, a12 = 0, a22 = 0, b1 = 0, b2 = 0;
                for (int dy = 0; dy <= win; dy++)
                    for (int dx = -win; dx <= win; dx++)
                    {
                        if ((dy == 0 && dx <= 0) || dx * dx + dy * dy > win * win)
                            continue;
                        const float px = qx + dx, py = qy + dy, mx = qx - dx, my = qy - dy;
                        const double e = bilinear(g, px, py) - bilinear(g, mx, my);
                        const double jx = (bilinear(g, px + 0.5f, py) - bilinear(g, px - 0.5f, py)) -
                                          (bilinear(g, mx + 0.5f, my) - bilinear(g, mx - 0.5f, my));
                        const double jy = (bilinear(g, px, py + 0.5f) - bilinear(g, px, py - 0.5f)) -
                                          (bilinear(g, mx, my + 0.5f) - bilinear(g, mx, my - 0.5f));
                        a11 += jx * jx, a12 += jx * jy, a22 += jy * jy;
                        b1 -= jx * e, b2 -= jy * e;
                    }
                const double det = a11 * a22 - a12 * a12;
                if (det <= 1e-6)
                    break;
                float sx = (float)((a22 * b1 - a12 * b2) / det), sy = (float)((a11 * b2 - a12 * b1) / det);
                const float len = sqrtf(sx * sx + sy * sy);
                if (len > 1.0f) // at most a pixel per step
                    sx /= len, sy /= len;
                qx += sx, qy += sy;
                if (len < 0.005f)
                    break;
            }
            if (fabsf(qx - x) > r * 0.5f || fabsf(qy - y) > r * 0.5f)
                continue; // ran off: no clean corner here
            // one corner can give several peaks (a dark cross where the ink spread): keep the first
            const float near2 = std::max(3.0f, r * 0.6f) * std::max(3.0f, r * 0.6f);
            bool dup = false;
            for (const Pt &o : out)
                if ((o.x - qx) * (o.x - qx) + (o.y - qy) * (o.y - qy) < near2)
                {
                    dup = true;
                    break;
                }
            if (!dup)
                out.push_back({qx, qy});
        }
}

static bool detectGridCorners(const Gray &g0, std::vector<GridPt> &pts, std::string &err)
{
    const Gray g = boxBlur3(g0);
    int hist[256] = {};
    for (uint8_t v : g.px)
        hist[v]++;
    const int total = (int)g.px.size();
    int lo = 0, hi = 255, acc = 0;
    for (int v = 0; v < 256; v++)
        if ((acc += hist[v]) >= total / 20)
        {
            lo = v;
            break;
        }
    acc = 0;
    for (int v = 255; v >= 0; v--)
        if ((acc += hist[v]) >= total / 20)
        {
            hi = v;
            break;
        }
    if (hi - lo < 30)
    {
        err = "no contrast (light, focus?)";
        return false;
    }
    // Ring sizes for small to big squares; the one with the biggest grid wins
    pts.clear();
    std::vector<Pt> c;
    std::vector<GridPt> best;
    size_t seen = 0;
    for (int r : {3, 5, 8, 12, 18})
    {
        xCorners(g, r, hi - lo, c);
        seen = std::max(seen, c.size());
        std::string e;
        if (c.size() >= 12 && gridFromPoints(c, g.w, g.h, best, e, "corners") && best.size() > pts.size())
            pts = best;
    }
    if (pts.size() < 12)
    {
        err = "only " + std::to_string(std::max(pts.size(), seen)) + " corners seen";
        return false;
    }
    return true;
}

// Squares as blobs (dark and bright ones), their centres on the grid. Fallback when the corners don't work.
static bool detectGridSquares(const Gray &g0, std::vector<GridPt> &pts, std::string &err)
{
    pts.clear();
    if (g0.empty())
    {
        err = "no image";
        return false;
    }
    const Gray g = boxBlur3(g0);
    // Contrast from the 10 / 90 % brightness; thresholds a bit away from the middle, so squares that only
    // touch at a corner come apart
    int hist[256] = {};
    for (uint8_t v : g.px)
        hist[v]++;
    const int total = (int)g.px.size();
    int lo = 0, hi = 255, acc = 0;
    for (int v = 0; v < 256; v++)
        if ((acc += hist[v]) >= total / 10)
        {
            lo = v;
            break;
        }
    acc = 0;
    for (int v = 255; v >= 0; v--)
        if ((acc += hist[v]) >= total / 10)
        {
            hi = v;
            break;
        }
    if (hi - lo < 30)
    {
        err = "no contrast (light, focus?)";
        return false;
    }
    const int t = otsu(g), margin = (hi - lo) / 6;
    std::vector<Pt> c;
    std::vector<float> area;
    for (int dark = 0; dark < 2; dark++)
        for (const Blob &b : findBlobs(g, dark ? t - margin : t + margin, dark, 12))
        {
            if (b.touchesBorder)
                continue;
            const float box = (float)(b.maxX - b.minX + 1) * (b.maxY - b.minY + 1);
            if (b.area < 0.35f * box)
                continue;
            // roughly as wide as long (square at any angle)
            const float tr = b.mu20 + b.mu02, d = sqrtf((b.mu20 - b.mu02) * (b.mu20 - b.mu02) + 4 * b.mu11 * b.mu11);
            if (tr <= 0 || (tr + d) > 3.0f * std::max(tr - d, 1e-3f))
                continue;
            c.push_back({b.cx, b.cy});
            area.push_back((float)b.area);
        }
    if (c.size() < 9)
    {
        err = "only " + std::to_string(c.size()) + " squares seen (square size vs. picture, focus, light?)";
        return false;
    }
    // Squares of a similar size only
    std::vector<float> sorted = area;
    std::sort(sorted.begin(), sorted.end());
    const float med = sorted[sorted.size() / 2];
    std::vector<Pt> p;
    for (size_t i = 0; i < c.size(); i++)
        if (area[i] > 0.35f * med && area[i] < 2.5f * med)
            p.push_back(c[i]);
    return gridFromPoints(p, g.w, g.h, pts, err, "squares");
}

bool detectGrid(const Gray &g0, std::vector<GridPt> &pts, std::string &err)
{
    pts.clear();
    if (g0.empty())
    {
        err = "no image";
        return false;
    }
    std::string e1, e2;
    if (detectGridCorners(g0, pts, e1))
        return true;
    std::vector<GridPt> sq;
    if (detectGridSquares(g0, sq, e2))
    {
        pts = sq;
        return true;
    }
    err = e1 + ", " + e2 + " (board filling the picture, sharp, even light without glare; cal_grid = square size?)";
    return false;
}

bool fitGrid(const std::vector<GridPt> &pts, float sizeMm, float w, float h, float k[2], float A[4], float &rmsLinear,
             float &rmsLens)
{
    const int n = (int)pts.size();
    if (n < 6 || sizeMm <= 0)
        return false;
    std::vector<float> mv(2 * n), px(2 * n);
    for (int i = 0; i < n; i++)
    {
        mv[2 * i] = pts[i].i * sizeMm, mv[2 * i + 1] = pts[i].j * sizeMm;
        px[2 * i] = pts[i].x, px[2 * i + 1] = pts[i].y;
    }
    const float(*m2)[2] = reinterpret_cast<const float(*)[2]>(mv.data());
    const float(*p2)[2] = reinterpret_cast<const float(*)[2]>(px.data());
    double lin[8], len[8];
    if (!fitAffine(m2, p2, n, w, h, false, lin, rmsLinear) || !fitAffine(m2, p2, n, w, h, true, len, rmsLens))
        return false;
    k[0] = (float)len[0], k[1] = (float)len[1];
    for (int i = 0; i < 4; i++)
        A[i] = (float)len[4 + i];
    return fabsf(A[0] * A[3] - A[1] * A[2]) > 1e-9f;
}

void nearestOrthogonal(const float M[4], float Q[4])
{
    if (M[0] * M[3] - M[1] * M[2] >= 0)
    {
        const float a = atan2f(M[2] - M[1], M[0] + M[3]);
        Q[0] = cosf(a), Q[1] = -sinf(a), Q[2] = sinf(a), Q[3] = cosf(a);
    }
    else
    {
        const float a = atan2f(M[2] + M[1], M[0] - M[3]);
        Q[0] = cosf(a), Q[1] = sinf(a), Q[2] = sinf(a), Q[3] = -cosf(a);
    }
}
