#include "settings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *const AXIS_NAME[N_AXIS] = {"x", "y", "z", "rot", "paste", "e0", "e1", "x2"};
const char *const INPUT_NAME[N_INPUTS] = {"x_min", "x2_min", "z_flag", "z_max", "feeder", "estop", "e1_home", "y_min"};
const char *const OUTPUT_NAME[N_OUTPUTS] = {"pump", "valve", "light_bottom", "light_top", "cam_fan", "box_fan", "pi_fan"};

int axisByName(const char *name)
{
    for (int a = 0; a < N_AXIS; a++)
        if (!strcasecmp(name, AXIS_NAME[a]))
            return a;
    return -1;
}

namespace cfg
{
    namespace
    {
        struct Def
        {
            const char *name;
            float def, min, max;
            const char *words; // "none|flag" -> 0, 1 ; nullptr = numbers only
        };

        const Def AXIS_DEFS[N_AXIS_KEYS] = {
            {"steps_", 80, 0.001f, 100000, nullptr},
            {"speed_", 100, 0.001f, 100000, nullptr},
            {"accel_", 500, 0.001f, 1000000, nullptr},
            {"invert_", 0, 0, 1, nullptr},
            {"test_speed_", 180, 1, 3600, nullptr},
            {"tmc_cur_", 600, 50, 2000, nullptr},
            {"tmc_hold_", 50, 0, 100, nullptr},
            {"tmc_ms_", 16, 1, 256, nullptr},
            {"tmc_stealth_", 1, 0, 1, nullptr},
            {"tmc_sg_", 0, 0, 255, nullptr},
        };
        // Axis-specific defaults (steps, speed, accel); the rest use AXIS_DEFS.def
        const float AXIS_STEPS[N_AXIS] = {80, 80, 400, 200 * 16 / 360.0f, 400, 200 * 16 / 360.0f, 80, 80};
        const float AXIS_SPEED[N_AXIS] = {150, 150, 5, 360, 10, 360, 50, 150};
        const float AXIS_ACCEL[N_AXIS] = {500, 500, 100, 1440, 50, 1440, 500, 500};

        const Def KEY_DEFS[N_KEYS] = {
            {"travel_x", 300, 1, 5000, nullptr},
            {"travel_y", 300, 1, 5000, nullptr},
            {"travel_e1", 500, 1, 5000, nullptr},
            {"home_xy", 2, 0, 2, "none|switch|sensorless"}, // the machine homes X / Y sensorless (DIAG jumpers 0 / 1 / 7)
            {"home_speed", 40, 0.1f, 500, nullptr},
            {"home_slow", 3, 0.1f, 100, nullptr},
            {"home_pulloff", 3, 0, 50, nullptr},
            {"home_cur", 60, 10, 100, nullptr},
            {"home_blank", 2, 0, 50, nullptr},
            {"home_skew_max", 2, 0.1f, 50, nullptr},
            {"x2_offset", 0, -3, 3, nullptr},
            {"z_home", 1, 0, 1, "none|flag"},
            {"z_travel", 40, 1, 500, nullptr},
            {"z_flag_speed", 4, 0.1f, 100, nullptr},
            {"z_flag_slow", 0.5f, 0.05f, 50, nullptr},
            {"z_flag_backoff", 2, 0.1f, 50, nullptr},
            {"e1_home", 2, 0, 2, "none|switch|sensorless"},
            {"e1_home_dir", -1, -1, 1, nullptr},
            {"e1_home_speed", 20, 0.1f, 500, nullptr},
            {"e1_home_pulloff", 2, 0, 50, nullptr},
            {"estop_input", 5, -1, 7, nullptr},
            {"watchdog_ms", 2000, 0, 60000, nullptr},
            {"pump_power", 100, 0, 100, nullptr},
            {"syringe_d", 10, 0.1f, 100, nullptr},
            {"paste_speed", 1, 0.001f, 1000, nullptr},
            {"rot_speed", 180, 1, 3600, nullptr},
            {"e0_speed", 180, 1, 3600, nullptr},
            {"e1_speed", 30, 0.1f, 1000, nullptr},
            {"jog_speed", 30, 0.1f, 1000, nullptr},
            {"junction_dev", 0.02f, 0.001f, 1, nullptr},
            {"in_invert_0", 1, 0, 1, nullptr}, // StallGuard DIAG (X) is HIGH on a stall
            {"in_invert_1", 1, 0, 1, nullptr}, // StallGuard DIAG (X2)
            {"in_invert_2", 0, 0, 1, nullptr},
            {"in_invert_3", 0, 0, 1, nullptr},
            {"in_invert_4", 0, 0, 1, nullptr},
            {"in_invert_5", 0, 0, 1, nullptr},
            {"in_invert_6", 1, 0, 1, nullptr}, // StallGuard DIAG is HIGH on a stall
            {"in_invert_7", 1, 0, 1, nullptr}, // StallGuard DIAG (Y)
            {"light_top_idle", 100, 0, 100, nullptr},
            {"cam_fan", 50, 0, 100, nullptr},  // head camera fan, always on
            {"box_fan", 100, 0, 100, nullptr}, // housing fan, always on
            {"pi_fan", 100, 0, 100, nullptr},  // Raspberry Pi housing fan, always on
            {"neo_count", 24, 0, 256, nullptr},
            {"neo_r", 255, 0, 255, nullptr},
            {"neo_g", 255, 0, 255, nullptr},
            {"neo_b", 255, 0, 255, nullptr},
            {"lc_scale", 420, -100000, 100000, nullptr}, // ~ a 1 kg cell (1 mV/V) at gain 128, 3.3 V
            {"lc_trigger", 30, 1, 5000, nullptr},
            {"pump_used", 1, 0, 1, nullptr}, // 0 = no pump connected: PUMP keeps FAN5 off
        };

