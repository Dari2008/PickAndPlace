#include "catalog.h"
#include "general_settings.h"
#include "../../octo/octopus.h"

#include <cmath>
#include <cstdio>

// Groups in display order
static const std::vector<std::string> GROUPS = {
    "Job & placing",
    "Paste",
    "Vacuum & outputs",
    "Nozzle changer",
    "Load cell",
    "Feeders",
    "Vision",
    "Bottom camera",
    "Top camera",
    "Lights",
    "Homing",
    "Machine size & jogging",
    "Axis X",
    "Axis Y",
    "Axis Z (seesaw)",
    "Axis ROT (nozzle)",
    "Axis PASTE (extruder)",
    "Axis E0 (feeder drive)",
    "Axis E1 (feeder gantry)",
    "Axis X2 (second X motor)",
    "Joystick",
    "Display & sound",
    "Safety & inputs",
};

const std::vector<std::string> &settingsGroups() { return GROUPS; }

namespace
{
    struct Builder
    {
        std::vector<SettingMeta> list;
        std::string group;

        // Pi setting: range / default / type from general_settings
        void pi(const char *key, const char *label, const char *unit, const char *help, int decimals = 2,
                const char *choices = nullptr)
        {
            SettingMeta m;
            m.key = key, m.group = group, m.label = label, m.unit = unit, m.help = help, m.decimals = decimals;
            const int i = settings.find(key);
            if (i >= 0)
            {
                char type;
                bool integer;
                settings.metaAt((uint8_t)i, type, m.min, m.max, m.def, integer);
                m.kind = type == 'b' ? "bool" : type == 'c' ? "choice" : type == 't' ? "text" : "num";
                if (integer)
                    m.decimals = 0;
                if (type == 'c')
                    m.choices = "none|motor";
            }
            if (choices)
                m.kind = "choice", m.choices = choices;
            list.push_back(m);
        }

        // Octopus setting
        SettingMeta &oc(const std::string &key, const std::string &label, const char *unit, float def, float mn, float mx,
                        int decimals, const char *help, const char *choices = nullptr, bool isBool = false)
        {
            SettingMeta m;
            m.key = key, m.group = group, m.label = label, m.unit = unit, m.help = help;
            m.def = def, m.min = mn, m.max = mx, m.decimals = decimals, m.machine = true;
            if (choices)
                m.kind = "choice", m.choices = choices;
            if (isBool)
                m.kind = "bool";
            list.push_back(m);
            return list.back();
        }
    };

