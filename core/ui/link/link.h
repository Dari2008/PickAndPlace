/**
 * link.h - the display UI's command port to the controller (in-process on the Pi)
 * -----------------------------------------------------------------------------
 * Same API as the ESP32's serial link (../../../../esp32-ui/src/link/link.h):
 *   ctrl.send("LS -p /jobs", [](const Reply &r) { ... });   // async
 *   Reply r = ctrl.call("STATUS");                            // blocking (not from the link thread)
 *   ctrl.upload("/jobs/a.pap", text, progress, done);         // line by line, with CRC
 *   ctrl.onEvent([](const Record &e) { ... });                // "!status", "!job", ...
 * On the Pi the "wire" is a Channel of the controller (ctrl/protocol/ports.h):
 * the same text lines, without a serial port in between.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../../link/record.h"

class Channel;

struct Reply
{
    bool ok = false;
    bool timeout = false;
    std::string error;
    std::vector<Record> records;
};

using ReplyFn = std::function<void(const Reply &)>;
using EventFn = std::function<void(const Record &)>;
using ProgressFn = std::function<void(size_t done, size_t total)>;

class Link
{
public:
    void begin();
    void end();

    void send(const std::string &line, ReplyFn fn = nullptr, uint32_t timeoutMs = 0);
    Reply call(const std::string &line, uint32_t timeoutMs = 0);
    void upload(const std::string &path, std::string content, ProgressFn progress, ReplyFn done);

    void onEvent(EventFn fn) { listeners_.push_back(fn); }

    // Heard from the controller in the last 2 s (it reports every 250 ms)
    bool connected() const;
    size_t queued();

private:
    struct Request
    {
        std::string line;
        ReplyFn fn;
        uint32_t timeoutMs = 0;
        bool isUpload = false;
        std::string path, content;
        ProgressFn progress;
    };

    void run();
    void handleLine(const std::string &line);
    void startNext();
    void finish(bool ok, const std::string &error, bool timeout = false);
    void writeLine(const std::string &line);
    void uploadStep();
    static uint32_t defaultTimeout(const std::string &line);

    std::shared_ptr<Channel> ch_;
    std::thread thread_;
    std::thread::id threadId_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::deque<Request> queue_;
    std::vector<EventFn> listeners_;

    bool busy_ = false;
    Request cur_;
    Reply reply_;
    uint32_t sentAt_ = 0;
    size_t upPos_ = 0;
    uint32_t upCrc_ = 0;
    int upStage_ = 0;
    std::atomic<uint32_t> lastRx_{0};
};

extern Link ctrl;
