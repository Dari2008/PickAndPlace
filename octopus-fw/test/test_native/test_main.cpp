// Unit tests on the PC: pio test -e native
#include "motion/homing_gate.h"
#include "toolbus.h"
#include <unity.h>

#include <string.h>
#include <string>

#include "proto/args.h"
#include "proto/out.h"
#include "settings.h"
#include "tmc_frame.h"

void setUp() { cfg::init(); }
void tearDown() {}

// ---- Args ---------------------------------------------------------------------------
static void test_args_words_and_kv()
{
    char line[] = "MOVE x=10.5 y=-3 speed=50 # comment";
    Args a(line);
    TEST_ASSERT_EQUAL(4, a.count());
    TEST_ASSERT_TRUE(a.is(0, "move"));
    float v = 0;
    bool bad = false;
    TEST_ASSERT_TRUE(a.kv("x", v, bad));
    TEST_ASSERT_EQUAL_FLOAT(10.5f, v);
    TEST_ASSERT_TRUE(a.kv("Y", v, bad));
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, v);
    TEST_ASSERT_FALSE(a.kv("z", v, bad));
    TEST_ASSERT_FALSE(bad);
}

static void test_args_quotes_and_bad_number()
{
    char line[] = "SET name=\"a b\" x=abc \"two words\"";
    Args a(line);
    TEST_ASSERT_EQUAL(4, a.count());
    TEST_ASSERT_EQUAL_STRING("a b", a.value("name"));
    TEST_ASSERT_EQUAL_STRING("two words", a[3]);
    float v;
    bool bad = false;
    TEST_ASSERT_TRUE(a.kv("x", v, bad));
    TEST_ASSERT_TRUE(bad);
}

static void test_args_empty_and_comment()
{
    char l1[] = "   ";
    TEST_ASSERT_EQUAL(0, Args(l1).count());
    char l2[] = "# only a comment";
    TEST_ASSERT_EQUAL(0, Args(l2).count());
}

// ---- Line / Sink --------------------------------------------------------------------------
struct StrSink : Sink
{
    std::string s;
    void write(const char *d, size_t n) override { s.append(d, n); }
};

static void test_line_quoting()
{
    Line l("status");
    l.str("state", "idle").num("busy", 0).flt("x", 1.25f, 2).str("text", "link \"lost\"");
    TEST_ASSERT_EQUAL_STRING("status state=idle busy=0 x=1.25 text=\"link 'lost'\"", l.s.c_str());
    StrSink s;
    s.line(l.s);
    s.ok();
    s.error("bad");
    TEST_ASSERT_EQUAL_STRING("status state=idle busy=0 x=1.25 text=\"link 'lost'\"\nok\nerror bad\n", s.s.c_str());
}

// ---- settings -------------------------------------------------------------------------------
static void test_settings_defaults_and_set()
{
    TEST_ASSERT_EQUAL_FLOAT(80.0f, cfg::axis(cfg::STEPS, X_AXIS));
    TEST_ASSERT_EQUAL_FLOAT(80.0f, cfg::axis(cfg::TMC_SG, E1_AXIS));
    std::string err;
    int changed = -1;
    TEST_ASSERT_TRUE(cfg::set("steps_x", "100", err, &changed));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, cfg::axis(cfg::STEPS, X_AXIS));
    TEST_ASSERT_EQUAL_STRING("steps_x", cfg::name((size_t)changed));
    TEST_ASSERT_TRUE(cfg::set("TMC_CUR_E1", "750", err));
    TEST_ASSERT_EQUAL_FLOAT(750.0f, cfg::axis(cfg::TMC_CUR, E1_AXIS));
}

static void test_settings_words_and_ranges()
{
    std::string err;
    TEST_ASSERT_TRUE(cfg::set("z_home", "flag", err));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, cfg::get(cfg::Z_HOME));
    TEST_ASSERT_EQUAL_STRING("flag", cfg::text((size_t)cfg::find("z_home")).c_str());
    TEST_ASSERT_TRUE(cfg::set("e1_home", "sensorless", err));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, cfg::get(cfg::E1_HOME));
    TEST_ASSERT_FALSE(cfg::set("tmc_cur_x", "5000", err));
    TEST_ASSERT_TRUE(err.find("tmc_cur_x") != std::string::npos);
    TEST_ASSERT_FALSE(cfg::set("nope", "1", err));
    TEST_ASSERT_FALSE(cfg::set("steps_y", "12abc", err));
    TEST_ASSERT_EQUAL_FLOAT(80.0f, cfg::axis(cfg::STEPS, Y_AXIS)); // unchanged
}

