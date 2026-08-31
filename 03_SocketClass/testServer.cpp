#include "TcpSocket.h"
#include <iostream>
#include <cstring>

int main(){
    TcpSocket server;

    try {
        server.Bind(8080);
        server.Listen(5);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    std::cout << "Server Waiting(8080)\n";

    char buf[1024] = {0,};
    TcpSocket client = server.Accept();

    while(1){
        if(!client.IsValid()) continue;

        std::memset(buf, 0, sizeof(buf));
        ssize_t len = client.Recv(buf, sizeof(buf) - 1);
        

        if(len>0){
            buf[len] = '\n';
            std::cout << "client: " << buf << '\n';

            client.Send(buf, len);
        } else if(len == 0){
            std::cout << "client disconnect\n";
            break;
        } else {
            std::cerr << "recv err\n";
            break;
        }
    }
    return 0;
}