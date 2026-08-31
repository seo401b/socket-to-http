#include "TcpSocket.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <iostream>

// 생성자
TcpSocket::TcpSocket(){
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    // SOCK_REUSEADDR option
    int opt = 1;
    setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

//내부 생성자, 커널이 준 fd를 재포장
TcpSocket::TcpSocket(int fd) : sock_fd_(fd){}

TcpSocket::~TcpSocket(){
    if(sock_fd_ >= 0){
        close(sock_fd_);
    }
}

//Move
TcpSocket::TcpSocket(TcpSocket&& other) noexcept{
    sock_fd_ = other.sock_fd_;
    other.sock_fd_ = -1;
}

//Move operator
TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept{
    if(this != &other){
        if(sock_fd_ >= 0) {
            close(sock_fd_);
        }
        sock_fd_ = other.sock_fd_;
        other.sock_fd_ = -1;
    }
    return *this;
}

//Bind
bool TcpSocket::Bind(int port){
    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if(bind(sock_fd_, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0){
        std::cerr << port << "port binding failed: " << std::strerror(errno) << '\n';
        return false;
    }
    return true;
}

//Listen
bool TcpSocket::Listen(int log){
    if(listen(sock_fd_, log) < 0){
        std::cerr << "Listen failed'\n";
        return false;
    }
    return true;
}

//Accept for client
TcpSocket TcpSocket::Accept(){
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(sock_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
    return TcpSocket(client_fd);
}

//Connect for client
bool TcpSocket::Connect(const std::string& ip, int port){
    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if(inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0){
        std::cerr << "Connect fail\n";
        return  false;
    }

    if(connect(sock_fd_, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0){
        std::cerr << "Connect fail\n";
        return false;
    }
    return true;
}

//Send
ssize_t TcpSocket::Send(const void* buf, size_t len){
    return send(sock_fd_, buf, len, 0);
}

//Recv
ssize_t TcpSocket::Recv(void* buf, size_t len){
    return recv(sock_fd_, buf, len, 0);
}