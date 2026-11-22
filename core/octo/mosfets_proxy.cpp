#include "../ctrl/mosfets.h"
#include "octopus.h"
#include "../ctrl/settings/general_settings.h"

#include <cctype>
#include <cstring>

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

std::string bottomLightLine(bool part)
{
    const char *c = part ? settings.lightPartRgb : settings.lightCalRgb;
    bool hex = strlen(c) == 6;
    for (int i = 0; hex && i < 6; i++)
        hex = isxdigit((unsigned char)c[i]) != 0;
    return "LIGHT bottom " + std::to_string((int)settings.lightBottom) + (hex ? std::string(" ") + c : std::string());
}

void Mosfets::bottomLight(bool part) { octo.call(bottomLightLine(part)); }

float Mosfets::pump() const { return octo.status().pump / 100.0f; }
bool Mosfets::isValveOpen() const { return octo.status().valve; }
float Mosfets::light(bool top) const
{
    const OctoStatus s = octo.status();
    return (top ? s.lightTop : s.lightBottom) / 100.0f;
}
