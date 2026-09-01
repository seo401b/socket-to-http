#include "TcpSocket.h"
#include <iostream>
#include <cstring>
#include <thread>


int main(){
    TcpSocket server;

    if(!server.Bind(8080) || !server.Listen(100000)){
        return 1;
    }

    std::cout << "Server Waiting(8080)\n";

    while(1){
        TcpSocket client = server.Accept();
        if(!client.IsValid()) continue;

        std::thread worker_thread([client = std::move(client)]()mutable {
            char buf[1024];

            while(client.IsValid()){
                ssize_t len = client.Recv(buf, sizeof(buf) - 1);
                
                if(len>0){
                    client.Send(buf, len);
                } else {
                    break;
                }
            }
        });
        worker_thread.detach();
    }

    return 0;
}