        // Layout: [axis keys: N_AXIS_KEYS x N_AXIS][global keys]
        constexpr size_t N_AXIS_VALUES = (size_t)N_AXIS_KEYS * N_AXIS;
        constexpr size_t N_TOTAL = N_AXIS_VALUES + N_KEYS;
        float values[N_TOTAL];
        char names[N_AXIS_VALUES][24];

        float axisDefault(uint8_t k, uint8_t a)
        {
            switch (k)
            {
            case STEPS: return AXIS_STEPS[a];
            case SPEED: return AXIS_SPEED[a];
            case ACCEL: return AXIS_ACCEL[a];
            case INVERT: return a == X2_AXIS ? 1 : 0; // the two X motors face each other: X2 turns the other way
            case TMC_SG: return a == X_AXIS || a == X2_AXIS || a == Y_AXIS || a == E1_AXIS ? 80 : 0; // these home sensorless
            default: return AXIS_DEFS[k].def;
            }
        }

        const Def &defOf(size_t i)
        {
            return i < N_AXIS_VALUES ? AXIS_DEFS[i / N_AXIS] : KEY_DEFS[i - N_AXIS_VALUES];
        }

        bool parseWord(const char *words, const char *v, float &out)
        {
            if (!words)
                return false;
            int idx = 0;
            const char *p = words;
            const size_t len = strlen(v);
            while (*p)
            {
                const char *bar = strchr(p, '|');
                const size_t n = bar ? (size_t)(bar - p) : strlen(p);
                if (n == len && !strncasecmp(p, v, n))
                {
                    out = (float)idx;
                    return true;
                }
                if (!bar)
                    break;
                p = bar + 1;
                idx++;
            }
            return false;
        }

        std::string wordOf(const char *words, int idx)
        {
            const char *p = words;
            for (int i = 0; p && *p; i++)
            {
                const char *bar = strchr(p, '|');
                const size_t n = bar ? (size_t)(bar - p) : strlen(p);
                if (i == idx)
                    return std::string(p, n);
                if (!bar)
                    break;
                p = bar + 1;
            }
            return std::to_string(idx);
        }
    }

    void init()
    {
        for (uint8_t k = 0; k < N_AXIS_KEYS; k++)
            for (uint8_t a = 0; a < N_AXIS; a++)
            {
                const size_t i = (size_t)k * N_AXIS + a;
                values[i] = axisDefault(k, a);
                snprintf(names[i], sizeof(names[i]), "%s%s", AXIS_DEFS[k].name, AXIS_NAME[a]);
            }
        for (uint8_t k = 0; k < N_KEYS; k++)
            values[N_AXIS_VALUES + k] = KEY_DEFS[k].def;
    }

    float get(Key k) { return values[N_AXIS_VALUES + k]; }
    float axis(AxisKey k, uint8_t a) { return values[(size_t)k * N_AXIS + a]; }

    size_t count() { return N_TOTAL; }
    const char *name(size_t i) { return i < N_AXIS_VALUES ? names[i] : KEY_DEFS[i - N_AXIS_VALUES].name; }

    bool isAxisKey(size_t i, AxisKey &k, uint8_t &a)
    {
        if (i >= N_AXIS_VALUES)
            return false;
        k = (AxisKey)(i / N_AXIS);
        a = (uint8_t)(i % N_AXIS);
        return true;
    }

    int find(const char *key)
    {
        for (size_t i = 0; i < N_TOTAL; i++)
            if (!strcasecmp(key, name(i)))
                return (int)i;
        return -1;
    }

    std::string text(size_t i)
    {
        const Def &d = defOf(i);
        const float v = values[i];
        if (d.words)
            return wordOf(d.words, (int)lroundf(v));
        char buf[24];
        snprintf(buf, sizeof(buf), "%g", (double)v);
        return buf;
    }

    bool set(const char *key, const char *value, std::string &err, int *changed)
    {
        const int i = find(key);
        if (i < 0)
        {
            err = std::string("unknown key ") + key;
            return false;
        }
        const Def &d = defOf((size_t)i);
        float v;
        if (!parseWord(d.words, value, v))
        {
            char *end = nullptr;
            v = strtof(value, &end);
            if (end == value || *end != '\0')
            {
                err = std::string("bad value for ") + key;
                return false;
            }
        }
        if (v < d.min || v > d.max)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s must be %g..%g", name((size_t)i), (double)d.min, (double)d.max);
            err = buf;
            return false;
        }
        values[i] = v;
        if (changed)
            *changed = i;
        return true;
    }
}
