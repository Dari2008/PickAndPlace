/**
 * vision.h - what the job runner and the commands need from the cameras and sensors
 * -----------------------------------------------------------------------------
 * Bottom camera (looks up): part on the nozzle -> offset (mm, machine axes) and
 * angle (deg). Top camera (on the head, looks down): fiducials, tape pockets,
 * placement photos. All offsets are in machine millimetres through the calibration
 * matrices (settings cam_*_m00..m11, cam_bottom_cx/cy, cam_top_off_x/y).
 *
 *   vision::PartResult r;  vision::measurePart(r);   // nozzle already above the bottom camera
 *   float dx, dy;  vision::findFiducial(dx, dy, err);   // top camera centred on the expected spot
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <string>

namespace vision
{
    enum Cam
    {
        BOTTOM = 0,
        TOP = 1
    };

    struct PartResult
    {
        bool ok = false;
        float dx = 0, dy = 0; // part centre minus nozzle centre, machine mm
        float angle = 0;      // part angle in machine axes, deg (0 = as picked from a tape)
        float widthMm = 0, heightMm = 0;
        float rotSign = 1;    // machine-frame turn per positive nozzle degree (+1 / -1)
        std::string error;
    };

    bool available(Cam cam);       // camera delivers frames
    bool measurePart(PartResult &r);
    // Nozzle tip (no part) above the bottom camera: offset from the rotation centre (cam_bottom_cx/_cy), mm
    bool measureNozzle(float &dx, float &dy, std::string &err);
    // Feature next to the image centre of the top camera, offset in machine mm (feature - centre)
    bool findFiducial(float &dx, float &dy, std::string &err);
    bool findPocket(float &dx, float &dy, std::string &err);
    bool capture(Cam cam, const std::string &dataPath); // JPEG into the data folder ("/captures/x.jpg")
    // QR code in the top camera picture (the one nearest the centre); tries a few frames
    bool readQr(std::string &text, std::string &err);

    // Vacuum sensor (BMP280 in the vacuum line)
    bool vacuumAvailable();
    float vacuumHpa();       // absolute pressure, NAN without sensor
    float vacuumDropHpa();   // vac_baseline - pressure
    bool partOnNozzle();     // drop >= vac_pick_min (true when no sensor: can't tell)
}
