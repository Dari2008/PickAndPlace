/**
 * lights.h - camera lights: top light (FAN0, always on) and bottom light (NeoPixel ring + FAN1)
 * -----------------------------------------------------------------------------
 *   top     FAN0 header (12 V LEDs, voltage jumper on 12 V). On at light_top_idle % from power-on ("always on"); it goes dark by itself
 *           while the bottom light is on (the bottom camera looks up past it) and comes back after.
 *   bottom  NeoPixel / WS2812 ring on the RGB header (PB0, buffered to 5 V on the board; 5 V + GND on the
 *           same header), neo_count LEDs, colour neo_r / neo_g / neo_b scaled by the brightness. FAN1 follows
 *           the same brightness (for a plain LED ring instead).
 *
 *   LIGHT TOP <0-100|ON|OFF|IDLE>   LIGHT BOTTOM <0-100|ON|OFF>
 *
 * The ring is written with interrupts off (~30 us per LED). That would delay step pulses, so a new
 * frame is only sent while no motion is running (lights::loop()); the camera code switches the light
 * when the head stands still anyway.
 * -----------------------------------------------------------------------------
 */
#pragma once

namespace lights
{
    void begin();               // top at its idle level, bottom off
    void setTop(float f);       // 0..1 (commanded; dark while the bottom light is on)
    void setTopIdle();          // back to light_top_idle
    void setBottom(float f);    // 0..1
    void allOff();              // e-stop / link loss: both dark (CLEAR brings the top light back)
    void loop();                // sends a pending ring frame when motion is idle
    float top();                // effective levels (STATUS)
    float bottom();
}
