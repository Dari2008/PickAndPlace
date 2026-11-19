#include "../ctrl/mosfets.h"
#include "octopus.h"

#include <cmath>
#include <string>

Mosfets mosfets;

static int pct(float f)
{
    if (f < 0)
        f = 0;
    if (f > 1)
        f = 1;
    return (int)lroundf(f * 100.0f);
}

void Mosfets::allOff()
{
    enablePump(0);
    closeValve();
    setLight(false, 0);
    topLightIdle(); // the top light stays on
}

void Mosfets::topLightIdle() { octo.call("LIGHT top IDLE"); }

void Mosfets::enablePump(float f) { octo.call("PUMP " + std::to_string(pct(f))); }
void Mosfets::openValve() { octo.call("VALVE ON"); }
void Mosfets::closeValve() { octo.call("VALVE OFF"); }

void Mosfets::setLight(bool top, float f)
{
    octo.call(std::string("LIGHT ") + (top ? "top " : "bottom ") + std::to_string(pct(f)));
}

float Mosfets::pump() const { return octo.status().pump / 100.0f; }
bool Mosfets::isValveOpen() const { return octo.status().valve; }
float Mosfets::light(bool top) const
{
    const OctoStatus s = octo.status();
    return (top ? s.lightTop : s.lightBottom) / 100.0f;
}
