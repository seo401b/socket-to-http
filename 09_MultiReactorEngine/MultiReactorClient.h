#ifndef MULTI_REACTOR_CLIENT_H
#define MULTI_REACTOR_CLIENT_H

#include "TcpSocket.h"
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <sys/epoll.h>

class MultiReactorClient {
public:
    using ReadCallback = std::function<void(const char* buffer, size_t len)>;
    using ConnectCallback = std::function<void()>;
    using DisconnectCallback = std::function<void()>;

    MultiReactorClient();
    ~MultiReactorClient();

    bool connect(const std::string& ip, int port);
    void send(const char* data, size_t len);
    void send(const std::string& data);
    void disconnect();

    void setOnRead(ReadCallback cb) { on_read_ = std::move(cb); }
    void setOnConnect(ConnectCallback cb) { on_connect_ = std::move(cb); }
    void setOnDisconnect(DisconnectCallback cb) { on_disconnect_ = std::move(cb); }

private:
    void ioLoop();

    TcpSocket socket_;
    int epfd_{-1};
    std::atomic<bool> running_{false};
    std::thread io_thread_;

    ReadCallback on_read_;
    ConnectCallback on_connect_;
    DisconnectCallback on_disconnect_;
};

#endif