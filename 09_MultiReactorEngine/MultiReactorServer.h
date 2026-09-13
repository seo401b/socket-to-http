#ifndef MULTI_REACTOR_SERVER_H
#define MULTI_REACTOR_SERVER_H

#include "TcpSocket.h"
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <sys/epoll.h>
#include <unistd.h>

class MultiReactorServer {
public:
    using ReadCallback = std::function<void(TcpSocket& client, const char* buffer, size_t len)>;
    using ConnectCallback = std::function<void(TcpSocket& client)>;
    using DisconnectCallback = std::function<void(int fd)>;

    explicit MultiReactorServer(int port, int thread_count = 0);
    ~MultiReactorServer();

    void setOnRead(ReadCallback cb) { on_read_ = std::move(cb); }
    void setOnConnect(ConnectCallback cb) { on_connect_ = std::move(cb); }
    void setOnDisconnect(DisconnectCallback cb) { on_disconnect_ = std::move(cb); }

    void start();
    void stop();

private:
    void workerLoop(int thread_id);

    int port_;
    int thread_count_;
    std::atomic<bool> running_{false};
    std::vector<std::thread> workers_;

    ReadCallback on_read_;
    ConnectCallback on_connect_;
    DisconnectCallback on_disconnect_;
};

#endif