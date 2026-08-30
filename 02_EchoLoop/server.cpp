#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

int main(){
    //create socket AF_INET -> Addr Family IPv4   STREAM -> TCP  0 -> default protocol 
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    //addr set, binding
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(12345); // host 2 network, short -> Little end to Big end (standard.)

    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    std::cout << "create socket & binding success\n";
    
    //listenig socket
    listen(server_sock, 5);

    //accept client
    int client_sock = accept(server_sock, nullptr, nullptr);
    std::cout << "accept success\n";

    char buffer[1024] = {0,};
    

    //cout recv
    while(1){
        std::memset(buffer, 0, sizeof(buffer));

        ssize_t recv_len = read(client_sock, buffer, sizeof(buffer)-1);
        if(recv_len > 0){
            buffer[recv_len] = '\n';
            std::cout << buffer << '\n';
            //echo
            write(client_sock, buffer, recv_len);
        }
        else if(recv_len == 0){
            std::cout << "recv FIN\n";
            break;
        }
        else{
            std::cerr<<"err\n";
            break;
        }
    }

    //free
    close(client_sock);
    close(server_sock);
    return 0;
}