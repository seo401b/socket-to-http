#include "MultiReactorServer.h"
#include <iostream>
#include <errno.h>

MultiReactorServer::MultiReactorServer(int port, int thread_count)
    :port_(port), thread_count_(thread_count) {
    if(thread_count_ <= 0){
        thread_count_ = std::thread::hardware_concurrency();
        if(thread_count_ == 0) thread_count_ = 1;
    }
}

MultiReactorServer::~MultiReactorServer(){
    stop();
}

void MultiReactorServer::start(){
    running_.store(true);
    for(int i = 0; i < thread_count_; ++i){
        workers_.emplace_back(&MultiReactorServer::workerLoop, this, i);
    }


    for(auto& t : workers_){
        if(t.joinable()) t.join();
    }
}

void MultiReactorServer::stop() {
    if (running_.exchange(false)) {
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
    }
}

void MultiReactorServer::workerLoop(int thread_id) {
    std::unordered_map<int, TcpSocket> clients_map;

    TcpSocket listen_sock;
    if (!listen_sock.Bind(port_) || !listen_sock.Listen(1000000) || !listen_sock.SetNonBlocking()) {
        return;
    }

    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        return;
    }

    ::epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_sock.GetFd();

    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock.GetFd(), &ev) < 0) {
        ::close(epfd);
        return;
    }

    ::epoll_event cev[1024];

    // 비동기 종료 타임아웃 100ms 추가
    while (running_.load()) {
        int nfds = ::epoll_wait(epfd, cev, 1024, 100);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int event_fd = cev[i].data.fd;

            // 1. New Connection
            if (event_fd == listen_sock.GetFd()) {
                TcpSocket client_sock = listen_sock.Accept();
                if (client_sock.GetFd() < 0) continue;

                client_sock.SetNonBlocking();
                int client_fd = client_sock.GetFd();

                ::epoll_event cev_client{};
                cev_client.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                cev_client.data.fd = client_fd;

                if (::epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev_client) >= 0) {
                    if (on_connect_) on_connect_(client_sock);
                    clients_map.emplace(client_fd, std::move(client_sock));
                }
            }
            // 2. Read Event
            else if (cev[i].events & EPOLLIN) {
                auto it = clients_map.find(event_fd);
                if (it == clients_map.end()) continue;

                TcpSocket& client_sock = it->second;
                char buf[1024] = {0,};
                bool is_closed = false;

                while (true) {
                    ssize_t len = client_sock.Recv(buf, sizeof(buf) - 1);
                    if (len > 0) {
                        buf[len] = '\0';
                        if (on_read_) {
                            on_read_(client_sock, buf, len);
                        }
                    }
                    else if (len == 0) {
                        is_closed = true;
                        break;
                    }
                    else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        is_closed = true;
                        break;
                    }
                }

                if (is_closed) {
                    if (on_disconnect_) on_disconnect_(event_fd);
                    ::epoll_ctl(epfd, EPOLL_CTL_DEL, event_fd, nullptr);
                    clients_map.erase(event_fd);
                }
                else {
                    ::epoll_event cev_mod{};
                    cev_mod.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    cev_mod.data.fd = event_fd;
                    ::epoll_ctl(epfd, EPOLL_CTL_MOD, event_fd, &cev_mod);
                }
            }
        }
    }

    ::close(epfd);
}