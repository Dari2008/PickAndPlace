/**
 * nozzles.h - automatic nozzle changer: the rack, its slots and the change sequence
 * -----------------------------------------------------------------------------
 * A straight rack of push-on nozzles. Settings (GET / SET, nozzle_*): number of slots, first and last slot
 * position (the others are spaced evenly between them), the seesaw Z for pushing a nozzle on (nozzle_z_pick)
 * and for parking it in a slot (nozzle_z_drop), an optional sideways slide, speed. Each slot has a sensor: "is there
 * a nozzle in this slot". The tool changer board sends all of them to the Octopus as one byte, 5 times a second;
 * which bit is which slot is nozzle_bit_n, set by triggering the slots in order (NozzleLearn, NOZZLE LEARN).
 *
 * Per slot (file <data>/nozzles.txt, NOZZLE command): a name ("CN040", "0402"), a tip offset dx / dy
 * (default 0 / 0) and a tip height dz (how much further the tip reaches than head2_reach; measured on the
 * load cell). Also stored: which slot's nozzle is on the head now (0 = none).
 *
 * Touch-off (load cell, HX711 on the Octopus): over the pad, the seesaw lowers the vacuum nozzle until it
 * presses the pad (PROBE z=), backs off, touches again slowly. Contact z gives the real reach:
 * reach = loadcell_height - z. Without a nozzle changer it sets head2_reach itself.
 *
 *   nozzles.load();
 *   int n = nozzles.find("CN040");               // slot by name or number, 0 = unknown
 *   std::vector<NozzleStep> plan = nozzles.plan(n);   // moves + checks from the mounted nozzle to slot n
 *   std::string err; nozzles.change(n, err);       // runs the plan right away (commands; jobs run the plan themselves)
 *
 * Change sequence (Z: seesaw, negative = nozzle side down; all at nozzle_z_safe first):
 *   put back slot m:  slot m must be empty -> over slot m (+ slide) -> down to z_drop -> (slide in) -> up: the slot
 *                     keeps the nozzle -> slot m must show a nozzle
 *   pick up slot n:   slot n must show a nozzle -> over slot n -> down to z_pick (pushed on) -> wait ->
 *                     (slide out) -> up -> slot n must be empty
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#define MAX_NOZZLES 8

struct NozzleSlot
{
    std::string name;
    float dx = 0, dy = 0; // tip offset (mm), added to every pick / place position
    float dz = 0;         // tip reaches this much further down than head2_reach (mm, load cell)
};

struct NozzleStep
{
    enum Kind
    {
        MOVE,       // x, y, z at speed (0 = travel speed)
        WAIT,       // ms
        ROTATE0,    // nozzle angle back to 0 before changing
        EXPECT,     // slot `slot` must (present = true) / must not show a nozzle
        MOUNTED,    // from now on slot `slot` is on the head (0 = none)
        CALIBRATE,  // head is over the bottom camera: measure the tip offset of slot `slot` and store it
        TOUCHOFF    // load cell: measure the tip height of slot `slot` and store it (moves by itself)
    } kind;
    float x = 0, y = 0, z = 0, speed = 0;
    uint32_t ms = 0;
    int slot = 0;
    bool present = false;
};

// Learn which sensor bit belongs to which slot: the user triggers slot 1, 2, 3 ... (nozzle in or out, or a finger).
// Each new bit that changes goes to the next slot. Bits already taken are ignored, so taking a nozzle out and
// putting it back is one trigger. Nothing is stored until every slot has its bit.
class NozzleLearn
{
public:
    enum Step
    {
        WAITING,     // nothing new
        ASSIGNED,    // the slot before next() got its bit
        FINISHED,    // every slot has its bit
        TWO_AT_ONCE  // two new bits changed together: which is it? Trigger that slot again
    };
    void start(unsigned raw, int slots);
    void cancel() { active_ = false; }
    Step feed(unsigned raw);
    bool active() const { return active_; }
    int next() const { return next_; }   // slot waited for, 1..slots
    int slots() const { return slots_; }
    int bit(int slot) const { return bit_[(slot - 1) % MAX_NOZZLES]; }

private:
    bool active_ = false;
    int slots_ = 0, next_ = 1;
    unsigned last_ = 0, used_ = 0;
    int bit_[MAX_NOZZLES] = {0};
};

class Nozzles
{
public:
    void load();
    bool save();

    int count() const;                    // nozzle_count (0 = no changer)
    bool slotXY(int n, float &x, float &y) const;
    int find(const std::string &nameOrNumber) const; // 1..count, 0 = not found
    NozzleSlot &slot(int n) { return slots_[(n - 1) % MAX_NOZZLES]; }
    int mounted() const { return mounted_; }
    void setMounted(int n);               // saved
    // Tip offset of the nozzle on the head (0 / 0 without one)
    float offsetX() const { return mounted_ ? slots_[mounted_ - 1].dx : 0.0f; }
    float offsetY() const { return mounted_ ? slots_[mounted_ - 1].dy : 0.0f; }
    float offsetZ() const { return mounted_ ? slots_[mounted_ - 1].dz : 0.0f; }

    // Sensors: bit n-1 = slot n shows a nozzle. false = sensors not readable (Octopus offline).
    bool readPresent(unsigned &mask);
    // The byte as sent, before nozzle_bit_n and nozzle_sensor_invert. false = tool changer board silent or Octopus offline.
    bool readRaw(unsigned &raw);
    // For a check right after a move: the sensors arrive 5 times a second, so wait up to SENSOR_SETTLE_MS for slot
    // `slot` to show `want`. there = what it shows in the end. false = sensors not readable.
    bool slotShows(int slot, bool want, bool &there);
    static constexpr uint32_t SENSOR_SETTLE_MS = 1000;
    std::vector<NozzleStep> plan(int target) const;
    bool change(int target, std::string &err); // blocking, outside jobs (NOZZLE CHANGE)
    // Bottom light on, measure the tip, light off; stores dx / dy of `slot` (the head stands over the camera)
    bool calibrate(int slot, std::string &err);
    // Load cell touch-off of the nozzle on the head (blocking, moves). slot 0 = no changer: sets head2_reach.
    // reach = measured head 2 reach with this nozzle, z = contact seesaw Z.
    bool touchOff(int slot, std::string &err, float *reach = nullptr, float *z = nullptr);
    // Simulation: how much further each slot's nozzle reaches (the simulated load cell uses it)
    static float simTipDz(int slot);

    // Simulation (pap-core --sim): the rack is emulated here
    void simApply(const NozzleStep &s);
    void simFlipBit(int bit) { simRaw_ ^= 1u << bit; } // as if that sensor changed (raw byte, for NOZZLE LEARN)

private:
    NozzleSlot slots_[MAX_NOZZLES];
    int mounted_ = 0;
    bool loaded_ = false;
    unsigned simMask_ = 0xFF; // all slots full, nothing on the head
    unsigned simRaw_ = 0;
};

extern Nozzles nozzles;
