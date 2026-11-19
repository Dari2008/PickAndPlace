/**
 * web.h - website side as seen by the display UI on the Pi
 * -----------------------------------------------------------------------------
 * The website is its own program (pap-web, ../../../web) talking to pap-core over
 * TCP; uploads from it arrive as UPLOAD commands on its port. uploadState() shows
 * them in the display's upload banner (fed by the controller's UPLOAD handler).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct UploadState
{
    bool active = false, ok = false;
    std::string path, error;
    size_t done = 0, total = 0;
    uint32_t endedAt = 0; // millis() when it finished
};
UploadState uploadState();
