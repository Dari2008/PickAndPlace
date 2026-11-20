#include "general_settings.h"
#include "storage.h"
#include "../mosfets.h"
#include "../../octo/octopus.h"

#include <cstddef>

GeneralSettings settings;

static const char PATH[] = "/settings.txt";

const char *axisKey(uint8_t axis) { return octoAxisName(axis); }

float mval(const char *key, float def) { return octo.valueNum(key, def); }

enum Type : uint8_t
{
    T_BOOL,
    T_U8,
    T_U16,
    T_FLOAT,
    T_PASTE, // none | motor
    T_TEXT,  // char[TEXT_SETTING_LEN]
};

struct Row
{
    const char *key;
    uint8_t type;
    float min, max;
    float def;
    uint16_t offset;
    const char *defText; // T_TEXT
};

#define OFS(m) ((uint16_t)offsetof(GeneralSettings, m))

static const Row ROWS[] = {
    // job
    {"beep", T_BOOL, 0, 1, 1, OFS(beep), nullptr},
    {"travel_speed", T_FLOAT, 0.1f, 100000, 100, OFS(travelSpeed), nullptr},
    {"head1_reach", T_FLOAT, 0, 200, 10, OFS(headReach[0]), nullptr},
    {"head2_reach", T_FLOAT, 0, 200, 10, OFS(headReach[1]), nullptr},
    {"pick_height", T_FLOAT, -50, 400, 2, OFS(pickHeight), nullptr},
    {"pick_ms", T_U16, 0, 60000, 150, OFS(pickMs), nullptr},
    {"place_ms", T_U16, 0, 60000, 100, OFS(placeMs), nullptr},
    {"paste_output", T_PASTE, 0, 1, PASTE_MOTOR, OFS(pasteOutput), nullptr},
    {"paste_retract", T_FLOAT, 0, 100, 0.2f, OFS(pasteRetract), nullptr},
    {"head1_offset_x", T_FLOAT, -200, 200, 0, OFS(head1Offset[0]), nullptr},
    {"head1_offset_y", T_FLOAT, -200, 200, 0, OFS(head1Offset[1]), nullptr},
    {"reject_x", T_FLOAT, -1, 1000, -1, OFS(rejectX), nullptr},
    {"reject_y", T_FLOAT, -1, 1000, -1, OFS(rejectY), nullptr},
    // joystick: ADS1115 counts at 4.096 V full scale, 3.3 V supply -> 0 .. ~26400
    {"joy_x_min", T_FLOAT, 0, 32767, 0, OFS(joyMin[0]), nullptr},
    {"joy_x_center", T_FLOAT, 0, 32767, 13200, OFS(joyCenter[0]), nullptr},
    {"joy_x_max", T_FLOAT, 0, 32767, 26400, OFS(joyMax[0]), nullptr},
    {"joy_y_min", T_FLOAT, 0, 32767, 0, OFS(joyMin[1]), nullptr},
    {"joy_y_center", T_FLOAT, 0, 32767, 13200, OFS(joyCenter[1]), nullptr},
    {"joy_y_max", T_FLOAT, 0, 32767, 26400, OFS(joyMax[1]), nullptr},
    {"joy_deadzone", T_U8, 0, 60, 8, OFS(joyDeadzone), nullptr},
    {"joy_invert_x", T_BOOL, 0, 1, 0, OFS(joyInvert[0]), nullptr},
    {"joy_invert_y", T_BOOL, 0, 1, 0, OFS(joyInvert[1]), nullptr},
    {"joy_swap", T_BOOL, 0, 1, 0, OFS(joySwap), nullptr},
    {"joy_jog_invert_x", T_BOOL, 0, 1, 0, OFS(joyJogInvert[0]), nullptr},
    {"joy_jog_invert_y", T_BOOL, 0, 1, 0, OFS(joyJogInvert[1]), nullptr},
    {"joy_jog_speed", T_FLOAT, 0.1f, 500, 50, OFS(joyJogSpeed), nullptr},
    {"joy_hold_ms", T_U16, 200, 5000, 800, OFS(joyHoldMs), nullptr},
    // vacuum
    {"vac_baseline", T_FLOAT, 300, 1200, 1013.25f, OFS(vacBaseline), nullptr},
    {"vac_pick_min", T_FLOAT, 1, 900, 150, OFS(vacPickMin), nullptr},
    {"vac_check", T_BOOL, 0, 1, 1, OFS(vacCheck), nullptr},
    {"vac_retries", T_U8, 0, 5, 1, OFS(vacRetries), nullptr},
    {"vac_wait_ms", T_U16, 0, 5000, 150, OFS(vacWaitMs), nullptr},
    // cameras (0 = bottom, 1 = top)
    {"cam_bottom_dev", T_TEXT, 0, 0, 0, OFS(cam[0].dev), "/dev/video0"},
    {"cam_bottom_w", T_U16, 160, 4096, 1280, OFS(cam[0].w), nullptr},
    {"cam_bottom_h", T_U16, 120, 4096, 720, OFS(cam[0].h), nullptr},
    {"cam_bottom_exposure", T_FLOAT, -1, 10000, -1, OFS(cam[0].exposure), nullptr},
    {"cam_bottom_thresh", T_FLOAT, -1, 255, -1, OFS(cam[0].thresh), nullptr},
    {"cam_bottom_m00", T_FLOAT, -10, 10, 0.02f, OFS(cam[0].m[0]), nullptr},
    {"cam_bottom_m01", T_FLOAT, -10, 10, 0, OFS(cam[0].m[1]), nullptr},
    {"cam_bottom_m10", T_FLOAT, -10, 10, 0, OFS(cam[0].m[2]), nullptr},
    {"cam_bottom_m11", T_FLOAT, -10, 10, 0.02f, OFS(cam[0].m[3]), nullptr},
    {"cam_top_dev", T_TEXT, 0, 0, 0, OFS(cam[1].dev), "/dev/video2"},
    {"cam_top_w", T_U16, 160, 4096, 1280, OFS(cam[1].w), nullptr},
    {"cam_top_h", T_U16, 120, 4096, 720, OFS(cam[1].h), nullptr},
    {"cam_top_exposure", T_FLOAT, -1, 10000, -1, OFS(cam[1].exposure), nullptr},
    {"cam_top_thresh", T_FLOAT, -1, 255, -1, OFS(cam[1].thresh), nullptr},
    {"cam_top_m00", T_FLOAT, -10, 10, 0.02f, OFS(cam[1].m[0]), nullptr},
    {"cam_top_m01", T_FLOAT, -10, 10, 0, OFS(cam[1].m[1]), nullptr},
    {"cam_top_m10", T_FLOAT, -10, 10, 0, OFS(cam[1].m[2]), nullptr},
    {"cam_top_m11", T_FLOAT, -10, 10, 0.02f, OFS(cam[1].m[3]), nullptr},
    {"cam_bottom_cx", T_FLOAT, -1, 8192, -1, OFS(camBottomCenter[0]), nullptr},
    {"cam_bottom_cy", T_FLOAT, -1, 8192, -1, OFS(camBottomCenter[1]), nullptr},
    {"cam_bottom_x", T_FLOAT, -1000, 2000, 0, OFS(camBottomPos[0]), nullptr},
    {"cam_bottom_y", T_FLOAT, -1000, 2000, 0, OFS(camBottomPos[1]), nullptr},
    {"cam_bottom_z", T_FLOAT, -200, 200, 0, OFS(camBottomPos[2]), nullptr},
    {"cam_top_off_x", T_FLOAT, -500, 500, 0, OFS(camTopOffset[0]), nullptr},
    {"cam_top_off_y", T_FLOAT, -500, 500, 0, OFS(camTopOffset[1]), nullptr},
    {"cam_bottom_invert", T_BOOL, 0, 1, 0, OFS(camBottomInvert), nullptr},
    {"cam_bottom_rot_sign", T_FLOAT, -1, 1, 1, OFS(camBottomRotSign), nullptr},
    {"cam_settle_ms", T_U16, 0, 5000, 150, OFS(camSettleMs), nullptr},
    // vision
    {"vision_parts", T_BOOL, 0, 1, 1, OFS(visionParts), nullptr},
    {"vision_fiducials", T_BOOL, 0, 1, 0, OFS(visionFiducials), nullptr},
    {"vision_pockets", T_BOOL, 0, 1, 0, OFS(visionPockets), nullptr},
    {"vision_place_check", T_BOOL, 0, 1, 0, OFS(visionPlaceCheck), nullptr},
    {"vision_required", T_BOOL, 0, 1, 0, OFS(visionRequired), nullptr},
    {"vision_max_offset", T_FLOAT, 0.01f, 50, 2, OFS(visionMaxOffset), nullptr},
    {"vision_max_angle", T_FLOAT, 0.1f, 90, 20, OFS(visionMaxAngle), nullptr},
    {"fid_diameter", T_FLOAT, 0.1f, 20, 1.0f, OFS(fidDiameter), nullptr},
    {"fid_search", T_FLOAT, 0.2f, 50, 3.0f, OFS(fidSearch), nullptr},
    {"pocket_search", T_FLOAT, 0.2f, 50, 2.0f, OFS(pocketSearch), nullptr},
    {"light_top", T_U8, 0, 100, 100, OFS(lightTop), nullptr},
    {"light_bottom", T_U8, 0, 100, 100, OFS(lightBottom), nullptr},
    {"cal_step", T_FLOAT, 0.1f, 50, 3, OFS(calStep), nullptr},
    {"backlight", T_U8, 5, 100, 100, OFS(backlight), nullptr},
    {"feeder_wheel_teeth", T_U8, 1, 200, 20, OFS(feederWheelTeeth), nullptr},
    {"feeder_qr", T_BOOL, 0, 1, 1, OFS(feederQr), nullptr},
    {"feeder_qr_x0", T_FLOAT, -1000, 2000, 0, OFS(feederQr0[0]), nullptr},
    {"feeder_qr_y0", T_FLOAT, -1000, 2000, 0, OFS(feederQr0[1]), nullptr},
    {"feeder_qr_dx", T_FLOAT, -10, 10, 1, OFS(feederQrDir[0]), nullptr},
    {"feeder_qr_dy", T_FLOAT, -10, 10, 0, OFS(feederQrDir[1]), nullptr},
    // nozzle changer
    {"nozzle_count", T_U8, 0, 8, 5, OFS(nozzleCount), nullptr},
    {"nozzle_first_x", T_FLOAT, -1000, 2000, 0, OFS(nozzleFirst[0]), nullptr},
    {"nozzle_first_y", T_FLOAT, -1000, 2000, 0, OFS(nozzleFirst[1]), nullptr},
    {"nozzle_last_x", T_FLOAT, -1000, 2000, 0, OFS(nozzleLast[0]), nullptr},
    {"nozzle_last_y", T_FLOAT, -1000, 2000, 0, OFS(nozzleLast[1]), nullptr},
    {"nozzle_z_pick", T_FLOAT, -50, 50, -8, OFS(nozzleZPick), nullptr},
    {"nozzle_z_drop", T_FLOAT, -50, 50, -8, OFS(nozzleZDrop), nullptr},
    {"nozzle_z_safe", T_FLOAT, -50, 50, 0, OFS(nozzleSafeZ), nullptr},
    {"nozzle_slide_x", T_FLOAT, -50, 50, 0, OFS(nozzleSlide[0]), nullptr},
    {"nozzle_slide_y", T_FLOAT, -50, 50, 0, OFS(nozzleSlide[1]), nullptr},
    {"nozzle_speed", T_FLOAT, 0.5f, 100, 5, OFS(nozzleSpeed), nullptr},
    {"nozzle_wait_ms", T_U16, 0, 5000, 200, OFS(nozzleWaitMs), nullptr},
    {"nozzle_sensor_invert", T_BOOL, 0, 1, 0, OFS(nozzleSensorInvert), nullptr},
    {"nozzle_bit_1", T_U8, 0, 7, 0, OFS(nozzleBit[0]), nullptr},
    {"nozzle_bit_2", T_U8, 0, 7, 1, OFS(nozzleBit[1]), nullptr},
    {"nozzle_bit_3", T_U8, 0, 7, 2, OFS(nozzleBit[2]), nullptr},
    {"nozzle_bit_4", T_U8, 0, 7, 3, OFS(nozzleBit[3]), nullptr},
    {"nozzle_bit_5", T_U8, 0, 7, 4, OFS(nozzleBit[4]), nullptr},
    {"nozzle_bit_6", T_U8, 0, 7, 5, OFS(nozzleBit[5]), nullptr},
    {"nozzle_bit_7", T_U8, 0, 7, 6, OFS(nozzleBit[6]), nullptr},
    {"nozzle_bit_8", T_U8, 0, 7, 7, OFS(nozzleBit[7]), nullptr},
    {"nozzle_check", T_BOOL, 0, 1, 1, OFS(nozzleCheck), nullptr},
    {"nozzle_calibrate", T_BOOL, 0, 1, 1, OFS(nozzleCalibrate), nullptr},
    {"nozzle_touchoff", T_BOOL, 0, 1, 1, OFS(nozzleTouchoff), nullptr},
    // load cell
    {"loadcell", T_BOOL, 0, 1, 0, OFS(loadcell), nullptr},
    {"loadcell_x", T_FLOAT, -1000, 2000, 0, OFS(loadcellPos[0]), nullptr},
    {"loadcell_y", T_FLOAT, -1000, 2000, 0, OFS(loadcellPos[1]), nullptr},
    {"loadcell_height", T_FLOAT, -50, 200, 0, OFS(loadcellHeight), nullptr},
    {"loadcell_speed", T_FLOAT, 0.1f, 50, 3, OFS(loadcellSpeed), nullptr},
    {"loadcell_slow", T_FLOAT, 0.02f, 10, 0.3f, OFS(loadcellSlow), nullptr},
    {"loadcell_backoff", T_FLOAT, 0.1f, 10, 0.5f, OFS(loadcellBackoff), nullptr},
    {"loadcell_depth", T_FLOAT, 0.5f, 30, 4, OFS(loadcellDepth), nullptr},
};
static const uint8_t ROW_COUNT = sizeof(ROWS) / sizeof(ROWS[0]);

