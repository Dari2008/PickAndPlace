/**
 * mosfets.h - pump, valve and camera lights: outputs of the Octopus (PUMP / VALVE / LIGHT). The head camera
 * fan, the housing fan and the Pi housing fan run by themselves on the Octopus (settings cam_fan / box_fan /
 * pi_fan).
 * -----------------------------------------------------------------------------
 * Same API as the ATmega version (../../../src/mosfets.h), so the ported code is
 * unchanged; every call sends a command to the Octopus, the getters read its status.
 * -----------------------------------------------------------------------------
 */
#pragma once

class Mosfets
{
public:
    void begin() {}
    void allOff();

    void enablePump(float f = 1.0f);
    void disablePump() { enablePump(0.0f); }
    float pump() const;

    void openValve();
    void closeValve();
    bool isValveOpen() const;

    void setLight(bool top, float f); // camera lights (FAN0 top, bottom = NeoPixel ring + FAN1)
    void topLightIdle();              // top light back to its always-on level (light_top_idle on the Octopus)
    float light(bool top) const;


};

extern Mosfets mosfets;