    std::vector<SettingMeta> build()
    {
        Builder b;

        b.group = "Job & placing";
        b.pi("travel_speed", "Travel speed", "mm/s", "Moves between parts when the job sets no speed", 0);
        b.pi("pick_height", "Pick height", "mm", "Nozzle tip above the bed at a pocket");
        b.pi("pick_ms", "Pick wait", "ms", "After the valve opens, before lifting");
        b.pi("place_ms", "Place wait", "ms", "After the valve closes, before lifting");
        b.pi("head1_reach", "Paste head reach", "mm", "Seesaw travel from level until the paste tip touches the bed");
        b.pi("head2_reach", "Nozzle reach", "mm", "Seesaw travel from level until the nozzle touches the bed");
        b.pi("head1_offset_x", "Paste tip offset X", "mm", "Paste tip relative to the nozzle");
        b.pi("head1_offset_y", "Paste tip offset Y", "mm", "Paste tip relative to the nozzle");
        b.pi("reject_x", "Drop point X", "mm", "Where skipped parts are dropped; -1 = where they are");
        b.pi("reject_y", "Drop point Y", "mm", "Where skipped parts are dropped; -1 = where they are");

        b.group = "Paste";
        b.pi("paste_output", "Paste output", "", "What pushes the paste: the extruder motor, or nothing (no dispenser fitted)");
        b.oc("syringe_d", "Syringe inner diameter", "mm", 10, 0.1f, 100, 2, "Turns microlitres into plunger travel");
        b.oc("paste_speed", "Paste speed", "ul/s", 1, 0.001f, 1000, 3, "While a dot is pushed out");
        b.pi("paste_retract", "Pull back after a dot", "ul", "Against oozing");

        b.group = "Vacuum & outputs";
        b.oc("pump_used", "Pump connected", "", 1, 0, 1, 0,
             "Off = no vacuum pump on FAN5: pump commands (jobs, buttons) keep the output off", nullptr, true);
        b.oc("pump_power", "Pump power", "%", 100, 0, 100, 0, "Pump output when switched on");
        b.pi("vac_check", "Check the vacuum after a pick", "", "Needs the BMP280 in the vacuum line");
        b.pi("vac_baseline", "Pressure with the pump off", "hPa", "Set by Calibration > Vacuum");
        b.pi("vac_pick_min", "Pressure drop with a part", "hPa", "Below this the pick counts as failed");
        b.pi("vac_retries", "Pick retries", "", "Pick again this often before skipping the part", 0);
        b.pi("vac_wait_ms", "Wait before checking", "ms", "After the valve opens", 0);
        b.oc("cam_fan", "Head camera fan", "%", 50, 0, 100, 0, "Octopus FAN2, always on (also after an e-stop)");
        b.oc("box_fan", "Housing fan", "%", 100, 0, 100, 0, "Octopus FAN3, rear fan of the control box, always on");
        b.oc("pi_fan", "Pi housing fan", "%", 100, 0, 100, 0, "Octopus FAN4, 30 mm fan of the Raspberry Pi housing, always on");

        b.group = "Nozzle changer";
        b.pi("nozzle_count", "Slots in the rack", "", "0 = no changer, max 8", 0);
        b.pi("nozzle_first_x", "Slot 1 X", "mm", "Machine position of the first slot");
        b.pi("nozzle_first_y", "Slot 1 Y", "mm", "Machine position of the first slot");
        b.pi("nozzle_last_x", "Last slot X", "mm", "The slots between are spaced evenly");
        b.pi("nozzle_last_y", "Last slot Y", "mm", "The slots between are spaced evenly");
        b.pi("nozzle_z_pick", "Push-on height", "mm", "Seesaw Z that pushes a nozzle on (negative = nozzle side down)");
        b.pi("nozzle_z_drop", "Park height", "mm", "Seesaw Z at which a nozzle sits in its slot");
        b.pi("nozzle_z_safe", "Travel height at the rack", "mm", "Seesaw Z for moving to and from the rack");
        b.pi("nozzle_slide_x", "Slide X", "mm", "Sideways slide into / out of a slot; 0 = straight down and up");
        b.pi("nozzle_slide_y", "Slide Y", "mm", "Sideways slide into / out of a slot");
        b.pi("nozzle_speed", "Speed at the rack", "mm/s", "Moves into and out of the slots");
        b.pi("nozzle_wait_ms", "Pause while pushed on", "ms", "", 0);
        b.pi("nozzle_check", "Check with the slot sensors", "", "Every step of a change is checked");
        b.pi("nozzle_sensor_invert", "Sensor bit 0 = nozzle present", "", "Turn on if the sensors read the other way");
        for (int i = 1; i <= 8; i++)
            b.pi(("nozzle_bit_" + std::to_string(i)).c_str(), ("Sensor bit of slot " + std::to_string(i)).c_str(), "", "Set by Calibration > Nozzle sensor order", 0);
        b.pi("nozzle_calibrate", "Measure the nozzle after a change", "", "Bottom camera finds the tip and stores its offset");
        b.pi("nozzle_touchoff", "Touch off after a change", "", "Load cell measures the tip height and stores it (needs a load cell)");

        b.group = "Load cell";
        b.pi("loadcell", "Load cell fitted", "", "HX711 on the Octopus EXP1: measures the nozzle tip height");
        b.pi("loadcell_x", "Pad X", "mm", "Machine position with the vacuum nozzle over the pad");
        b.pi("loadcell_y", "Pad Y", "mm", "Machine position with the vacuum nozzle over the pad");
        b.pi("loadcell_height", "Pad height", "mm", "Pad surface above the bed (same zero as the board)");
        b.pi("loadcell_speed", "First touch speed", "mm/s", "Seesaw speed for the first, rough touch");
        b.pi("loadcell_slow", "Second touch speed", "mm/s", "Slow, precise touch: this one is measured");
        b.pi("loadcell_backoff", "Lift between touches", "mm", "");
        b.pi("loadcell_depth", "Search depth", "mm", "How far past the expected contact it searches");
        b.oc("lc_trigger", "Trigger force", "g", 30, 1, 5000, 0, "The seesaw stops at this force");
        b.oc("lc_scale", "Scale", "counts/g", 420, -100000, 100000, 3, "Set by LOADCELL CAL <grams> (Sensors)");

        b.group = "Feeders";
        b.pi("feeder_wheel_teeth", "Feed wheel teeth", "", "Teeth of the feed wheel; feeders advance by whole teeth", 0);
        b.oc("e0_speed", "Feed motor speed", "°/s", 180, 1, 3600, 0, "E0 while advancing a feeder");
        b.oc("e1_speed", "Gantry speed", "mm/s", 30, 0.1f, 1000, 1, "E1 in jobs");
        b.pi("feeder_qr", "Read QR codes in auto detect", "", "Top camera reads each feeder's code");
        b.pi("feeder_qr_x0", "QR position X at E1 = 0", "mm", "Set by Teach QR position");
        b.pi("feeder_qr_y0", "QR position Y at E1 = 0", "mm", "Set by Teach QR position");
        b.pi("feeder_qr_dx", "QR moves in X per mm of E1", "", "Rail along X: 1");
        b.pi("feeder_qr_dy", "QR moves in Y per mm of E1", "", "Rail along X: 0");

        b.group = "Vision";
        b.pi("vision_parts", "Correct parts with the bottom camera", "", "Offset and rotation of every part");
        b.pi("vision_fiducials", "Measure board fiducials", "", "At job start, from [board] fiducial = x, y");
        b.pi("vision_pockets", "Correct pockets with the top camera", "", "Finds the pocket before every pick");
        b.pi("vision_place_check", "Photo of every placed part", "", "Saved in captures/");
        b.pi("vision_required", "Pause when the camera finds nothing", "", "Off: the part is placed without correction");
        b.pi("vision_max_offset", "Largest accepted offset", "mm", "Parts further off are skipped");
        b.pi("vision_max_angle", "Largest accepted angle", "°", "Parts turned further are skipped");
        b.pi("fid_diameter", "Fiducial diameter", "mm", "");
        b.pi("fid_search", "Fiducial search radius", "mm", "");
        b.pi("pocket_search", "Pocket search radius", "mm", "");
        b.pi("cal_step", "Calibration move", "mm", "How far the head moves during a camera calibration");

        b.group = "Bottom camera";
        b.pi("cam_bottom_dev", "Device", "", "/dev/video0 or better /dev/v4l/by-id/...");
        b.pi("cam_bottom_w", "Picture width", "px", "", 0);
        b.pi("cam_bottom_h", "Picture height", "px", "", 0);
        b.pi("cam_bottom_exposure", "Exposure", "", "-1 = automatic");
        b.pi("cam_bottom_thresh", "Detection threshold", "", "-1 = automatic", 0);
        b.pi("cam_bottom_invert", "Dark parts on a bright background", "", "");
        b.pi("cam_bottom_rot_sign", "Rotation direction", "", "+1 / -1, set by Calibration > Rotation direction", 0);
        b.pi("light_bottom", "Light brightness", "%", "NeoPixel ring while the bottom camera looks", 0);
        b.pi("cam_bottom_x", "Camera position X", "mm", "Head position with the nozzle over the camera");
        b.pi("cam_bottom_y", "Camera position Y", "mm", "");
        b.pi("cam_bottom_z", "Camera position Z", "mm", "Seesaw Z for the picture");
        b.pi("cam_bottom_cx", "Rotation centre X", "px", "-1 = picture centre (set by the calibration)", 1);
        b.pi("cam_bottom_cy", "Rotation centre Y", "px", "", 1);
        for (const char *m : {"m00", "m01", "m10", "m11"})
            b.pi((std::string("cam_bottom_") + m).c_str(), (std::string("Pixel > mm ") + m).c_str(), "",
                 "Scale, rotation and skew (set by the calibration)", 5);

        b.group = "Top camera";
        b.pi("cam_top_dev", "Device", "", "/dev/video2 or better /dev/v4l/by-id/...");
        b.pi("cam_top_w", "Picture width", "px", "", 0);
        b.pi("cam_top_h", "Picture height", "px", "", 0);
        b.pi("cam_top_exposure", "Exposure", "", "-1 = automatic");
        b.pi("cam_top_thresh", "Detection threshold", "", "-1 = automatic", 0);
        b.pi("light_top", "Light while looking", "%", "Top light during pictures", 0);
        b.pi("cam_top_off_x", "Camera offset X", "mm", "Camera centre minus nozzle (Calibration > Top camera offset)");
        b.pi("cam_top_off_y", "Camera offset Y", "mm", "");
        b.pi("cam_settle_ms", "Settle time before a picture", "ms", "", 0);
        for (const char *m : {"m00", "m01", "m10", "m11"})
            b.pi((std::string("cam_top_") + m).c_str(), (std::string("Pixel > mm ") + m).c_str(), "",
                 "Scale, rotation and skew (set by the calibration)", 5);

        b.group = "Lights";
        b.oc("light_top_idle", "Top light (always on)", "%", 100, 0, 100, 0, "Goes dark by itself while the bottom light is on");
        b.oc("neo_count", "LEDs in the bottom ring", "", 24, 0, 256, 0, "NeoPixel ring on the Octopus RGB header");
        b.oc("neo_r", "Ring colour red", "", 255, 0, 255, 0, "");
        b.oc("neo_g", "Ring colour green", "", 255, 0, 255, 0, "");
        b.oc("neo_b", "Ring colour blue", "", 255, 0, 255, 0, "");

        b.group = "Homing";
        b.oc("home_xy", "X / Y homing", "", 2, 0, 2, 0, "Sensorless (DIAG jumpers 0 / 1 / 7, tmc_sg_x / _x2 / _y), or switches on DIAG0 / DIAG1 / DIAG7", "none|switch|sensorless");
        b.oc("home_speed", "Homing speed", "mm/s", 40, 0.1f, 500, 1, "First, fast approach");
        b.oc("home_slow", "Homing slow speed", "mm/s", 3, 0.1f, 100, 1, "Second, precise approach");
        b.oc("home_pulloff", "Pull-off", "mm", 3, 0, 50, 1, "Back off the switch after homing");
        b.oc("home_cur", "X homing current", "%", 60, 10, 100, 0, "Of the X motor currents, while seeking a stall");
        b.oc("home_blank", "Ignore stalls at the start", "mm", 2, 0, 50, 1, "Sensorless homing: while the motors accelerate");
        b.oc("home_skew_max", "Max X skew", "mm", 2, 0.1f, 50, 1, "Homing fails if one X side runs on this far alone");
        b.oc("x2_offset", "X2 trim", "mm", 0, -3, 3, 2, "Second X motor moves this far alone after homing, to square the gantry");
        b.oc("z_home", "Seesaw homing", "", 1, 0, 1, 0, "Half flag on DIAG2, or level by hand", "none|flag");
        b.oc("z_travel", "Seesaw travel", "mm", 40, 1, 500, 1, "Search range for the flag");
        b.oc("z_flag_speed", "Flag search speed", "mm/s", 4, 0.1f, 100, 1, "");
        b.oc("z_flag_slow", "Flag slow speed", "mm/s", 0.5f, 0.05f, 50, 2, "");
        b.oc("z_flag_backoff", "Flag back-off", "mm", 2, 0.1f, 50, 1, "");
        b.oc("e1_home", "Gantry homing", "", 2, 0, 2, 0, "Sensorless (DIAG jumper 6, tmc_sg_e1), or a switch on DIAG6", "none|switch|sensorless");
        b.oc("e1_home_dir", "Gantry homing direction", "", -1, -1, 1, 0, "-1 = towards E1 min, +1 = towards max");
        b.oc("e1_home_speed", "Gantry homing speed", "mm/s", 20, 0.1f, 500, 1, "");
        b.oc("e1_home_pulloff", "Gantry pull-off", "mm", 2, 0, 50, 1, "");

        b.group = "Machine size & jogging";
        b.oc("travel_x", "X travel", "mm", 300, 1, 5000, 0, "Moves beyond are refused");
        b.oc("travel_y", "Y travel", "mm", 300, 1, 5000, 0, "");
        b.oc("travel_e1", "Gantry travel", "mm", 500, 1, 5000, 0, "Also the length auto detect searches");
        b.oc("jog_speed", "Jog speed", "mm/s", 30, 0.1f, 1000, 1, "Default for JOG and the website");
        b.oc("rot_speed", "Nozzle rotation speed", "°/s", 180, 1, 3600, 0, "");
        b.oc("junction_dev", "Cornering", "mm", 0.02f, 0.001f, 1, 3, "Junction deviation: higher = faster corners");

        // Per axis (Octopus): steps, speed, acceleration, direction, driver
        static const char *const AX[] = {"x", "y", "z", "rot", "paste", "e0", "e1", "x2"};
        static const char *const AXG[] = {"Axis X", "Axis Y", "Axis Z (seesaw)", "Axis ROT (nozzle)", "Axis PASTE (extruder)",
                                          "Axis E0 (feeder drive)", "Axis E1 (feeder gantry)", "Axis X2 (second X motor)"};
        static const char *const UNIT[] = {"mm", "mm", "mm", "°", "mm", "°", "mm", "mm"};
        static const float STEPS[] = {80, 80, 400, 200 * 16 / 360.0f, 400, 200 * 16 / 360.0f, 80, 200 * 16 / 360.0f};
        static const float SPEED[] = {150, 150, 5, 360, 10, 360, 50, 360};
        static const float ACCEL[] = {500, 500, 100, 1440, 50, 1440, 500, 1440};
        for (int a = 0; a < 8; a++)
        {
            b.group = AXG[a];
            const std::string ax = AX[a], u = UNIT[a];
            const std::string perU = "steps/" + u, speedU = u + "/s", accU = u + "/s/s";
            const bool follows = ax == "x2";   // takes every X step: steps, speed, acceleration and microsteps are those of X
            if (!follows)
            {
                b.oc("steps_" + ax, "Steps per " + u, perU.c_str(), STEPS[a], 0.001f, 100000, 3, "Full steps x microsteps per unit");
                b.oc("speed_" + ax, "Top speed", speedU.c_str(), SPEED[a], 0.001f, 100000, 1, "");
                b.oc("accel_" + ax, "Acceleration", accU.c_str(), ACCEL[a], 0.001f, 1000000, 0, "");
            }
            b.oc("invert_" + ax, "Reverse direction", "", follows ? 1 : 0, 0, 1, 0, follows ? "Set so both X motors push the gantry the same way" : "", nullptr, true);
            if (!follows)
                b.oc("test_speed_" + ax, "Motor test speed", "°/s", 180, 1, 3600, 0, "Testing > Test motors (at the shaft)");
            b.oc("tmc_cur_" + ax, "Motor current", "mA", 600, 50, 2000, 0, "TMC2209 run current (RMS)");
            b.oc("tmc_hold_" + ax, "Hold current", "%", 50, 0, 100, 0, "Of the run current while standing");
            if (!follows)
                b.oc("tmc_ms_" + ax, "Microsteps", "", 16, 1, 256, 0, "1, 2, 4 … 256 (change steps per unit too)");
            b.oc("tmc_stealth_" + ax, "StealthChop (quiet)", "", 1, 0, 1, 0, "Off = SpreadCycle (more torque)", nullptr, true);
            b.oc("tmc_sg_" + ax, "StallGuard threshold", "", a == 0 || a == 1 || a == 6 || a == 7 ? 80 : 0, 0, 255, 0, "Sensorless homing sensitivity (higher = more sensitive)");
        }

        b.group = "Joystick";
        b.pi("joy_jog_speed", "Jog speed at full deflection", "mm/s", "Move / Teach / Cameras screens");
        b.pi("joy_deadzone", "Dead zone", "%", "Around the centre", 0);
        b.pi("joy_hold_ms", "Button hold time", "ms", "Held this long = back", 0);
        b.pi("joy_invert_x", "Reverse X", "", "");
        b.pi("joy_invert_y", "Reverse Y", "", "");
        b.pi("joy_swap", "Swap X and Y", "", "");
        for (const char *a : {"x", "y"})
            for (const char *p : {"min", "center", "max"})
                b.pi((std::string("joy_") + a + "_" + p).c_str(), (std::string("Raw ") + (char)toupper(a[0]) + " " + p).c_str(), "",
                     "From Calibration > Joystick", 0);

        b.group = "Display & sound";
        b.pi("beep", "Beep", "", "Display and job beeps");
        b.pi("backlight", "Display brightness", "%", "", 0);

        b.group = "Safety & inputs";
        b.oc("estop_input", "E-stop input", "", 5, -1, 7, 0, "DIAG input of the e-stop button, -1 = none");
        b.oc("watchdog_ms", "Link watchdog", "ms", 2000, 0, 60000, 0, "The Octopus stops when the Pi is silent this long while busy (0 = off)");
        static const char *const IN[] = {"X stall", "X2 stall", "Z flag", "Z max", "Feeder sensor", "E-stop", "Gantry stall", "Y stall"};
        for (int i = 0; i < 8; i++)
            b.oc("in_invert_" + std::to_string(i), std::string("Invert DIAG") + std::to_string(i) + " (" + IN[i] + ")", "",
                 i == 0 || i == 1 || i == 6 || i == 7 ? 1 : 0, 0, 1, 0, "Switch to GND = hit; invert for NC switches / StallGuard", nullptr, true);
        return b.list;
    }

    std::string fmt(float v, int dec)
    {
        char t[32];
        snprintf(t, sizeof(t), "%.*f", dec < 0 ? 0 : dec, v);
        return t;
    }
}

const std::vector<SettingMeta> &settingsCatalog()
{
    static const std::vector<SettingMeta> list = build();
    return list;
}

std::string settingValue(const SettingMeta &m)
{
    if (!m.machine)
    {
        const int i = settings.find(m.key.c_str());
        if (i < 0)
            return "";
        char v[64];
        settings.valueAt((uint8_t)i, v, sizeof(v));
        return v;
    }
    std::string v = octo.value(m.key, "");
    if (!v.empty())
        return v;
    // Not reported yet (or not changed in the simulator): the firmware default
    if (m.kind == "choice")
    {
        int idx = (int)m.def, n = 0;
        size_t p = 0;
        while (n < idx && (p = m.choices.find('|', p)) != std::string::npos)
            p++, n++;
        const size_t e = m.choices.find('|', p);
        return m.choices.substr(p, e == std::string::npos ? std::string::npos : e - p);
    }
    return fmt(m.def, m.decimals);
}
