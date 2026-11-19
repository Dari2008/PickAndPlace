#include "tcp_server.h"
#include "../ctrl/protocol/ports.h"

#include <arpa/inet.h>
#include <cerrno>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

bool TcpServer::start(const std::string &bind, int port)
{
    fd_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0)
        return false;
    int one = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, bind.c_str(), &a.sin_addr) != 1 || ::bind(fd_, (sockaddr *)&a, sizeof(a)) < 0 ||
        listen(fd_, 8) < 0)
    {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    running_ = true;
    t_ = std::thread(&TcpServer::acceptLoop, this);
    return true;
}

void TcpServer::stop()
{
    running_ = false;
    if (fd_ >= 0)
        shutdown(fd_, SHUT_RDWR);
    if (t_.joinable())
        t_.join();
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
}

static void serveClient(int c)
{
    auto ch = std::make_shared<Channel>("tcp");
    ports.add(ch);
    int one = 1;
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    // Writer: everything the controller writes into the channel goes to the socket
    std::thread writer([c, ch] {
        std::string line;
        while (!ch->closed())
        {
            if (!ch->popLine(line, 200))
                continue;
            line += '\n';
            size_t done = 0;
            while (done < line.size())
            {
                const ssize_t n = send(c, line.data() + done, line.size() - done, MSG_NOSIGNAL);
                if (n <= 0)
                {
                    ch->close();
                    return;
                }
                done += (size_t)n;
            }
        }
    });
    std::string buf;
    char tmp[4096];
    while (!ch->closed())
    {
        pollfd p{c, POLLIN, 0};
        if (poll(&p, 1, 500) <= 0)
            continue;
        const ssize_t n = recv(c, tmp, sizeof(tmp), 0);
        if (n <= 0)
            break;
        buf.append(tmp, (size_t)n);
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos)
        {
            std::string l = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (!l.empty() && l.back() == '\r')
                l.pop_back();
            ch->pushLine(l);
        }
        if (buf.size() > 1 << 20)
            break; // no line end in 1 MB: not our protocol
    }
    ch->close();
    writer.join();
    ::close(c);
}

void TcpServer::acceptLoop()
{
    while (running_)
    {
        pollfd p{fd_, POLLIN, 0};
        if (poll(&p, 1, 500) <= 0)
            continue;
        const int c = accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0)
            continue;
        std::thread(serveClient, c).detach();
    }
}
