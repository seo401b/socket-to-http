#include "TcpSocket.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <sys/epoll.h>
#include <errno.h>
#include <unordered_map>

std::unordered_map<int, TcpSocket> client_map;

int main(){
    TcpSocket server;

    if (!server.Bind(8080) || !server.Listen(1000000) || !server.SetNonBlocking()) {
        std::cerr << "Server setup fail\n";
        return 1;
    }

    std::cout << "Server Waiting(8080)\n";

    int epfd = ::epoll_create1(EPOLL_CLOEXEC); //Atomic ctrl
    if(epfd < 0) {
        std::cerr << "epfd create fail\n";
        return 1;
    }

    ::epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server.GetFd();

    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, server.GetFd(), &ev) < 0) {
        std::cerr << "epoll add fail\n";
        return 1;
    }

    ::epoll_event events[1024];

    char buf[1024] = {0,};

    while(1){
        int nfds = ::epoll_wait(epfd, events, 1024, -1);
        if(nfds < 0){
            if(errno == EINTR) continue;
            break;
        }

        for(int i = 0; i < nfds; ++i){
            int tmp_fd = events[i].data.fd;

            if(tmp_fd == server.GetFd()){
                TcpSocket client = server.Accept();
                client.SetNonBlocking();
                ev.events = EPOLLIN | EPOLLET; // 새로운 요청 -> Edge Trig 전환
                ev.data.fd = client.GetFd();
                if (::epoll_ctl(epfd, EPOLL_CTL_ADD, client.GetFd(), &ev) < 0) {
                    std::cerr << "epoll add fail\n";
                    return 1;
                }

                client_map.emplace(client.GetFd(), std::move(client));
            }
        

            else if(events[i].events & EPOLLIN){
                std::memset(buf, 0, sizeof(buf));
                auto it = client_map.find(tmp_fd);
                if(it == client_map.end()) continue;

                TcpSocket& client = it -> second;

                while(1){
                    ssize_t len = client.Recv(buf, sizeof(buf)-1);
                    if(len > 0){
                        buf[len] = '\0';
                        std::cout << "client: " << buf << '\n';

                        client.Send(buf, len);
                    } else if(len == 0){
                        if(::epoll_ctl(epfd, EPOLL_CTL_DEL, client.GetFd(), nullptr) < 0){
                            std::cerr << "epoll del fail\n";
                            return 1;
                        }
                        client_map.erase(it); //Destructor call
                        break;
                    } else {
                        if(errno == EAGAIN || errno == EWOULDBLOCK) break;
                        ::epoll_ctl(epfd, EPOLL_CTL_DEL, tmp_fd, nullptr);
                        client_map.erase(it);
                        break;
                    }
                }
            }
        }
    }
    return 0;
}