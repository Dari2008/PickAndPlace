// Port of ../../../../esp32-ui/src/link/link.cpp: Serial1 + FreeRTOS task -> controller Channel + std::thread
#include "link.h"
#include "../../ctrl/protocol/ports.h"
#include <Arduino.h>

#include <cstring>
#include <future>
#include <strings.h>

Link ctrl;

void Link::begin()
{
    ch_ = std::make_shared<Channel>("ui");
    ports.add(ch_);
    running_ = true;
    thread_ = std::thread(&Link::run, this);
    threadId_ = thread_.get_id();
}

void Link::end()
{
    running_ = false;
    if (ch_)
        ch_->close();
    if (thread_.joinable())
        thread_.join();
}

bool Link::connected() const
{
    const uint32_t rx = lastRx_;
    return rx != 0 && (millis() - rx < 2000 || longWait_);
}

uint32_t Link::defaultTimeout(const std::string &line)
{
    auto starts = [&](const char *w) {
        const size_t n = strlen(w);
        return line.size() >= n && strncasecmp(line.c_str(), w, n) == 0 && (line.size() == n || line[n] == ' ');
    };
    if (starts("HOME") || starts("FINDLEN"))
        return 330000;
    if (starts("CAL") || starts("SGTEST"))
        return 180000; // a lens calibration takes 9 pictures
    if (starts("RUN") || starts("CHECK") || starts("LS") || starts("FEEDERS") || starts("CAT") || starts("SD"))
        return 30000;
    if (starts("WAIT"))
        return 600000;
    return 8000;
}

void Link::send(const std::string &line, ReplyFn fn, uint32_t timeoutMs)
{
    Request r;
    r.line = line;
    r.fn = fn;
    r.timeoutMs = timeoutMs ? timeoutMs : defaultTimeout(line);
    std::lock_guard<std::mutex> l(mutex_);
    queue_.push_back(std::move(r));
}

Reply Link::call(const std::string &line, uint32_t timeoutMs)
{
    Reply result;
    if (std::this_thread::get_id() == threadId_)
    {
        result.error = "call() from the link thread";
        return result;
    }
    auto done = std::make_shared<std::promise<Reply>>();
    std::future<Reply> f = done->get_future();
    const uint32_t t = timeoutMs ? timeoutMs : defaultTimeout(line);
    send(line, [done](const Reply &r) { done->set_value(r); }, t);
    if (f.wait_for(std::chrono::milliseconds(t + 2000)) != std::future_status::ready)
    {
        result.timeout = true;
        result.error = "no answer";
        return result;
    }
    return f.get();
}

void Link::upload(const std::string &path, std::string content, ProgressFn progress, ReplyFn done)
{
    Request r;
    r.isUpload = true;
    r.line = "UPLOAD \"" + path + "\"";
    r.path = path;
    r.content = std::move(content);
    r.progress = progress;
    r.fn = done;
    r.timeoutMs = 5000; // per line
    std::lock_guard<std::mutex> l(mutex_);
    queue_.push_back(std::move(r));
}

size_t Link::queued()
{
    std::lock_guard<std::mutex> l(mutex_);
    return queue_.size() + (busy_ ? 1 : 0);
}

void Link::writeLine(const std::string &line)
{
    ch_->pushLine(line);
    sentAt_ = millis();
}

void Link::run()
{
    while (running_)
    {
        std::string line;
        if (ch_->popLine(line, 2))
        {
            lastRx_ = millis() | 1; // 0 = never heard
            if (!line.empty())
                handleLine(line);
            continue;
        }
        if (!busy_)
            startNext();
        else if (millis() - sentAt_ > cur_.timeoutMs)
        {
            if (cur_.isUpload)
                writeLine("ABORT");
            finish(false, "no answer from the controller", true);
        }
    }
}

void Link::startNext()
{
    {
        std::lock_guard<std::mutex> l(mutex_);
        if (queue_.empty())
            return;
        cur_ = std::move(queue_.front());
        queue_.pop_front();
        busy_ = true;
        longWait_ = cur_.timeoutMs > 10000;
    }
    reply_ = Reply();
    upPos_ = 0;
    upCrc_ = 0xFFFFFFFFu;
    upStage_ = 0;
    writeLine(cur_.line);
}

void Link::finish(bool ok, const std::string &error, bool timeout)
{
    reply_.ok = ok;
    reply_.error = error;
    reply_.timeout = timeout;
    ReplyFn fn = cur_.fn;
    Reply r = std::move(reply_);
    busy_ = false;
    longWait_ = false;
    cur_ = Request();
    if (fn)
        fn(r);
}

static uint32_t crcAdd(uint32_t crc, uint8_t c)
{
    crc ^= c;
    for (int k = 0; k < 8; k++)
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    return crc;
}

void Link::uploadStep()
{
    const std::string &s = cur_.content;
    if (upPos_ >= s.size())
    {
        char end[16];
        snprintf(end, sizeof(end), "END %08X", (unsigned)(upCrc_ ^ 0xFFFFFFFFu));
        upStage_ = 2;
        writeLine(end);
        return;
    }
    size_t eol = s.find('\n', upPos_);
    if (eol == std::string::npos)
        eol = s.size();
    std::string line = s.substr(upPos_, eol - upPos_);
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    upPos_ = eol + 1;
    for (char c : line)
        upCrc_ = crcAdd(upCrc_, (uint8_t)c);
    upCrc_ = crcAdd(upCrc_, '\n');
    upStage_ = 1;
    writeLine("|" + line);
    if (cur_.progress)
        cur_.progress(std::min(upPos_, s.size()), s.size());
}

void Link::handleLine(const std::string &line)
{
    if (line[0] == '!')
    {
        const Record ev = Record::parse(line);
        for (auto &fn : listeners_)
            fn(ev);
        return;
    }
    if (!busy_)
        return;
    const bool isOk = line == "ok" || line.rfind("ok ", 0) == 0;
    const bool isError = line.rfind("error", 0) == 0;
    if (isError)
    {
        finish(false, line.size() > 6 ? line.substr(6) : "error");
        return;
    }
    if (cur_.isUpload && upStage_ < 2)
    {
        if (isOk)
            uploadStep();
        return;
    }
    if (isOk)
        finish(true, "");
    else
        reply_.records.push_back(Record::parse(line));
}
