/**
 * app.h - command dispatch, reports, watchdog, e-stop (the firmware's main loop)
 * -----------------------------------------------------------------------------
 * One port: USB CDC (/dev/ttyACM0 on the Pi). Text protocol of ../protocol.md:
 * a command per line, answered by records, then exactly one "ok" / "error ...".
 * Events start with '!': !ready, !status (REPORT), !alarm.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>
#include "proto/out.h"

namespace app
{
    void begin(Sink &out);
    void loop();
    void handleLine(char *line); // one command (also used by tests on the board)
}

#define FIRMWARE_NAME "pap-motion"
#define FIRMWARE_VERSION "1.0"
#define PAP_BOARD_NAME "BTT Octopus V1.1"