static void *memberPtr(const GeneralSettings *s, const Row &row) { return (uint8_t *)s + row.offset; }

uint8_t GeneralSettings::count() const { return ROW_COUNT; }

void GeneralSettings::keyAt(uint8_t i, char *key) const { strcpy(key, i < ROW_COUNT ? ROWS[i].key : ""); }

void GeneralSettings::metaAt(uint8_t i, char &type, float &min, float &max, float &def, bool &integer) const
{
    const Row &r = ROWS[i < ROW_COUNT ? i : 0];
    type = r.type == T_BOOL ? 'b' : r.type == T_PASTE ? 'c' : r.type == T_TEXT ? 't' : 'n';
    integer = r.type == T_U8 || r.type == T_U16;
    min = r.min, max = r.max, def = r.def;
}

int GeneralSettings::find(const char *key) const
{
    for (uint8_t i = 0; i < ROW_COUNT; i++)
        if (!strcasecmp(ROWS[i].key, key))
            return i;
    return -1;
}

static void formatFloat(float v, char *out, uint8_t size)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%.4f", (double)v);
    char *dot = strchr(buf, '.');
    if (dot)
    {
        char *end = buf + strlen(buf) - 1;
        while (end > dot && *end == '0')
            *end-- = '\0';
        if (end == dot)
            *end = '\0';
    }
    strncpy(out, buf, size - 1);
    out[size - 1] = '\0';
}

