#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <iostream>

int main() {
    //create sock
    int client_sock = socket(AF_INET, SOCK_STREAM, 0);

    //server addr setting (IP, Port)
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    //loop back host 127.0.0.1
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(12345);

    //connect request to server
    connect (client_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    std::cout << "connect success\n";
    
    //send
    const char* msg = "Hello Server - Client-\n";
    write(client_sock, msg, std::strlen(msg));

    //recv
    char buffer[1024] = {0,};
    ssize_t recv_len = read(client_sock, buffer, sizeof(buffer) - 1);
    if(recv_len > 0){
        std::cout << buffer << '\n';
    }
    //free
    close(client_sock);
    return 0;
}