static void test_settings_pump_used()
{
    // pump_used: 1 = a pump is connected (default), 0 = none: PUMP keeps the output off
    TEST_ASSERT_EQUAL_FLOAT(1.0f, cfg::get(cfg::PUMP_USED));
    std::string err;
    TEST_ASSERT_TRUE(cfg::set("pump_used", "0", err));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cfg::get(cfg::PUMP_USED));
    TEST_ASSERT_FALSE(cfg::set("pump_used", "2", err));
    TEST_ASSERT_TRUE(cfg::set("pump_used", "1", err));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, cfg::get(cfg::PUMP_USED));
}

static void test_settings_names_unique()
{
    for (size_t i = 0; i < cfg::count(); i++)
        for (size_t j = i + 1; j < cfg::count(); j++)
            TEST_ASSERT_TRUE_MESSAGE(strcmp(cfg::name(i), cfg::name(j)) != 0, cfg::name(i));
}

// ---- second X motor and homing gate --------------------------------------------------------------
static uint8_t gateHits; // bit n = input n reads "hit"
static bool gateHit(uint8_t n) { return gateHits & (1u << n); }

static void gateBegin(HomingGate &g, uint32_t blank, uint32_t skew)
{
    int8_t in[N_AXIS];
    memset(in, -1, sizeof(in));
    in[X_AXIS] = IN_X_MIN;
    in[X2_AXIS] = IN_X2_MIN;
    gateHits = 0;
    g.begin(X_PAIR, in, false, blank, skew);
}

static void test_x2_mirrors_x()
{
    TEST_ASSERT_EQUAL_HEX8(X_PAIR, mirrorX((uint8_t)bit(X_AXIS)));
    TEST_ASSERT_EQUAL_HEX8(bit(Y_AXIS), mirrorX((uint8_t)bit(Y_AXIS)));
    TEST_ASSERT_EQUAL_HEX8(0, mirrorX((uint8_t)bit(X2_AXIS))); // X2 never moves on its own
    TEST_ASSERT_EQUAL_HEX8(X_PAIR | bit(Z_AXIS), mirrorX((uint8_t)(bit(X_AXIS) | bit(Z_AXIS))));
    TEST_ASSERT_EQUAL(N_AXIS - 1, X2_AXIS); // the loops over N_MOVE_AXES rely on it
    TEST_ASSERT_EQUAL(X2_AXIS, N_MOVE_AXES);
    TEST_ASSERT_EQUAL_STRING("x2_min", INPUT_NAME[IN_X2_MIN]); // reported under the right name
    TEST_ASSERT_EQUAL_STRING("y_min", INPUT_NAME[IN_Y_MIN]);
    TEST_ASSERT_EQUAL(1, IN_X2_MIN); // beside X on the board
}

static void test_gate_passes_steps_when_inactive()
{
    HomingGate g;
    TEST_ASSERT_EQUAL_HEX8(X_PAIR, g.filter(X_PAIR, gateHit));
}

static void test_gate_each_side_stops_alone()
{
    HomingGate g;
    gateBegin(g, 0, 100);
    TEST_ASSERT_EQUAL_HEX8(X_PAIR, g.filter(X_PAIR, gateHit));
    gateHits = 1u << IN_X_MIN; // X side reached its stop: X2 runs on, X is frozen on that step
    TEST_ASSERT_EQUAL_HEX8(bit(X2_AXIS), g.filter(X_PAIR, gateHit));
    TEST_ASSERT_EQUAL_HEX8(bit(X2_AXIS), g.filter(X_PAIR, gateHit));
    TEST_ASSERT_EQUAL_HEX8(bit(X2_AXIS), g.lock);
    gateHits = 1u << IN_X2_MIN; // X's DIAG may drop again while it holds: X stays stopped
    TEST_ASSERT_EQUAL_HEX8(0, g.filter(X_PAIR, gateHit));
    TEST_ASSERT_EQUAL_HEX8(0, g.lock);
    TEST_ASSERT_EQUAL(2, g.skew);
    TEST_ASSERT_FALSE(g.skewFailed);
    TEST_ASSERT_EQUAL_HEX8(0, g.filter(X_PAIR, gateHit)); // no stray step before the flush
}

static void test_gate_x2_can_stop_first()
{
    HomingGate g;
    gateBegin(g, 0, 100);
    gateHits = 1u << IN_X2_MIN;
    TEST_ASSERT_EQUAL_HEX8(bit(X_AXIS), g.filter(X_PAIR, gateHit));
}

static void test_gate_blank_ignores_early_stalls()
{
    HomingGate g;
    gateBegin(g, 3, 100);
    gateHits = (1u << IN_X_MIN) | (1u << IN_X2_MIN); // both DIAGs fire while accelerating
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL_HEX8(X_PAIR, g.filter(X_PAIR, gateHit));
    g.filter(0, gateHit);                             // tick without a step: does not use up the blank
    TEST_ASSERT_EQUAL_HEX8(0, g.filter(X_PAIR, gateHit)); // blank over: both stop
    TEST_ASSERT_EQUAL_HEX8(0, g.lock);
}

