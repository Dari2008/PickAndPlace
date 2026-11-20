#include "ports.h"
#include <motion.h>
#include "commands.h"

#include <chrono>

Ports ports;
uint8_t linkLogLevel = 1;

// ---- Channel ----------------------------------------------------------------------------------
void Channel::pushLine(const std::string &line)
{
    commands::urgentLine(line); // HALT / ESTOP can't wait behind a blocking HOME
    std::lock_guard<std::mutex> l(m_);
    in_.push_back(line);
}

bool Channel::popLine(std::string &line, int timeoutMs)
{
    std::unique_lock<std::mutex> l(m_);
    if (!cv_.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return !out_.empty() || closed_; }))
        return false;
    if (out_.empty())
        return false;
    line = std::move(out_.front());
    out_.pop_front();
    return true;
}

void Channel::close()
{
    closed_ = true;
    cv_.notify_all();
}

bool Channel::nextLine(std::string &line)
{
    std::lock_guard<std::mutex> l(m_);
    if (in_.empty())
        return false;
    line = std::move(in_.front());
    in_.pop_front();
    return true;
}

void Channel::write(const char *data, size_t n)
{
    std::lock_guard<std::mutex> l(m_);
    for (size_t i = 0; i < n; i++)
    {
        const char c = data[i];
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            out_.push_back(std::move(partial_));
            partial_.clear();
        }
        else
            partial_ += c;
    }
    // Slow or gone client: don't pile up forever
    while (out_.size() > 20000)
        out_.pop_front();
    cv_.notify_all();
}

// ---- Port ---------------------------------------------------------------------------------------
void Port::poll()
{
    if (waitIdle_)
    {
        if (motion.isBusy())
            return;
        waitIdle_ = false;
        io.println(F("ok"));
    }
    std::string line;
    while (channel->nextLine(line))
    {
        if (line.size() > LINE_MAX)
        {
            io.println(F("error line too long"));
            continue;
        }
        std::vector<char> buf(line.begin(), line.end());
        buf.push_back('\0');
        commands::execute(*this, buf.data());
        if (waitIdle_)
            return;
    }
    if (reportMs && millis() - lastReport_ >= reportMs)
    {
        lastReport_ = millis();
        commands::report(io);
    }
}

// ---- Ports ----------------------------------------------------------------------------------------
void Ports::add(std::shared_ptr<Channel> ch)
{
    std::lock_guard<std::mutex> l(m_);
    pending_.push_back(std::move(ch));
}

size_t Ports::count()
{
    std::lock_guard<std::mutex> l(m_);
    return list_.size() + pending_.size();
}

void Ports::poll()
{
    {
        std::lock_guard<std::mutex> l(m_);
        for (auto &ch : pending_)
            list_.emplace_back(new Port(ch));
        pending_.clear();
    }
    for (size_t i = 0; i < list_.size(); i++)
        list_[i]->poll();
    std::lock_guard<std::mutex> l(m_);
    for (size_t i = 0; i < list_.size();)
        if (list_[i]->channel->closed())
            list_.erase(list_.begin() + i);
        else
            i++;
}

size_t AllPorts::write(uint8_t c)
{
    for (auto &p : ports.list_)
        p->io.write(c);
    return 1;
}

size_t AllPorts::write(const uint8_t *buf, size_t size)
{
    for (auto &p : ports.list_)
        p->io.write(buf, size);
    return size;
}
