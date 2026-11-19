#include "toolbus.h"

#include <Arduino.h>

namespace
{
    HardwareSerial bus(PA_10, PA_9); // TFT header: RX pin 2, TX pin 3 (nothing is sent)
    ToolBus state;
}

void toolbus_begin() { bus.begin(TOOLBUS_BAUD); }

void toolbus_poll()
{
    while (bus.available())
        state.feed((uint8_t)bus.read(), millis());
}

bool toolbus_fresh() { return state.fresh(millis()); }
uint8_t toolbus_value() { return state.value(millis()); }
