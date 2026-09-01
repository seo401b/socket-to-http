#include "TcpSocket.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

std::atomic<int> active_clients{0};

void HandleClient(TcpSocket client){
    active_clients++;
    char buf[1024];
    int msg_cnt = 0;

    while(client.IsValid()){
        ssize_t len = client.Recv(buf, sizeof(buf)-1);
        if(len <= 0) break;

        auto start = std::chrono::high_resolution_clock::now();
        client.Send(buf, len); //Echo
        auto end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end-start).count();
        msg_cnt++;

        if (latency > 10000 || msg_cnt%500 == 0){
            std::cout << "Active Threads: " << active_clients.load() << '\n'
                      << "Latency: " << latency/1000.0 << " ms\n";
        }
    }
    active_clients--;
}

int main(){
    TcpSocket server;

    if(!server.Bind(8080) || !server.Listen(5000)){
        std::cerr << "server setup fail\n";
        return 1;
    }
    std::cout << "Latency Server Running(8080)\n";

    while(1){
        TcpSocket client = server.Accept();
        if(!client.IsValid()) continue;
        
        std::thread t(HandleClient, std::move(client));
        t.detach();
    }
    return 0;
}