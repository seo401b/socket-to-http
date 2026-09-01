#include "TcpSocket.h"
#include <iostream>
#include <cstring>
#include <thread>

int main(){
    TcpSocket server;

    if (!server.Bind(8080) || !server.Listen(5) || !server.SetNonBlocking()) {
        std::cerr << "Server setup fail\n";
        return 1;
    }

    std::cout << "Server Waiting(8080)\n";

    TcpSocket client;
    char buf[1024] = {0,};

    while(1){
        if(!client.IsValid()) {
            client = server.Accept();

            if(client.IsValid()){
                client.SetNonBlocking();
                std::cout << "client connect\n";
            } else{
                if(errno != EAGAIN && errno != EWOULDBLOCK){
                    std::cerr << "err: " << std::strerror(errno) << '\n';
                    break;
                }
            }
        }

        if(client.IsValid()){
            std::memset(buf, 0, sizeof(buf));
            ssize_t len = client.Recv(buf, sizeof(buf) - 1);
            

            if(len>0){
                buf[len] = '\n';
                std::cout << "client: " << buf << '\n';

                client.Send(buf, len);
            } else if(len == 0){
                std::cout << "client disconnect\n";
                client = TcpSocket(); // close
            } else {
                if(errno != EAGAIN && errno != EWOULDBLOCK){
                    std::cerr << "recv err\n";
                    client = TcpSocket();
                }
            }
        }
        //CPU 과부화 방지
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}