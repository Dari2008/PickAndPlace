/**
 * tcp_server.h - the controller protocol over TCP (for pap-web and service tools)
 * -----------------------------------------------------------------------------
 * Every TCP connection gets its own command port (Channel): send a command line,
 * read the records and "ok" / "error ..." back, events ("!status" ...) arrive at
 * any time once "REPORT <ms>" is set. Same protocol as protocol.md.
 *   nc 127.0.0.1 7070   ->  STATUS
 * Only 127.0.0.1 by default (pap-web does the login); --api-bind 0.0.0.0 to open it.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <atomic>
#include <string>
#include <thread>

class TcpServer
{
public:
    bool start(const std::string &bind, int port);
    void stop();

private:
    void acceptLoop();
    int fd_ = -1;
    std::thread t_;
    std::atomic<bool> running_{false};
};
