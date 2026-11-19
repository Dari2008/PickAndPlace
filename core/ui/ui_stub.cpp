// Build without the display UI (make NO_UI=1): pap-core runs headless (website / TCP only)
#include "ui_main.h"

namespace panel
{
    void start(bool) {}
    void stop() {}
}
