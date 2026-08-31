#include "TcpSocket.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <mutex>
std::mutex log_mtx; //Mutual Exclusion MUTEX, only one

int main(){
    TcpSocket server;

    if(!server.Bind(8080) || !server.Listen(5)){
        return 1;
    }

    std::cout << "Server Waiting(8080)\n";

    while(1){
        TcpSocket client = server.Accept();
        if(!client.IsValid()) continue;

        std::thread worker_thread([client = std::move(client)]()mutable {
            char buf[1024] = {0,};

            while(1){
                std::memset(buf, 0, sizeof(buf));
                ssize_t len = client.Recv(buf, sizeof(buf) - 1);
                
                if(len>0){
                    buf[len] = '\n';
                    {
                    std::lock_guard<std::mutex> lock(log_mtx);
                    std::cout << "client: " << buf << '\n';
                    }
                    client.Send(buf, len);
                } else if(len == 0){
                    {
                    std::lock_guard<std::mutex> lock(log_mtx);
                    std::cout << "client disconnect\n";
                    }
                    break;
                } else {
                    {
                    std::lock_guard<std::mutex> lock(log_mtx);
                    std::cerr << "recv err\n";
                    }
                    break;
                }
            }
        });
        worker_thread.detach();
    }

    return 0;
}