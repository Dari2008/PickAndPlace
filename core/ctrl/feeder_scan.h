/**
 * feeder_scan.h - auto detect: find the feeders on the rail (protocol SCAN)
 * -----------------------------------------------------------------------------
 *   1. Home E1 on its switch (D15).
 *   2. Drive E1 slowly along the rail. The step ISR stops E1 on the exact
 *      step the feeder switch (D2) closes: that is the feeder's position.
 *   3. Read the feeder's NFC tag (3 tries).
 *   4. Match and save at once: a known tag ID updates that feeder's file
 *      (feeder_pos + name/part/advance from the tag, pockets kept); an
 *      unknown tag creates a new feeder. Without a readable tag, a tag-less
 *      feeder within 5 mm is updated, otherwise a new "Feeder NN" is made.
 *   5. Drive off the switch and go on, up to E1_MAX_TRAVEL.
 * Progress goes out as "!scan ..." events. update() runs it from loop().
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <Arduino.h>
#include <string>

class FeederScan
{
public:
    // nullptr if started, else a PROGMEM error text.
    PGM_P start();
    void stop() { stop_ = true; }
    void update();
    bool active() const { return state_ != IDLE; }

private:
    enum State : uint8_t
    {
        IDLE,
        HOMING,
        SEARCHING,
        READING,
        LEAVING
    };
    void handleFeeder();
public:
    // Top camera over the QR code of the feeder at E1 = e1 and read it (blocking; scan and FEEDER QR)
    static bool readQrAt(float e1, std::string &text, std::string &err);

private:
    bool driveOffSwitch();
    void finish(PGM_P error);
    void event(PGM_P state);

    State state_ = IDLE;
    bool stop_ = false;
    bool nfcOk_ = false;
    float hitPos_ = 0.0f;
    uint8_t count_ = 0;
    uint32_t lastEvent_ = 0;
};

extern FeederScan feederScan;
