/**
 * ports.h - command ports of the Pi controller: one per client (display UI, website, console)
 * -----------------------------------------------------------------------------
 * On the ATmega a port was a serial interface. On the Pi every client gets a
 * Channel: a thread-safe pipe of text lines (protocol.md). The controller thread
 * reads commands from it and writes answers and events into it; the client
 * (UI thread, TCP connection of pap-web, stdin) does the other end.
 *
 *   auto ch = std::make_shared<Channel>("ui");
 *   ports.add(ch);                       // the controller serves it from now on
 *   ch->pushLine("STATUS");              // client: send a command
 *   std::string l; ch->popLine(l, 100);  // client: next answer / event line
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <Arduino.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define LINE_MAX 4096
#define PATH_MAX_LEN 192

extern uint8_t linkLogLevel; // LOG command (kept for compatibility)

class Channel
{
public:
    explicit Channel(std::string name) : name(std::move(name)) {}
    const std::string name;

    // client side
    void pushLine(const std::string &line);
    bool popLine(std::string &line, int timeoutMs); // false on timeout / closed
    void close();
    bool closed() const { return closed_; }

    // controller side
    bool nextLine(std::string &line);
    void write(const char *data, size_t n);

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::string> in_;
    std::deque<std::string> out_;
    std::string partial_;
    std::atomic<bool> closed_{false};
};

// Print into a channel
class ChannelPrint : public Print
{
public:
    explicit ChannelPrint(Channel &c) : ch(c) {}
    size_t write(uint8_t c) override
    {
        const char ch1 = (char)c;
        ch.write(&ch1, 1);
        return 1;
    }
    size_t write(const uint8_t *b, size_t n) override
    {
        ch.write((const char *)b, n);
        return n;
    }
    using Print::write;
    Channel &ch;
};

class Port
{
public:
    explicit Port(std::shared_ptr<Channel> c) : channel(std::move(c)), printer_(*channel), io(printer_) {}
    void poll();
    void answerWhenIdle() { waitIdle_ = true; }
    std::shared_ptr<Channel> channel;

private:
    ChannelPrint printer_;

public:
    Print &io;
    char cwd[PATH_MAX_LEN] = "/";
    uint16_t reportMs = 0;

private:
    bool waitIdle_ = false;
    uint32_t lastReport_ = 0;
};

// Print that writes to every port (events)
class AllPorts : public Print
{
public:
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;
};

class Ports
{
public:
    void add(std::shared_ptr<Channel> ch);
    void poll(); // read input, execute commands, send due reports; drops closed channels
    AllPorts all;
    size_t count();

private:
    friend class AllPorts;
    std::mutex m_;
    std::vector<std::unique_ptr<Port>> list_;
    std::vector<std::shared_ptr<Channel>> pending_;
};

extern Ports ports;
