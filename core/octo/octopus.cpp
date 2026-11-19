#include "octopus.h"
#include "octo_sim.h"
#include "../hal/serial.h"
#include "../compat/Arduino.h"

#include <cstring>
#include <strings.h>

Octopus octo;

const char *const OCTO_AXIS_NAME[OCTO_AXES] = {"x", "y", "z", "rot", "paste", "e0", "e1", "x2"};

const Record *OctoReply::first(const char *type) const
{
    for (const Record &r : records)
        if (r.type == type)
            return &r;
    return nullptr;
}

// ---- serial transport -------------------------------------------------------------------
namespace
{
    class SerialTransport : public OctoTransport
    {
    public:
        explicit SerialTransport(std::string dev) : dev_(std::move(dev)) {}
        bool open() override
        {
            buf_.clear();
            return port_.open(dev_, 115200);
        }
        void close() override { port_.close(); }
        bool write(const std::string &line) override { return port_.writeAll(line + "\n"); }
        bool readLine(std::string &line, int timeoutMs, bool &broken) override
        {
            broken = false;
            for (;;)
            {
                const size_t nl = buf_.find('\n');
                if (nl != std::string::npos)
                {
                    line = buf_.substr(0, nl);
                    buf_.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    return true;
                }
                char tmp[512];
                const int n = port_.readSome(tmp, sizeof(tmp), timeoutMs);
                if (n < 0)
                {
                    broken = true;
                    return false;
                }
                if (n == 0)
                    return false;
                buf_.append(tmp, (size_t)n);
                timeoutMs = 0;
            }
        }
        std::string name() const override { return dev_; }

    private:
        std::string dev_;
        SerialPort port_;
        std::string buf_;
    };
}

// ---- status parsing ---------------------------------------------------------------------------
void fillOctoStatus(OctoStatus &s, const Record &r)
{
    s.valid = true;
    s.state = r.str("state", "idle");
    for (int a = 0; a < OCTO_AXES; a++)
        s.pos[a] = r.num(OCTO_AXIS_NAME[a], s.pos[a]);
    s.homed = r.flag("homed");
    s.e1Homed = r.flag("e1_homed");
    s.busy = r.flag("busy");
    s.motors = r.flag("motors");
    s.alarm = (int)r.integer("alarm");
    s.alarmText = r.str("alarm_text");
    s.pump = (int)r.integer("pump");
    s.pumpUsed = r.integer("pump_used", 1) != 0; // older firmware: always a pump
    s.valve = r.flag("valve");
    s.camFan = (int)r.integer("cam_fan");
    s.boxFan = (int)r.integer("box_fan");
    s.piFan = (int)r.integer("pi_fan");
    s.lightTop = (int)r.integer("light_top");
    s.lightBottom = (int)r.integer("light_bottom");
    s.inputs = (unsigned)r.integer("in");
    s.nozzleInputs = (unsigned)r.integer("noz");
}

// ---- Octopus ------------------------------------------------------------------------------
int Octopus::defaultTimeout(const std::string &line)
{
    std::string w = line.substr(0, line.find(' '));
    for (char &c : w)
        c = (char)toupper((unsigned char)c);
    if (w == "HOME")
        return 300000;
    if (w == "WAIT" || w == "PROBE" || w == "HALT")
        return 600000;
    if (w == "TMC" || w == "DEFAULTS")
        return 20000;
    return 5000;
}

void Octopus::start(const std::string &device)
{
    stop();
    device_ = device;
    simulated_ = device == "sim";
    if (simulated_)
        tx_.reset(new SimTransport());
    else
        tx_.reset(new SerialTransport(device));
    running_ = true;
    thread_ = std::thread(&Octopus::run, this);
}

void Octopus::stop()
{
    running_ = false;
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
    if (tx_)
        tx_->close();
}

bool Octopus::connected() const
{
    const uint32_t rx = lastRx_;
    return open_ && rx != 0 && millis() - rx < 2000;
}

void Octopus::onEvent(std::function<void(const Record &)> fn)
{
    std::lock_guard<std::mutex> l(m_);
    listeners_.push_back(std::move(fn));
}

OctoStatus Octopus::status()
{
    std::lock_guard<std::mutex> l(m_);
    OctoStatus s = status_;
    if (!connected())
        s.state = "offline";
    return s;
}

std::map<std::string, std::string> Octopus::values()
{
    std::lock_guard<std::mutex> l(m_);
    return values_;
}

void Octopus::setValues(const std::map<std::string, std::string> &v)
{
    std::lock_guard<std::mutex> l(m_);
    values_ = v;
}

std::string Octopus::value(const std::string &key, const std::string &def)
{
    std::lock_guard<std::mutex> l(m_);
    auto it = values_.find(key);
    return it == values_.end() ? def : it->second;
}

