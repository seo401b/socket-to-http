#include "MultiReactorClient.h"
#include <iostream>
#include <string>

int main() {
    MultiReactorClient client;

    client.setOnConnect([](){
        std::cout << "Connected to server\n";
    });

    client.setOnRead([](const char* buf, size_t len){
        std::cout << "\nServer: " << buf;
        std::cout << "msg to send (quit to exit): " << std::flush;
    });

    client.setOnDisconnect([](){
        std::cout << "disconnected\n";
    });

    if(!client.connect("127.0.0.1", 8080)){
        std::cerr << "failed connect to server\n";
        return 1;
    }

    std::string input;
    while(true){
        std::cout << "msg to send (quit to exit): ";
        if(!std::getline(std::cin, input) || input == "quit"){
            break;
        }
        if(!input.empty()){
            client.send(input + '\n');
        }
    }

    client.disconnect();
    return 0;
}