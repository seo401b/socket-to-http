#include "TcpSocket.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <sys/epoll.h>
#include <errno.h>
#include <unordered_map>
#include <vector>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <functional>

bool stop_server = 0;

void socket (int thread_id){
    std::unordered_map<int, TcpSocket> clients_map;

    TcpSocket listen_sock;
    if (!listen_sock.Bind(8080) || !listen_sock.Listen(1000000) || !listen_sock.SetNonBlocking()) {
        std::cerr << "Server setup fail\n";
        return;
    } // Bind opt so_reuseport로 변경 TcpSocket에서 직접 변경함

    std::cout << "Server Waiting(8080)\n";

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if(epfd < 0) {
        std::cerr << "epfd create fail\n";
        return;
    }

    ::epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_sock.GetFd();

    if(::epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock.GetFd(), &ev) < 0){
        std::cerr << "epoll add fail\n";
        return;
    }

    ::epoll_event cev[1024];

    while(!stop_server){
        int nfds = ::epoll_wait(epfd, cev, 1024, -1);
        if(nfds < 0){
            if(errno == EINTR) continue;
            break;
        }

        for(int i = 0; i < nfds; ++i){
            if(cev[i].data.fd == listen_sock.GetFd()){
                TcpSocket client_sock = listen_sock.Accept();
                client_sock.SetNonBlocking();

                ::epoll_event cev_client{};
                cev_client.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                cev_client.data.fd = client_sock.GetFd();
                if(::epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock.GetFd(), &cev_client) < 0){
                    std::cerr << "epoll add fail\n";
                    return;
                }
                clients_map.emplace(client_sock.GetFd(), std::move(client_sock));
            }

            else if(cev[i].events & EPOLLIN){
                int client_fd = cev[i].data.fd;

                auto it = clients_map.find(client_fd);
                if(it==clients_map.end()) return;

                TcpSocket& client_sock = it->second;

                char buf[1024] = {0,};
                bool is_closed = 0;

                while(1){
                    ssize_t len = client_sock.Recv(buf, sizeof(buf) - 1);
                    if (len > 0){
                        buf[len] = '\0';
                        std::cout << "client: " << buf << '\n';

                        client_sock.Send(buf, len);
                    }
                    else if(len == 0){
                        is_closed = true;
                        break;
                    }
                    else{
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            break;
                        }
                        is_closed = 1;
                        break;
                    }
                }

                if(is_closed){
                    ::epoll_ctl(epfd, EPOLL_CTL_DEL, client_fd, nullptr);
                    clients_map.erase(client_fd);
                }
                else{
                    ::epoll_event cev_mod{};
                    cev_mod.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    cev_mod.data.fd = client_fd;
                    ::epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &cev_mod);
                }
            }
        }
    }
}

int main(){
    unsigned int core_num = std::thread::hardware_concurrency();

    if(core_num == 0) core_num = 4; //내가 4core를 쓰기 때문.

    std::vector<std::thread> workers;
    for(int i = 0; i < core_num; ++i){
        workers.emplace_back(socket, i);
    }

    for(auto& t : workers){
        if(t.joinable()) t.join();
    }

    return 0;
}