static void test_gate_skew_limit_stops_both()
{
    HomingGate g;
    gateBegin(g, 0, 3);
    gateHits = 1u << IN_X_MIN;
    uint8_t out = 0;
    int stepsAlone = 0;
    for (int i = 0; i < 20; i++)
    {
        out = g.filter(X_PAIR, gateHit);
        if (out & bit(X2_AXIS))
            stepsAlone++;
    }
    TEST_ASSERT_EQUAL(3, stepsAlone); // never more than the limit, even if the other stall never comes
    TEST_ASSERT_EQUAL_HEX8(0, out);
    TEST_ASSERT_TRUE(g.skewFailed);
    TEST_ASSERT_EQUAL_HEX8(0, g.lock);
}

static void test_gate_holds_x_back_for_the_trim()
{
    int8_t none[N_AXIS];
    memset(none, -1, sizeof(none));
    HomingGate g;
    gateHits = 0xFF;
    g.begin((uint8_t)bit(X2_AXIS), none, false, 0, 0);
    TEST_ASSERT_EQUAL_HEX8(bit(X2_AXIS), g.filter(X_PAIR, gateHit)); // only X2 moves, nothing can stop it early
    TEST_ASSERT_EQUAL_HEX8(bit(X2_AXIS), g.lock);
}

static void test_gate_probe_with_until_clear()
{
    int8_t in[N_AXIS];
    memset(in, -1, sizeof(in));
    in[Z_AXIS] = IN_Z_FLAG;
    HomingGate g;
    gateHits = 1u << IN_Z_FLAG;
    g.begin((uint8_t)bit(Z_AXIS), in, true, 0, 0);
    TEST_ASSERT_EQUAL_HEX8(bit(Z_AXIS), g.filter((uint8_t)bit(Z_AXIS), gateHit));
    gateHits = 0;
    TEST_ASSERT_EQUAL_HEX8(0, g.filter((uint8_t)bit(Z_AXIS), gateHit));
}

static void test_settings_second_x_motor()
{
    cfg::init();
    TEST_ASSERT_EQUAL(X2_AXIS, axisByName("x2"));
    TEST_ASSERT_EQUAL(-1, axisByName("s"));
    TEST_ASSERT_EQUAL_FLOAT(80.0f, cfg::axis(cfg::TMC_SG, X2_AXIS));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, cfg::get(cfg::IN_INVERT_7));
    TEST_ASSERT_EQUAL_FLOAT(60.0f, cfg::get(cfg::HOME_CUR));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, cfg::get(cfg::HOME_BLANK));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, cfg::get(cfg::HOME_SKEW_MAX));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cfg::get(cfg::X2_OFFSET));
    std::string err;
    TEST_ASSERT_TRUE(cfg::set("x2_offset", "-0.25", err));
    TEST_ASSERT_EQUAL_FLOAT(-0.25f, cfg::get(cfg::X2_OFFSET));
    TEST_ASSERT_FALSE(cfg::set("x2_offset", "4", err)); // more than the pull-off could push into the stop
    TEST_ASSERT_TRUE(cfg::set("tmc_sg_x2", "95", err));
    TEST_ASSERT_EQUAL_FLOAT(95.0f, cfg::axis(cfg::TMC_SG, X2_AXIS));
    cfg::init();
}

// ---- tool changer board --------------------------------------------------------------------------
static void test_toolbus_needs_a_value_twice()
{
    ToolBus b;
    TEST_ASSERT_FALSE(b.fresh(0));
    b.feed(0x1F, 100);
    TEST_ASSERT_FALSE(b.fresh(100)); // one byte proves nothing
    b.feed(0x1F, 300);
    TEST_ASSERT_TRUE(b.fresh(300));
    TEST_ASSERT_EQUAL_HEX8(0x1F, b.value(300));
    b.feed(0x10, 500); // one damaged byte does not change the state
    TEST_ASSERT_EQUAL_HEX8(0x1F, b.value(500));
    b.feed(0x1F, 700);
    TEST_ASSERT_EQUAL_HEX8(0x1F, b.value(700));
    b.feed(0x17, 900); // a real change: taken on its second byte
    TEST_ASSERT_EQUAL_HEX8(0x1F, b.value(900));
    b.feed(0x17, 1100);
    TEST_ASSERT_EQUAL_HEX8(0x17, b.value(1100));
}

