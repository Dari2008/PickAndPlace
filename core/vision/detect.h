/**
 * detect.h - finding parts, fiducials, pockets and marks in a grayscale image (pixels)
 * -----------------------------------------------------------------------------
 * Pure functions on Gray images, so they can be tested with drawn images.
 *   PartPx p = detectPart(img, nozzleX, nozzleY, -1, false, 400);   // bright part, Otsu threshold
 *   Feature f = detectCircle(img, cx, cy, 150, 25, -1);              // fiducial ~25 px radius
 * Machine conversion (calibration matrix M, px -> mm):
 *   pxToMm(M, dxPx, dyPx, dx, dy);   angleToMachine(M, deg)
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <string>
#include "image.h"

struct PartPx
{
    bool ok = false;
    float cx = 0, cy = 0;   // centre of the smallest enclosing rectangle
    float angle = 0;        // long side, image degrees, normalised to (-45, 45]
    float w = 0, h = 0;     // px
    RotRect rect;           // as found (angle not normalised), for drawing
    std::string error;
};

struct Feature
{
    bool ok = false;
    float x = 0, y = 0; // px
    float size = 0;     // radius (circle) or sqrt(area)
    std::string error;
};

// Part on the nozzle (bottom camera): the blob nearest the nozzle centre, within maxRadiusPx.
// dark = part darker than the background. thresh < 0: Otsu.
PartPx detectPart(const Gray &g, float nozzleX, float nozzleY, int thresh, bool dark, float maxRadiusPx);
// Round bright mark (fiducial) of about radiusPx near (cx, cy), within searchPx
Feature detectCircle(const Gray &g, float cx, float cy, float searchPx, float radiusPx, int thresh);
// Dark region nearest (cx, cy) within searchPx (tape pocket seen from above)
Feature detectPocket(const Gray &g, float cx, float cy, float searchPx, int thresh);
// Any compact mark nearest (cx, cy): bright or dark, whichever stands out (calibration targets, nozzle tip)
Feature detectMark(const Gray &g, float cx, float cy, float searchPx);

void pxToMm(const float m[4], float dxPx, float dyPx, float &dxMm, float &dyMm);
float angleToMachine(const float m[4], float angleImgDeg); // result normalised to (-45, 45]
float mmPerPx(const float m[4]);                           // mean scale
float normalize45(float deg);

// Calibration from two moves: delta[0] = pixel shift for machine move (step, 0), delta[1] for (0, step).
// sign = +1: the object moved (bottom camera), -1: the camera moved (top camera).
bool matrixFromMoves(const float dpx[2][2], float step, float sign, float m[4]);
