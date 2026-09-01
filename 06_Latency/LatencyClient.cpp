#include "TcpSocket.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>

void RunClient(int i, int runtime){
    TcpSocket client;

    if(!client.Connect("127.0.0.1", 8080)){
        return;
    }

    char buf[128];
    const std::string msg = "PING";

    auto start_time = std::chrono::steady_clock::now();

    while(client.IsValid()){
        auto now = std::chrono::steady_clock::now();
        if(std::chrono::duration_cast<std::chrono::seconds>(now-start_time).count() >= runtime){
            break;
        }

        if(client.Send(msg.c_str(), msg.size()) <= 0) break;

        ssize_t len = client.Recv(buf, sizeof(buf)-1); //Echo recv
        if(len <= 0) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char* argv[]){
    int client_num = std::stoi(argv[1]);
    int test_sec = std::stoi(argv[2]);

    std::cout << "동시 접속자: " << client_num << "명\n"
              << "테스트 시간: " << test_sec << "초\n";

    std::vector<std::thread> thread_vec;
    thread_vec.reserve(client_num);

    for(int i = 0; i < client_num; ++i){
        thread_vec.emplace_back(RunClient, i+1, test_sec);
        //std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for(auto& t : thread_vec){
        if(t.joinable()) t.join();
    }
    return 0;
}