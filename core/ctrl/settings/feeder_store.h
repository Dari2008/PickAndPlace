/**
 * feeder_store.h - feeders, one file each in /feeders (created automatically)
 *
 *   /feeders/feeder02.txt
 *     name = 100n 0402
 *     feeder_pos = 125.0     mm, E1 moves the E0 motor here
 *     teeth = 2              teeth of the feed wheel per advance (wheel: feeder_wheel_teeth, 20)
 *     advance = 20.0         deg, E0 turns this far per advance (only used when teeth = 0)
 *     next = 2               next pocket to pick (1 = first)
 *     part = C1525           part ID (from the QR code)
 *     tag = F07              feeder ID from its QR code; auto detect matches feeders by it
 *     pocket = 12.50, 40.00  X, Y in mm, one line per pocket, picked in order
 *
 * Only the list (number, name, part ID) is kept for all feeders; a feeder
 * being worked on is loaded completely into a Feeder (e.g. `feeder`).
 */
#pragma once

#include <Arduino.h>

#define MAX_FEEDERS 24
#define MAX_POCKETS 24
#define FEEDER_NAME_LEN 20
#define FEEDER_PART_LEN 12
#define FEEDER_TAG_LEN 31 // feeder ID from the QR code

struct Pocket
{
    float x, y; // mm
};

struct Feeder
{
    uint8_t num;
    char name[FEEDER_NAME_LEN + 1];
    float feederPos; // mm, E1
    float advance;   // deg, E0 (when teeth = 0)
    uint8_t teeth;   // teeth per advance, 0 = use `advance`
    uint8_t next;    // 1-based
    char part[FEEDER_PART_LEN + 1];
    char tag[FEEDER_TAG_LEN + 1]; // "" = no tag known
    uint8_t pocketCount;
    Pocket pockets[MAX_POCKETS];
};

struct FeederEntry
{
    uint8_t num;
    char name[FEEDER_NAME_LEN + 1];
    char part[FEEDER_PART_LEN + 1];
};

class FeederStore
{
public:
    // Read the list of feeder files (creates /feeders if missing).
    bool scan();
    uint8_t count() const { return count_; }
    const FeederEntry &entry(uint8_t i) const { return list_[i]; }

    bool load(uint8_t num, Feeder &f);
    bool save(const Feeder &f);
    bool remove(uint8_t num);

    // Create the next free feeder file with defaults. Returns its number or -1.
    int16_t add();

    // Feeder for a job's [feeders] line: part ID first, then name, both
    // without case (pap-format.md). Feeder number, or 0.
    uint8_t match(const char *text) const;
    // Index in the list of feeder `num`, or -1.
    int8_t indexOf(uint8_t num) const;

    // Feeder number whose file has `tag = <tag>`, or 0.
    uint8_t findByTag(const char *tag);
    // Feeder number without a tag whose feeder_pos is within `tolerance` of
    // `pos` (mm), or 0. Used when a feeder's tag can't be read.
    uint8_t findUntaggedNear(float pos, float tolerance);

    // Copy the keys of a tag's text ("name = ..." lines) into `f`:
    // name, part, advance. Lines may also be separated by ';'.
    static void applyTagText(Feeder &f, char *text);
    // QR code text "F07;name=100n 0402;part=C1525;teeth=2": the first field (without '=') or id= is the
    // feeder ID, the rest are feeder keys (name, part, teeth, advance). Sets tag + those keys.
    static void applyQr(Feeder &f, const char *text);
    static void qrId(const char *text, char *id); // id: FEEDER_TAG_LEN + 1 bytes

    // Degrees E0 turns per advance: teeth x 360 / feeder_wheel_teeth, or `advance` without teeth
    static float advanceDeg(const Feeder &f);

    // "/feeders/feederNN.txt"
    static void path(uint8_t num, char *buf);

private:
    void setEntry(uint8_t num, const char *name, const char *part);

    FeederEntry list_[MAX_FEEDERS];
    uint8_t count_ = 0;
};

extern FeederStore feeders;
extern Feeder feeder; // work buffer for protocol commands and the job runner
