/**
 * image.h - small grayscale image toolkit for the pick & place cameras (no OpenCV)
 * -----------------------------------------------------------------------------
 *   Gray g(w, h);                          // 8-bit, row-major
 *   int t = otsu(g);                       // global threshold
 *   auto blobs = findBlobs(g, t, false);   // connected regions brighter than t
 *   RotRect r = minAreaRect(blobs[0]);     // centre, size, angle of the smallest enclosing rectangle
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Gray
{
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    Gray() {}
    Gray(int w_, int h_, uint8_t v = 0) : w(w_), h(h_), px((size_t)w_ * h_, v) {}
    uint8_t at(int x, int y) const { return px[(size_t)y * w + x]; }
    uint8_t &at(int x, int y) { return px[(size_t)y * w + x]; }
    bool empty() const { return w <= 0 || h <= 0; }
};

struct Pt
{
    float x, y;
};

struct Blob
{
    int area = 0;
    float cx = 0, cy = 0;          // centroid
    float mu20 = 0, mu02 = 0, mu11 = 0; // central second moments / area
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    int perimeter = 0;             // boundary pixels
    bool touchesBorder = false;
    std::vector<Pt> boundary;      // boundary pixel centres (for the hull)
    float circularity() const;     // 1 = perfect disc
    float majorAngleDeg() const;   // orientation of the major axis
};

struct RotRect
{
    float cx = 0, cy = 0, w = 0, h = 0; // w >= h
    float angleDeg = 0;                 // direction of the long side, -90 .. 90
};

int otsu(const Gray &g, int x0 = 0, int y0 = 0, int x1 = -1, int y1 = -1);
// Regions of pixels > thresh (or <= thresh with dark = true), 4-connected, at least minArea pixels
std::vector<Blob> findBlobs(const Gray &g, int thresh, bool dark, int minArea = 20);
std::vector<Pt> convexHull(std::vector<Pt> pts);
RotRect minAreaRect(const Blob &b);
Gray boxBlur3(const Gray &g);
Gray scaleDown(const Gray &g, int factor);

// Drawing for overlays / tests
void fillRotRect(Gray &g, float cx, float cy, float w, float h, float angleDeg, uint8_t v);
void fillCircle(Gray &g, float cx, float cy, float r, uint8_t v);
void drawCross(std::vector<uint8_t> &rgb, int w, int h, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b);
void drawRotRect(std::vector<uint8_t> &rgb, int w, int h, const RotRect &rr, uint8_t r, uint8_t g, uint8_t b);
void drawCircle(std::vector<uint8_t> &rgb, int w, int h, float cx, float cy, float radius, uint8_t r, uint8_t g, uint8_t b);
void drawPoly(std::vector<uint8_t> &rgb, int w, int h, const Pt *p, int n, uint8_t r, uint8_t g, uint8_t b); // closed
