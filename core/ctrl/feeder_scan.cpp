#include "feeder_scan.h"
#include <motion.h>
#include "settings/general_settings.h"
#include "protocol/out.h"
#include "protocol/ports.h"
#include "settings/feeder_store.h"
#include "../octo/octopus.h"
#include "../vision/camera.h"
#include "../vision/vision.h"

#include <cmath>
#include <cstdlib>

FeederScan feederScan;

static const float NO_TAG_MATCH_MM = 5.0f; // untagged feeder counts as "same" within this
static const uint16_t PROGRESS_MS = 500;   // "!scan state=searching e1=..." interval

void FeederScan::event(PGM_P state)
{
    Print &p = ports.all;
    p.print(F("!scan"));
    out::strP(p, PSTR("state"), state);
#if HAS_E1
    out::flt(p, PSTR("e1"), motion.getPosition(E1_AXIS), 1);
#endif
    out::num(p, PSTR("count"), count_);
    p.println();
    lastEvent_ = millis();
}

PGM_P FeederScan::start()
{
#if !HAS_E1
    return PSTR("this board has no E1");
#else
    if (!feeders.scan())
        return PSTR("no SD card");
    nfcOk_ = false; // feeders are identified by their QR code (top camera), not by NFC
    state_ = HOMING;
    stop_ = false;
    count_ = 0;
    event(PSTR("homing"));
    return nullptr;
#endif
}

void FeederScan::update()
{
#if HAS_E1
    switch (state_)
    {
    case IDLE:
        return;

    case HOMING:
        if (!motion.homeE1()) // blocks
        {
            finish(PSTR("gantry homing failed (DIAG6 / StallGuard)"));
            return;
        }
        state_ = SEARCHING;
        motion.startFeederProbe(mval("travel_e1", 500), FEEDER_SCAN_SPEED);
        event(PSTR("searching"));
        break;

    case SEARCHING:
    {
        if (stop_)
        {
            motion.cancelFeederProbe();
            finish(nullptr);
            return;
        }
        const int8_t r = motion.pollFeederProbe();
        if (r > 0)
        {
            hitPos_ = motion.getTargetPosition(E1_AXIS); // from the PROBE answer: where E1 stopped
            state_ = READING;
            event(PSTR("reading"));
        }
        else if (r < 0)
            finish(nullptr); // end of the rail
        else if (millis() - lastEvent_ >= PROGRESS_MS)
            event(PSTR("searching"));
        break;
    }

    case READING:
        handleFeeder();
        if (state_ == IDLE)
            return; // failed
        state_ = LEAVING;
        break;

    case LEAVING:
        if (!driveOffSwitch())
        {
            finish(PSTR("feeder switch stays pressed (DIAG4)"));
            return;
        }
        if (stop_ || count_ >= MAX_FEEDERS || motion.getPosition(E1_AXIS) >= mval("travel_e1", 500) - 1.0f)
        {
            finish(nullptr);
            return;
        }
        state_ = SEARCHING;
        motion.startFeederProbe(mval("travel_e1", 500), FEEDER_SCAN_SPEED);
        event(PSTR("searching"));
        break;
    }
#endif
}

// Simulation: the QR code a simulated feeder (PAP_SIM_FEEDERS) at E1 = e1 carries
static std::string simFeederQr(float e1)
{
    const char *env = getenv("PAP_SIM_FEEDERS");
    std::string list = env ? env : "";
    int best = -1, i = 0;
    float bestD = 1e9f;
    for (size_t p = 0; p < list.size(); i++)
    {
        size_t c = list.find(',', p);
        if (c == std::string::npos)
            c = list.size();
        const float d = fabsf(atof(list.substr(p, c - p).c_str()) - e1);
        if (d < bestD)
            bestD = d, best = i;
        p = c + 1;
    }
    if (best < 0 || bestD > 15)
        return "";
    const int n = best + 1;
    return "SIM-F" + std::to_string(n) + ";name=Sim feeder " + std::to_string(n) + ";part=C" + std::to_string(100 + n) +
           ";teeth=2";
}

