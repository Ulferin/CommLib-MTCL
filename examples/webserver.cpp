
#include <iostream>
#include <string>
#include <optional>
#include <thread>
#include <cstring>


#include "../../manager.hpp"
#include "../../protocols/tcp.hpp"


int main(int argc, char** argv){
    Manager::registerType<ConnTcp>("TCP");
    Manager::init(argc, argv);
    Manager::listen("TCP:0.0.0.0:80"); // TCP:host:port

    while(true){
        auto h = Manager::getNext();
        if(h.isNewConnection()) 
            continue;

        char buffer[65535];
        size_t rcvd = h.read(buffer, 65535);

        if (rcvd == 0){ // discconnect che faccio??
            h.close();
            continue;
        }

        buffer[rcvd] = '\0';

        char* method = strtok(buffer,  " \t\r\n");
        char* uri    = strtok(NULL, " \t");
        char * prot   = strtok(NULL, " \t\r\n"); 

        fprintf(stderr, "\x1b[32m + [%s] %s\x1b[0m\n", method, uri);
        char* qs;

        if (qs = strchr(uri, '?'))
        {
            *qs++ = '\0'; //split URI
        } else {
            qs = uri - 1; //use an empty string
        }



        


        // ho servito la richiesta, devo chiudere la connessione
        h.close();
    }

    return 0;
}