float Octopus::valueNum(const std::string &key, float def)
{
    const std::string v = value(key);
    if (v.empty())
        return def;
    char *end;
    const float f = strtof(v.c_str(), &end);
    return end == v.c_str() ? def : f;
}

uint64_t Octopus::send(const std::string &line, int timeoutMs)
{
    auto r = std::make_shared<Request>();
    r->line = line;
    r->timeoutMs = timeoutMs > 0 ? timeoutMs : defaultTimeout(line);
    {
        std::lock_guard<std::mutex> l(m_);
        r->id = nextId_++;
        if (!open_)
        {
            r->done = true;
            r->reply.offline = true;
            r->reply.error = "motion board offline";
        }
        else
            queue_.push_back(r);
        byId_[r->id] = r;
    }
    cv_.notify_all();
    return r->id;
}

bool Octopus::poll(uint64_t ticket, OctoReply &out)
{
    std::lock_guard<std::mutex> l(m_);
    auto it = byId_.find(ticket);
    if (it == byId_.end())
    {
        out = OctoReply();
        out.error = "unknown ticket";
        return true;
    }
    if (!it->second->done)
        return false;
    out = it->second->reply;
    byId_.erase(it);
    return true;
}

OctoReply Octopus::call(const std::string &line, int timeoutMs)
{
    const uint64_t id = send(line, timeoutMs);
    std::unique_lock<std::mutex> l(m_);
    auto it = byId_.find(id);
    std::shared_ptr<Request> r = it->second;
    cv_.wait(l, [&] { return r->done || !running_; });
    OctoReply out = r->reply;
    byId_.erase(id);
    return out;
}

// with m_ held
void Octopus::finishHead(bool ok, const std::string &error, bool timeout, bool offline)
{
    if (queue_.empty())
        return;
    auto r = queue_.front();
    queue_.pop_front();
    r->reply.ok = ok;
    r->reply.error = error;
    r->reply.timeout = timeout;
    r->reply.offline = offline;
    r->done = true;
    cv_.notify_all();
}

// with m_ held: put the next request on the wire
void Octopus::pump()
{
    if (queue_.empty() || queue_.front()->sent || !open_)
        return;
    auto r = queue_.front();
    r->sent = true;
    r->sentMs = millis();
    if (!tx_->write(r->line))
        finishHead(false, "write failed", false, true);
}

void Octopus::handleLine(const std::string &line)
{
    if (line.empty())
        return;
    lastRx_ = millis() | 1; // 0 = never heard
    if (line[0] == '!')
    {
        const Record e = Record::parse(line);
        std::vector<std::function<void(const Record &)>> ls;
        {
            std::lock_guard<std::mutex> l(m_);
            if (e.type == "status")
            {
                fillOctoStatus(status_, e);
                status_.rxMs = millis();
                status_.seq++;
            }
            else if (e.type == "ready")
                initPending_ = true; // the board restarted: settings are back to defaults
            ls = listeners_;
        }
        for (auto &fn : ls)
            fn(e);
        return;
    }
    std::lock_guard<std::mutex> l(m_);
    if (queue_.empty() || !queue_.front()->sent)
        return; // stray line (e.g. after a timeout)
    if (line == "ok")
        finishHead(true, "", false, false);
    else if (line.compare(0, 6, "error ") == 0 || line == "error")
        finishHead(false, line.size() > 6 ? line.substr(6) : "error", false, false);
    else
    {
        const Record rec = Record::parse(line);
        if (rec.type == "status")
        {
            fillOctoStatus(status_, rec);
            status_.rxMs = millis();
            status_.seq++;
        }
        queue_.front()->reply.records.push_back(rec);
    }
    pump();
}

void Octopus::run()
{
    uint32_t lastTry = 0;
    while (running_)
    {
        if (!open_)
        {
            if (lastTry && millis() - lastTry < 1000)
            {
                delay(50);
                continue;
            }
            lastTry = millis();
            if (tx_->open())
            {
                open_ = true;
                lastRx_ = millis() | 1; // the first REPORT shows it really answers (0 = never heard)
                initPending_ = true;
            }
            else
            {
                std::lock_guard<std::mutex> l(m_);
                while (!queue_.empty())
                    finishHead(false, "motion board offline", false, true);
                continue;
            }
        }
        {
            std::lock_guard<std::mutex> l(m_);
            pump();
            // Timeout of the request on the wire
            if (!queue_.empty() && queue_.front()->sent &&
                millis() - queue_.front()->sentMs > (uint32_t)queue_.front()->timeoutMs)
            {
                finishHead(false, "timeout", true, false);
                pump();
            }
        }
        std::string line;
        bool broken = false;
        if (tx_->readLine(line, 20, broken))
            handleLine(line);
        else if (broken)
        {
            tx_->close();
            open_ = false;
            std::lock_guard<std::mutex> l(m_);
            status_.state = "offline";
            while (!queue_.empty())
                finishHead(false, "motion board disconnected", false, true);
        }
    }
}
