#include "MultiReactorServer.h"
#include "HttpSession.h"
#include <iostream>
#include <unordered_map>
#include <fstream>
#include <sstream>

std::string handleRequest(const HttpRequest& req){ //Express의 라우터 컨트롤러
    std::string path = (req.path == "/") ? "/index.html" : req.path;
    std::string filepath = "./www" + path;

    std::ifstream file(filepath, std::ios::binary);
    std::string body;
    std::string status;

    if(file.is_open()){
        std::ostringstream ss;
        ss << file.rdbuf();
        body = ss.str();
        status = "200 OK";
    }
    else {
        body = "<html><body><h1>404 Not Found</h1></body></html>";
        status = "404 Not Found";
    }

    std::string response = "HTTP/1.1 " + status + "\r\n" +
                           "Content-Type: text/html; charset=utf-8\r\n" +
                           "Content-Length: " + std::to_string(body.size()) + "\r\n" +
                           "Connection: keep-alive\r\n" +
                           "\r\n" + body;
    return response;
}

int main(){ //Express 프레임워크, 미들웨어 같은 것
    MultiReactorServer server(8080);

    std::unordered_map<int, HttpSession> sessions;

    server.setOnConnect([&](TcpSocket& client){
        int fd = client.GetFd();
        std::cout << "connect Fd: " << fd << '\n';
        sessions.emplace(fd, HttpSession());
    });

    server.setOnRead([&](TcpSocket& client, const char* buffer, size_t len){
        int fd = client.GetFd();
        auto it = sessions.find(fd);
        if(it == sessions.end()) return;

        it -> second.appendData(buffer, len);

        HttpRequest req;

        while (it->second.parse(req)) {
            std::cout << "[" << req.method << "] " << req.path << '\n';

            std::string response = handleRequest(req);
            client.Send(response.c_str(), response.size());
        }
    });

    server.setOnDisconnect([&](int fd){
        std::cout << "disconnect Fd: " << fd << '\n';
        sessions.erase(fd);
    });

    std::cout << "HTTP server running on 8080\n";
    server.start();

    return 0;
}