/*
 * Simple TCP hello world example
 * 
 * 
 * === Compilation ===
 * 
 * make hello_world
 * 
 * === Execution ===
 *  - server: $ ./hello_world 0
 *  - client: $ ./hello_world 1
 * 
 * 
 */

#include <iostream>
#include "../commlib.hpp"

int main(int argc, char** argv){

    if(argc < 2) {
        printf("Usage: %s <rank>\n", argv[0]);
        return 1;
    }

    int rank = atoi(argv[1]);
    Manager::init();
    // Listening for new connection, sending hello message to connected client
    if(rank == 0) {
        
        Manager::listen("TCP:0.0.0.0:42000");

        auto handle = Manager::getNext();

        if(handle.isNewConnection()) {
            printf("Got new connection\n");

            std::string hello{"Hello World!"};
            handle.send(hello.c_str(), hello.length());
            printf("Sent: \"%s\"\n", hello.c_str());

            handle.close();
        }
            
    }
    // Connecting to server, waiting for hello message
    else {
        
        auto handle = Manager::connect("TCP:0.0.0.0:42000");
        if(handle.isValid()) {
            char buff[100];
            ssize_t size = 100;

            if(handle.read(buff, size) == 0) {
                printf("Peer closed connection\n");
                return 1;
            }
            printf("Read: \"%s\"\n", buff);
        }
    }

    Manager::finalize();

    return 0;
}