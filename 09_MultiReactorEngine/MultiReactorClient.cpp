#include "MultiReactorClient.h"
#include <iostream>
#include <unistd.h>
#include <errno.h>

MultiReactorClient::MultiReactorClient(){}

MultiReactorClient::~MultiReactorClient() {
    disconnect();
}

bool MultiReactorClient::connect(const std::string& ip, int port){
    if(running_.load()) return false;

    if (!socket_.Connect(ip, port)) {
        return false;
    }

    socket_.SetNonBlocking();

    epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if(epfd_ < 0){
        return false;
    }

    ::epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = socket_.GetFd();

    if(::epoll_ctl(epfd_, EPOLL_CTL_ADD, socket_.GetFd(), &ev) < 0){
        ::close(epfd_);
        epfd_ = -1;
        return false;
    }

    running_.store(true);
    io_thread_ = std::thread(&MultiReactorClient::ioLoop, this);

    if(on_connect_) on_connect_();
    return true;
}

void MultiReactorClient::send(const char* data, size_t len){
    if(!running_.load()) return;

    ssize_t total_sent = 0;
    while(total_sent < len){
        ssize_t sent = socket_.Send(data + total_sent, len - total_sent);
        if(sent > 0){
            total_sent += sent;
        }
        else if(sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            break;
        }
        else{
            break;
        }
    }
}

void MultiReactorClient::send(const std::string& data){
    send(data.c_str(), data.size());
}

void MultiReactorClient::disconnect(){
    if(running_.exchange(false)){
        if(epfd_ >= 0){
            ::close(epfd_);
            epfd_ = -1;
        }
        if(io_thread_.joinable()){
            io_thread_.join();
        }
    }
}


void MultiReactorClient::ioLoop() {
    ::epoll_event cev[10];

    while(running_.load()){
        int nfds = ::epoll_wait(epfd_, cev, 10, 100);
        if(nfds < 0){
            if(errno == EINTR) continue;
            break;
        }

        for(int i = 0; i < nfds; ++i){
            if(cev[i].events & EPOLLIN){
                char buf[1024] = {0,};
                bool is_closed = false;

                while(true){
                    ssize_t len = socket_.Recv(buf, sizeof(buf) - 1);
                    if(len > 0){
                        buf[len] = '\n';
                        if(on_read_) on_read_(buf, len);
                    }
                    else if(len == 0){
                        is_closed = true;
                        break;
                    }
                    else{
                        if(errno == EAGAIN || errno == EWOULDBLOCK) break;
                        is_closed = true;
                        break;
                    }
                }
                if(is_closed){
                    running_.store(false);
                    if(on_disconnect_) on_disconnect_();
                    return;
                }
            }
        }
    }
}