static void test_toolbus_goes_stale_when_silent()
{
    ToolBus b;
    b.feed(0x03, 100);
    b.feed(0x03, 300);
    TEST_ASSERT_TRUE(b.fresh(1299));
    TEST_ASSERT_FALSE(b.fresh(1300));
    TEST_ASSERT_EQUAL_HEX8(0, b.value(1300)); // never old bits as if they were true
    b.feed(0x55, 2000); // garbage alone does not bring it back
    b.feed(0xAA, 2200);
    TEST_ASSERT_FALSE(b.fresh(2200));
}

static void test_toolbus_clock_wraps()
{
    ToolBus b;
    b.feed(0x01, 0xFFFFFF00u);
    b.feed(0x01, 0xFFFFFFF0u);
    TEST_ASSERT_TRUE(b.fresh(0x00000100u));
}

// ---- TMC ---------------------------------------------------------------------------------------
static void test_tmc_crc_known_frame()
{
    // Read request for IFCNT (0x02), address 0: CRC from the TMC2209 datasheet algorithm
    uint8_t f[4];
    tmc::readFrame(f, 0, tmc::IFCNT);
    TEST_ASSERT_EQUAL_HEX8(0x05, f[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, f[2]);
    TEST_ASSERT_EQUAL_HEX8(tmc::crc8(f, 3), f[3]);
    TEST_ASSERT_EQUAL_HEX8(0x8F, f[3]); // computed independently (Python, datasheet algorithm)
}

static void test_tmc_write_and_reply()
{
    uint8_t w[8];
    tmc::writeFrame(w, 0, tmc::GCONF, 0x000001C0);
    TEST_ASSERT_EQUAL_HEX8(0x80, w[2]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, w[6]);
    TEST_ASSERT_EQUAL_HEX8(0xF6, w[7]); // computed independently
    uint8_t r[8] = {0x05, 0xFF, tmc::IOIN, 0x21, 0x00, 0x00, 0x40, 0};
    r[7] = tmc::crc8(r, 7);
    uint32_t v = 0;
    TEST_ASSERT_TRUE(tmc::parseReply(r, tmc::IOIN, v));
    TEST_ASSERT_EQUAL_HEX32(0x21000040, v);
    r[5] ^= 1; // corrupted
    TEST_ASSERT_FALSE(tmc::parseReply(r, tmc::IOIN, v));
}

static void test_tmc_current_and_mres()
{
    uint8_t cs;
    bool vsense;
    float real = tmc::currentScale(800, 0.11f, cs, vsense);
    TEST_ASSERT_TRUE(vsense); // fits the fine 0.18 V range: CS 25
    TEST_ASSERT_EQUAL(25, cs);
    TEST_ASSERT_FLOAT_WITHIN(40, 800, real);
    real = tmc::currentScale(1500, 0.11f, cs, vsense);
    TEST_ASSERT_FALSE(vsense); // needs the 0.325 V range
    TEST_ASSERT_FLOAT_WITHIN(60, 1500, real);
    TEST_ASSERT_EQUAL(0, tmc::mres(256));
    TEST_ASSERT_EQUAL(4, tmc::mres(16));
    TEST_ASSERT_EQUAL(8, tmc::mres(1));
    tmc::Config c;
    c.microsteps = 16;
    TEST_ASSERT_EQUAL_HEX32(4u << 24, tmc::chopconf(c) & (0x0Fu << 24));
    c.stealth = false;
    TEST_ASSERT_TRUE(tmc::gconf(c) & (1u << 2));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_args_words_and_kv);
    RUN_TEST(test_args_quotes_and_bad_number);
    RUN_TEST(test_args_empty_and_comment);
    RUN_TEST(test_line_quoting);
    RUN_TEST(test_settings_defaults_and_set);
    RUN_TEST(test_settings_words_and_ranges);
    RUN_TEST(test_settings_pump_used);
    RUN_TEST(test_settings_names_unique);
    RUN_TEST(test_x2_mirrors_x);
    RUN_TEST(test_gate_passes_steps_when_inactive);
    RUN_TEST(test_gate_each_side_stops_alone);
    RUN_TEST(test_gate_x2_can_stop_first);
    RUN_TEST(test_gate_blank_ignores_early_stalls);
    RUN_TEST(test_gate_skew_limit_stops_both);
    RUN_TEST(test_gate_holds_x_back_for_the_trim);
    RUN_TEST(test_gate_probe_with_until_clear);
    RUN_TEST(test_settings_second_x_motor);
    RUN_TEST(test_toolbus_needs_a_value_twice);
    RUN_TEST(test_toolbus_goes_stale_when_silent);
    RUN_TEST(test_toolbus_clock_wraps);
    RUN_TEST(test_tmc_crc_known_frame);
    RUN_TEST(test_tmc_write_and_reply);
    RUN_TEST(test_tmc_current_and_mres);
    return UNITY_END();
}
