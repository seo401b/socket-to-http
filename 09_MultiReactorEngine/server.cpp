#include "MultiReactorServer.h"
#include <iostream>

int main() {
    MultiReactorServer server(8080);

    server.setOnConnect([](TcpSocket& client) {
        std::cout << "Connect FD: " << client.GetFd() << '\n';
    });

    server.setOnRead([](TcpSocket& client, const char* buffer, size_t len) {
        std::cout << "Read FD: " << client.GetFd()  << buffer;
        
        client.Send(buffer, len);
    });

    server.setOnDisconnect([](int fd) {
        std::cout << "Disconnect FD: " << fd << '\n';
    });

    server.start();

    return 0;
}