/**
 * octo_sim.h - a simulated Octopus (same text protocol, no hardware)
 * -----------------------------------------------------------------------------
 * Started with octo.start("sim") (pap-core --octopus sim). Moves take the time
 * distance / speed; STATUS, REPORT, WAIT, HOME, PROBE, SENSORS, outputs, GET / SET
 * behave like the firmware (octopus-fw/src/app.cpp). Simulated feeders for PROBE
 * sit at the E1 positions in PAP_SIM_FEEDERS (default "40,110,180"). The simulated load cell is at
 * loadcell_x / _y; the tip of the vacuum nozzle reaches head2_reach + Nozzles::simTipDz(mounted).
 * Used for development on a PC and by the tests.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <cstdlib>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "octopus.h"

class SimTransport : public OctoTransport
{
public:
    bool open() override;
    void close() override {}
    bool write(const std::string &line) override;
    bool readLine(std::string &line, int timeoutMs, bool &broken) override;
    std::string name() const override { return "sim"; }

private:
    struct Seg
    {
        float from[OCTO_AXES], to[OCTO_AXES];
        double t0, t1;
    };
    double now() const;
    void advance();                // drop finished segments
    void position(float *p) const; // current position
    void target(float *t) const;   // end of the queue
    bool busy() const;
    bool pumpUsed() const { return !set_.count("pump_used") || atof(set_.at("pump_used").c_str()) >= 0.5; }
    void queueMove(const float *to, float speed);
    void handle(const std::string &line);
    void out(const std::string &l) { out_.push_back(l); }
    std::string statusLine(const char *type) const;
    void ok() { out("ok"); }
    void error(const std::string &e) { out("error " + e); }

    std::deque<std::string> out_;
    std::vector<Seg> segs_;
    float pos_[OCTO_AXES] = {0};
    bool homed_ = false, e1Homed_ = false, motors_ = false;
    int alarm_ = 0;
    std::map<std::string, std::string> set_;
    std::map<std::string, int> outputs_;
    int reportMs_ = 0;
    double lastReport_ = 0;
    bool waitPending_ = false;
    double homeDoneAt_ = -1;
    double probeDoneAt_ = -1;
    float probeHit_ = 0;
    int probeAxis_ = OE1;
    bool probeFound_ = false;
    std::vector<float> feeders_;
};
