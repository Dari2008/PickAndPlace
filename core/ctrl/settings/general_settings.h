/**
 * general_settings.h - Pi settings in /settings.txt (the Octopus has its own: machine_store.h)
 * -----------------------------------------------------------------------------
 * Every setting is one "key = value" line. The table in general_settings.cpp lists
 * them all (key, type, range, default); GET / SET (protocol.md) and the file use the
 * same table. Machine values (steps, speeds, currents, homing, z_home, syringe_d,
 * paste_speed, rot_speed, e0_speed, e1_speed, pump_power ...) live on the Octopus.
 *
 * Job:       travel_speed head1_reach head2_reach pick_height pick_ms place_ms paste_output
 *            paste_retract head1_offset_x/_y reject_x/_y beep
 * Joystick:  joy_x_min/_center/_max (same for y) joy_deadzone joy_invert_x/_y joy_swap joy_jog_speed joy_hold_ms
 * Vacuum:    vac_baseline vac_pick_min vac_check vac_retries vac_wait_ms
 * Cameras:   cam_<bottom|top>_dev _w _h _exposure _thresh, cam_*_m00/_m01/_m10/_m11 (px -> mm),
 *            cam_bottom_cx/_cy (nozzle centre px), cam_bottom_x/_y/_z (where the nozzle looks),
 *            cam_top_off_x/_y (camera centre minus nozzle, mm), cam_bottom_invert, cam_settle_ms
 * Vision:    vision_parts vision_fiducials vision_pockets vision_place_check vision_required
 *            vision_max_offset vision_max_angle fid_diameter fid_search pocket_search
 *            light_top light_bottom cal_step backlight
 * Nozzles:   nozzle_count nozzle_first_x/_y nozzle_last_x/_y nozzle_z_pick nozzle_z_drop nozzle_z_safe
 *            nozzle_slide_x/_y nozzle_speed nozzle_wait_ms nozzle_sensor_invert nozzle_check (see ../nozzles.h)
 *            nozzle_touchoff
 * Load cell: loadcell loadcell_x/_y loadcell_height loadcell_speed/_slow loadcell_backoff loadcell_depth
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <Arduino.h>
#include <motion.h>

enum PasteOutput : uint8_t
{
    PASTE_NONE,
    PASTE_MOTOR
};

enum ZHome : uint8_t
{
    ZHOME_NONE,
    ZHOME_FLAG
};

#define TEXT_SETTING_LEN 48

struct CamSettings
{
    char dev[TEXT_SETTING_LEN];
    uint16_t w, h;
    float exposure;  // -1 = auto
    float thresh;    // -1 = Otsu
    float m[4];      // px -> mm: [m00 m01; m10 m11] (scale, rotation, skew)
};

class GeneralSettings
{
public:
    // ---- job --------------------------------------------------------------------------
    uint8_t beep;
    float travelSpeed;
    float headReach[2];
    float pickHeight;
    uint16_t pickMs;
    uint16_t placeMs;
    uint8_t pasteOutput; // PasteOutput
    float pasteRetract;  // microlitres
    float head1Offset[2];
    float rejectX, rejectY;

    // ---- joystick (ADS1115 raw counts) ------------------------------------------------
    float joyMin[2], joyCenter[2], joyMax[2]; // x, y
    uint8_t joyDeadzone;                      // % of half travel
    uint8_t joyInvert[2];
    uint8_t joySwap;
    float joyJogSpeed; // mm/s at full deflection
    uint16_t joyHoldMs;

    // ---- vacuum sensor (BMP280) -------------------------------------------------------
    float vacBaseline; // hPa, pump off
    float vacPickMin;  // hPa below the baseline that means "part on the nozzle"
    uint8_t vacCheck;
    uint8_t vacRetries;
    uint16_t vacWaitMs;

    // ---- cameras ----------------------------------------------------------------------
    CamSettings cam[2]; // 0 = bottom, 1 = top
    float camBottomCenter[2];  // nozzle centre in the bottom image, px
    float camBottomPos[3];     // machine X, Y, Z where the nozzle is above the bottom camera
    float camTopOffset[2];     // top camera centre - nozzle, mm
    uint8_t camBottomInvert;   // part darker than the background
    float camBottomRotSign;    // +1 / -1: a positive nozzle turn turns the part this way in machine axes
    uint16_t camSettleMs;      // wait after a move before a picture

    // ---- vision -----------------------------------------------------------------------
    uint8_t visionParts, visionFiducials, visionPockets, visionPlaceCheck, visionRequired;
    float visionMaxOffset; // mm
    float visionMaxAngle;  // deg
    float fidDiameter, fidSearch, pocketSearch;
    uint8_t lightTop, lightBottom; // %
    float calStep;                 // mm moved during a camera calibration
    uint8_t backlight;             // %

    // ---- nozzle changer (../nozzles.h) ------------------------------------------------------
    uint8_t feederWheelTeeth;     // teeth of the feeder feed wheel (feeder teeth = ... per advance)
    uint8_t feederQr;             // auto detect reads each feeder's QR code with the top camera
    float feederQr0[2];           // machine X / Y of the QR code of a feeder found at E1 = 0
    float feederQrDir[2];         // how far the QR code moves in X / Y per mm of E1 (rail along X: 1 / 0)
    uint8_t nozzleCount;          // slots in the rack, 0 = no changer
    float nozzleFirst[2], nozzleLast[2]; // machine X / Y of slot 1 and of the last slot
    float nozzleZPick;            // seesaw Z that pushes the nozzle on (negative = nozzle side down)
    float nozzleZDrop;            // seesaw Z at which a nozzle sits in its slot to be pulled off
    float nozzleSafeZ;            // seesaw Z for moving to / from the rack
    float nozzleSlide[2];         // sideways slide into / out of the slot (0 / 0 = straight down and up)
    float nozzleSpeed;            // mm/s for the moves at the rack
    uint16_t nozzleWaitMs;        // pause while pushed on
    uint8_t nozzleSensorInvert;   // 0: sensor bit set = nozzle in the slot
    uint8_t nozzleBit[8];         // which bit of the tool changer byte is slot n+1 (NOZZLE LEARN)
    uint8_t nozzleCheck;          // check every step with the slot sensors
    uint8_t nozzleCalibrate;      // after every pick-up: bottom camera measures the tip offset
    uint8_t nozzleTouchoff;       // after every pick-up: touch the load cell, store the tip height (dz)

    // ---- load cell (HX711 on the Octopus EXP1): nozzle tip height ---------------------------
    uint8_t loadcell;             // a load cell is fitted
    float loadcellPos[2];         // machine X / Y of the pad (the vacuum nozzle, head 2, above it)
    float loadcellHeight;         // pad surface above the bed, mm
    float loadcellSpeed;          // first touch, mm/s
    float loadcellSlow;           // second, precise touch, mm/s
    float loadcellBackoff;        // lift between the two touches, mm
    float loadcellDepth;          // search this far past the expected contact, mm

    GeneralSettings() { setDefaults(); }
    void setDefaults();
    bool load();
    bool save();
    void applySpeeds() {}

    uint8_t count() const;
    void keyAt(uint8_t i, char *key) const; // key: at least 40 bytes
    void valueAt(uint8_t i, char *out, uint8_t size) const;
    int find(const char *key) const;
    // type: 'b' bool, 'c' choice (paste_output), 't' text, 'n' number (integer: whole numbers only)
    void metaAt(uint8_t i, char &type, float &min, float &max, float &def, bool &integer) const;
    PGM_P setValue(uint8_t i, const char *value);
};

extern GeneralSettings settings;

// Octopus machine value (cached GET of the board), e.g. mval("e1_speed", 30)
float mval(const char *key, float def);
// "x", "y", "z", "e0", "e1"
const char *axisKey(uint8_t axis);