bool FeederScan::readQrAt(float e1, std::string &text, std::string &err)
{
    if (!vision::available(vision::TOP))
        return err = "no top camera", false;
    if (!motion.isHomed())
        return err = "home X / Y first (the head carries the top camera)", false;
    const float qx = settings.feederQr0[0] + settings.feederQrDir[0] * e1;
    const float qy = settings.feederQr0[1] + settings.feederQrDir[1] * e1;
    const float hx = qx - settings.camTopOffset[0], hy = qy - settings.camTopOffset[1];
    const float f = settings.travelSpeed;
    motion.moveTo(motion.getTargetPosition(X_AXIS), motion.getTargetPosition(Y_AXIS), 0, f); // seesaw level first
    motion.moveTo(hx, hy, 0, f);
    motion.waitUntilIdle();
    if (octo.simulated())
        Cameras::setSimQr(simFeederQr(e1));
    delay(settings.camSettleMs);
    const bool ok = vision::readQr(text, err);
    if (octo.simulated())
        Cameras::setSimQr("");
    return ok;
}

void FeederScan::handleFeeder()
{
    std::string qr, qrErr;
    const bool tag = settings.feederQr && readQrAt(hitPos_, qr, qrErr);
    char id[FEEDER_TAG_LEN + 1] = "";
    uint8_t num = 0;
    if (tag)
    {
        FeederStore::qrId(qr.c_str(), id);
        num = feeders.findByTag(id);
    }
    if (!num)
        num = feeders.findUntaggedNear(hitPos_, NO_TAG_MATCH_MM);

    bool isNew = false;
    if (!num)
    {
        const int16_t added = feeders.add();
        if (added < 0)
        {
            finish(PSTR("can't create a feeder (SD card full / missing)"));
            return;
        }
        num = (uint8_t)added;
        isNew = true;
    }
    if (!feeders.load(num, feeder))
    {
        finish(PSTR("can't read the feeder file"));
        return;
    }
    feeder.feederPos = hitPos_;
    if (tag)
        FeederStore::applyQr(feeder, qr.c_str());
    if (!feeders.save(feeder))
    {
        finish(PSTR("can't save the feeder file"));
        return;
    }
    count_++;

    Print &p = ports.all;
    p.print(F("!scan found"));
    out::num(p, PSTR("num"), num);
    out::flt(p, PSTR("pos"), hitPos_, 2);
    out::num(p, PSTR("tag"), tag);
    out::num(p, PSTR("new"), isNew);
    out::str(p, PSTR("name"), feeder.name);
    out::str(p, PSTR("id"), feeder.tag);
    if (!tag && settings.feederQr)
        out::str(p, PSTR("qr_error"), qrErr.c_str());
    p.println();
}

// Move on in 1 mm steps until the feeder switch opens again.
bool FeederScan::driveOffSwitch()
{
#if HAS_E1
    float moved = 0.0f;
    while (motion.feederSwitchHit() && moved < FEEDER_SWITCH_RELEASE)
    {
        motion.moveAxisBy(E1_AXIS, 1.0f, FEEDER_SCAN_SPEED);
        motion.waitUntilIdle();
        moved += 1.0f;
    }
    return !motion.feederSwitchHit();
#else
    return true;
#endif
}

void FeederScan::finish(PGM_P error)
{
#if HAS_E1
    motion.cancelFeederProbe();
#endif
    state_ = IDLE;
    feeders.scan();
    Print &p = ports.all;
    p.print(F("!scan"));
    out::strP(p, PSTR("state"), error ? PSTR("failed") : PSTR("done"));
    out::num(p, PSTR("count"), count_);
    if (error)
        out::strP(p, PSTR("error"), error);
    p.println();
}
