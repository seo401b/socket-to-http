#include "TcpSocket.h"
#include <iostream>
#include <cstring>

int main(){
    TcpSocket client;

    if(!client.Connect("127.0.0.1", 8080)){
        return 1;
    }


       //send & echo recv
    char buffer[1024] = {0,};
    std::string input_msg;

    while(1){
        std::cout << "client msg (종료: quit): ";

        std::getline(std::cin, input_msg);
        if(input_msg == "quit") break;

        //send
        client.Send(input_msg.c_str(), input_msg.size());

        //echo recv
        std::memset(buffer, 0, sizeof(buffer));
        ssize_t recv_len = client.Recv(buffer, sizeof(buffer)-1);

        if(recv_len > 0){
            std::cout << "echo from server: " << buffer << '\n';
        } else if(recv_len == 0){
            std::cout << "Disconnect from server\n";
            break;
        } else {
            std::cerr << "err\n";
            break;
        }
    }
    return 0;
}