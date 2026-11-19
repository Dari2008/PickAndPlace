#include "web.h"
#include "../../ctrl/protocol/commands.h"

// Uploads from the website (or the TCP API); the display's own uploads have their own progress screen
UploadState uploadState()
{
    const UploadInfo i = uploadInfo();
    UploadState s;
    if (i.fromUi)
        return s;
    s.active = i.active;
    s.ok = i.ok;
    s.path = i.path;
    s.error = i.error;
    s.done = i.done;
    s.total = i.total;
    s.endedAt = i.endedAt;
    return s;
}
