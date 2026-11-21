/**
 * octopus.h - link from the Pi to the Octopus motion board (USB CDC, text protocol)
 * -----------------------------------------------------------------------------
 *   octo.start("/dev/ttyACM0");             // or "sim": built-in simulator (no hardware)
 *   OctoReply r = octo.call("MOVE x=10 speed=50");          // blocks until ok / error
 *   OctoStatus s = octo.status();           // from "!status" every 50 ms (REPORT 50)
 *
 * One command is on the wire at a time; call() is thread-safe (callers queue up).
 * A reader thread collects answers and events. When the port (re)opens,
 * initPending() turns true: the controller then sends REPORT and the machine
 * settings (pap-core main loop).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../link/record.h"

struct OctoReply
{
    bool ok = false;
    bool timeout = false;
    bool offline = false;
    std::string error;
    std::vector<Record> records;
    const Record *first(const char *type) const;
};

// Axis order of the Octopus (octopus-fw/src/pap_board.h)
enum OctoAxis
{
    OX = 0, OY, OZ, OROT, OPASTE, OE0, OE1, OS, OCTO_AXES
};
extern const char *const OCTO_AXIS_NAME[OCTO_AXES];

struct OctoStatus
{
    bool valid = false;
    std::string state = "offline"; // idle moving homing alarm offline
    float pos[OCTO_AXES] = {0};
    bool homed = false, e1Homed = false, busy = false, motors = false;
    int alarm = 0;
    std::string alarmText;
    bool pumpUsed = true; // pump_used: a vacuum pump is connected (FAN5)
    int pump = 0, camFan = 0, boxFan = 0, piFan = 0, lightTop = 0, lightBottom = 0; // fans: always on (cam_fan / box_fan / pi_fan)
    bool valve = false;
    unsigned inputs = 0;  // bit n = DIAGn hit
    unsigned nozzleInputs = 0; // bit n = nozzle slot n+1 sensor signal HIGH (raw, tool changer board on the TFT header)
    uint32_t rxMs = 0;    // millis() when received
    uint64_t seq = 0;     // counts received status lines
};

// Where lines go and come from: the USB serial port or the simulator
class OctoTransport
{
public:
    virtual ~OctoTransport() {}
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool write(const std::string &line) = 0;
    // Next complete line (without '\n'), waiting up to timeoutMs. false + broken = port lost.
    virtual bool readLine(std::string &line, int timeoutMs, bool &broken) = 0;
    virtual std::string name() const = 0;
};

class Octopus
{
public:
    ~Octopus() { stop(); }
    void start(const std::string &device); // "/dev/ttyACM0" or "sim"
    void stop();

    bool connected() const;
    bool simulated() const { return simulated_; }
    std::string device() const { return device_; }

    OctoReply call(const std::string &line, int timeoutMs = 0); // 0 = default for the command
    // Non-blocking: the answer is fetched with poll(ticket, reply) later.
    uint64_t send(const std::string &line, int timeoutMs = 0);
    bool poll(uint64_t ticket, OctoReply &out);
    // HALT / ESTOP past the queue: written at once, even while a blocking command (HOME,
    // SGTEST, PROBE) is on the wire - the board polls for these while it blocks. Its answer
    // is expected right after the one on the wire and is dropped.
    void urgent(const std::string &line);
    // Queued like send(), but nobody waits for or fetches the answer (JOGV from the joystick)
    void post(const std::string &line, int timeoutMs = 0);

    OctoStatus status();
    void onEvent(std::function<void(const Record &)> fn);

    bool initPending() const { return initPending_; }
    void initDone() { initPending_ = false; }

    // Values of the last "GET" (machine settings of the Octopus)
    std::map<std::string, std::string> values();
    void setValues(const std::map<std::string, std::string> &v);
    std::string value(const std::string &key, const std::string &def = "");
    float valueNum(const std::string &key, float def);

    static int defaultTimeout(const std::string &line);

private:
    struct Request
    {
        uint64_t id;
        std::string line;
        int timeoutMs;
        uint32_t sentMs = 0;
        bool sent = false;
        bool done = false;
        OctoReply reply;
    };
    void run();
    void handleLine(const std::string &line);
    void pump(); // send the next request if none is on the wire
    void finishHead(bool ok, const std::string &error, bool timeout, bool offline);

    std::unique_ptr<OctoTransport> tx_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> open_{false};
    std::atomic<bool> initPending_{false};
    bool simulated_ = false;
    std::string device_;

    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<Request>> queue_; // head = on the wire
    std::map<uint64_t, std::shared_ptr<Request>> byId_;
    uint64_t nextId_ = 1;
    OctoStatus status_;
    std::map<std::string, std::string> values_;
    std::vector<std::function<void(const Record &)>> listeners_;
    std::atomic<uint32_t> lastRx_{0};
    // Last line written. Nothing for KEEPALIVE_MS while idle: a PING, so the board's link watchdog
    // (watchdog_ms) doesn't stop the pump / valve / a move while the Pi just has nothing to say.
    uint32_t lastTx_ = 0;
    static const uint32_t KEEPALIVE_MS = 500;
};

extern Octopus octo;

// Parse "!status ..." / "status ..." fields
void fillOctoStatus(OctoStatus &s, const Record &r);
