#include "TcpSocket.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <mutex>

std::mutex mtx;
double g_tot_t = 0.0;
double g_max_t = 0.0;
int g_cnt = 0;

void RunClient(int i, int runtime){
    TcpSocket client;

    if(!client.Connect("127.0.0.1", 8080)){
        return;
    }

    char buf[128];
    const std::string msg = "PING";

    auto start_time = std::chrono::steady_clock::now(); //runtime check

    double total_t = 0.0;
    double max_t = 0.0;
    int cnt = 0;

    while(client.IsValid()){
        auto now = std::chrono::steady_clock::now(); //runtime check
        if(std::chrono::duration_cast<std::chrono::seconds>(now-start_time).count() >= runtime){
            break;
        } //runtime check

        auto start = std::chrono::high_resolution_clock::now(); //RTT check

        if(client.Send(msg.c_str(), msg.size()) <= 0) break;

        ssize_t len = client.Recv(buf, sizeof(buf)-1); //Echo recv
        if(len <= 0) break;

        auto end = std::chrono::high_resolution_clock::now(); //RTT check
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end-start).count();
        // std::cout << "Latency: " << latency/1000.0 << " ms\n"; mutex로인한 병목 발생

        total_t = latency;
        if(max_t < total_t) max_t = total_t;
        cnt++;
        

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        g_tot_t += total_t;
        g_cnt += cnt;
        if(max_t > g_max_t) g_max_t = max_t; 
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

    std::cout << "총 처리 스레드: " << g_cnt << '\n'
                << "평균: " << g_tot_t/g_cnt << " ms\n"
                << "최고 지연: " << g_max_t << " ms\n";

    return 0;
}