#pragma once

#include <string>
#include <sys/types.h>

class TcpSocket {
private:
    int sock_fd_; 

    explicit TcpSocket(int fd);

public:
    TcpSocket();  // 생성자
    ~TcpSocket(); // 소멸자

    // 복사 금지
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    // Move
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    // 시스템 콜 래핑
    bool Bind(int port);
    bool Listen(int backlog = 5);
    TcpSocket Accept();
    bool Connect(const std::string& ip, int port);
    
    ssize_t Send(const void* buf, size_t len);
    ssize_t Recv(void* buf, size_t len);

    // 유틸리티 메서드
    bool IsValid() const { return sock_fd_ >= 0; }
    int GetFd() const { return sock_fd_; }
};