void GeneralSettings::valueAt(uint8_t i, char *out, uint8_t size) const
{
    if (i >= ROW_COUNT)
    {
        out[0] = '\0';
        return;
    }
    const Row &row = ROWS[i];
    const void *p = memberPtr(this, row);
    switch (row.type)
    {
    case T_BOOL:
    case T_U8:
        snprintf(out, size, "%u", *(const uint8_t *)p);
        break;
    case T_U16:
        snprintf(out, size, "%u", *(const uint16_t *)p);
        break;
    case T_FLOAT:
        formatFloat(*(const float *)p, out, size);
        break;
    case T_PASTE:
    {
        static const char *const NAMES[] = {"none", "motor"};
        const uint8_t v = *(const uint8_t *)p;
        strncpy(out, v < 2 ? NAMES[v] : "motor", size);
        out[size - 1] = '\0';
        break;
    }
    case T_TEXT:
        strncpy(out, (const char *)p, size);
        out[size - 1] = '\0';
        break;
    }
}

static bool parseNumber(const char *s, float &v)
{
    char *end;
    v = strtod(s, &end);
    while (*end == ' ')
        end++;
    return end != s && *end == '\0';
}

PGM_P GeneralSettings::setValue(uint8_t i, const char *value)
{
    if (i >= ROW_COUNT)
        return "unknown setting";
    const Row &row = ROWS[i];
    void *p = memberPtr(this, row);
    float v;

    if (row.type == T_TEXT)
    {
        if (strlen(value) >= TEXT_SETTING_LEN)
            return "text too long";
        strcpy((char *)p, value);
        return nullptr;
    }
    if (row.type == T_PASTE)
    {
        static const char *const NAMES[] = {"none", "motor"};
        for (uint8_t k = 0; k < 2; k++)
            if (!strcasecmp(value, NAMES[k]))
            {
                *(uint8_t *)p = k;
                return nullptr;
            }
        return "use motor or none";
    }
    if (row.type == T_BOOL && (!strcasecmp(value, "on") || !strcasecmp(value, "true")))
        v = 1.0f;
    else if (row.type == T_BOOL && (!strcasecmp(value, "off") || !strcasecmp(value, "false")))
        v = 0.0f;
    else if (!parseNumber(value, v))
        return "not a number";
    if (v < row.min || v > row.max)
        return "out of range";
    switch (row.type)
    {
    case T_BOOL:
        *(uint8_t *)p = v != 0.0f;
        break;
    case T_U8:
        *(uint8_t *)p = (uint8_t)(v + 0.5f);
        break;
    case T_U16:
        *(uint16_t *)p = (uint16_t)(v + 0.5f);
        break;
    default:
        *(float *)p = v;
        break;
    }
    return nullptr;
}

void GeneralSettings::setDefaults()
{
    char text[24];
    for (uint8_t i = 0; i < ROW_COUNT; i++)
    {
        const Row &row = ROWS[i];
        if (row.type == T_TEXT)
            setValue(i, row.defText);
        else if (row.type == T_PASTE)
            *(uint8_t *)memberPtr(this, row) = (uint8_t)row.def;
        else
        {
            formatFloat(row.def, text, sizeof(text));
            setValue(i, text);
        }
    }
}

static void onKey(const char *key, const char *value, void *)
{
    const int i = settings.find(key);
    if (i >= 0)
        settings.setValue((uint8_t)i, value); // bad values keep the default
}

bool GeneralSettings::load() { return storage.readKv(PATH, onKey, nullptr); }

bool GeneralSettings::save()
{
    File f;
    if (!storage.openWrite(f, PATH))
        return false;
    f.print(F("# Pick & place settings (Pi) - see pi/README.md; machine values: machine.txt\n"));
    char key[40], value[80];
    for (uint8_t i = 0; i < ROW_COUNT; i++)
    {
        keyAt(i, key);
        valueAt(i, value, sizeof(value));
        f.print(key);
        f.print(F(" = "));
        f.println(value);
    }
    return f.close();
}


