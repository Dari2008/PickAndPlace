/**
 * outputs.h - MOSFET outputs of the Octopus (pump, valve, lights, fans)
 * -----------------------------------------------------------------------------
 * FAN5 pump (PWM), HE1 valve (on/off), FAN0 / FAN1 top / bottom camera light (PWM), FAN2 head camera fan,
 * FAN3 housing fan, FAN4 Raspberry Pi housing fan (all three always on: cam_fan / box_fan / pi_fan %).
 * Fan headers with the voltage jumper on 12 V (FAN5, the pump: VIN). pump_used 0 (no pump connected) keeps the
 * pump output off whatever PUMP asks.
 * Low-side MOSFETs: + = supply, - = switched. Every output starts OFF; allOff() (ESTOP, link loss, the e-stop
 * button) switches everything off except the three fans; fansOn() sets them from the settings.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>
#include "pap_board.h"

class Outputs
{
public:
    void begin();
    void set(uint8_t out, float fraction); // 0..1 (the valve: 0 or 1)
    float get(uint8_t out) const { return value_[out]; }
    void allOff();              // not the three fans (cooling keeps running)
    void fansOn();              // cam_fan / box_fan / pi_fan from the settings
    bool anyOn() const;

private:
    float value_[N_OUTPUTS] = {0};
};

extern Outputs outputs;
