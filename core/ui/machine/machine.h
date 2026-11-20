/**
 * machine.h - what the display and the website know about the controller
 * -----------------------------------------------------------------------------
 * Filled from the link: "!status" / "!job" every 250 ms (REPORT 250), the
 * other events as they come, FEEDERS and GET after connecting and after
 * changes. All getters return copies (safe from any task).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <Arduino.h>
#include <functional>
#include <string>
#include <vector>
#include "../../link/record.h"
#include "../rtos.h"

struct MachineStatus
{
    bool connected = false;
    std::string state = "offline"; // idle running paused message scanning alarm offline
    float x = 0, y = 0, z = 0, e0 = 0, e1 = 0, rot = 0; // rot: nozzle angle
    bool homed = false, e1Homed = false, busy = false, sd = false;
    int alarm = 0;
    int pump = 0; // %
    bool pumpUsed = true; // pump_used: a vacuum pump is connected
    int lightTop = 0, lightBottom = 0; // %
    bool valve = false;
};

struct JobStatus
{
    std::string state = "idle"; // idle running paused message done stopped failed
    std::string file, name, part, feeder, phase, next, message, error;
    int done = 0, total = 0, skipped = 0, percent = 0;
    long elapsed = 0, remaining = -1; // s
    bool active() const { return state == "running" || state == "paused" || state == "message"; }
};

struct Pocket
{
    float x, y;
};

struct FeederInfo
{
    int num = 0;
    std::string name, part, tag;
    float pos = 0, advance = 0; // advance: degrees actually turned
    int teeth = 0;              // teeth per advance (0 = degrees only)
    int next = 1, pocketCount = 0;
    std::vector<Pocket> pockets; // filled by loadFeeder()
};

struct SettingInfo
{
    std::string key, value;
};

// One entry of the settings catalog (SETTINGS command, ctrl/settings/catalog.h)
struct CatalogEntry
{
    std::string key, value, group, label, unit, kind, choices, help;
    float min = 0, max = 0;
    int decimals = 2;
    bool machine = false;
};

struct ScanFound
{
    int num = 0;
    float pos = 0;
    bool tag = false, isNew = false;
    std::string name;
};

// SGTEST live load values ("!sg" events while Calibration -> Sensorless homing measures)
struct SgLive
{
    std::string axis;
    int v = -1, v2 = -1; // X2: v2, other axes -1
    float mm = 0;        // commanded travel so far
    uint32_t seq = 0;    // counts events
};

struct ScanInfo
{
    std::vector<ScanFound> found;
    bool active = false;
    std::string state; // homing searching reading done failed
    int count = 0;
    float e1 = 0;
    std::string last, error;
};

class Machine
{
public:
    void begin();
    void tick(); // from loop()

    MachineStatus status();
    JobStatus job();
    std::vector<FeederInfo> feeders();
    std::vector<SettingInfo> settings();
    std::vector<CatalogEntry> catalog();
    ScanInfo scan();
    std::string setting(const char *key, const char *def = "");
    std::string lastAlarm();
    SgLive sgLive();

    void refreshFeeders();
    void refreshSettings();
    void resetScan(); // forget the last auto detect (a new one starts)

    // Counter that changes whenever anything above changed (redraw hint).
    uint32_t revision() const { return revision_; }
    // Changes with every "!status" / "!job" (positions, outputs, job progress).
    uint32_t statusRevision() const { return statusRevision_; }

    // Raw events for the website (called in the link task).
    void onEvent(std::function<void(const Record &)> fn) { listeners_.push_back(fn); }

private:
    void handle(const Record &e);
    void init(); // after (re)connecting

    SemaphoreHandle_t mutex_ = nullptr;
    MachineStatus status_;
    JobStatus job_;
    std::vector<FeederInfo> feeders_;
    std::vector<SettingInfo> settings_;
    std::vector<CatalogEntry> catalog_;
    ScanInfo scan_;
    std::string alarm_;
    SgLive sg_;
    volatile uint32_t revision_ = 0;
    volatile uint32_t statusRevision_ = 0;
    bool wasConnected_ = false;
    bool initPending_ = false;
    std::vector<std::function<void(const Record &)>> listeners_;
};

extern Machine machine;

// Parse helpers (also used by the website code)
void fillStatus(MachineStatus &s, const Record &r);
void fillJob(JobStatus &j, const Record &r);
FeederInfo feederFrom(const Record &r);
