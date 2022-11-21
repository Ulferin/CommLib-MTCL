#include <iostream>
#include <string>
#include <optional>
#include <thread>


#include "../manager.hpp"
#include "../protocols/tcp.hpp"


int main(int argc, char** argv){
    if(argc != 3) {
        printf("Usage: %s <id> <address>\n", argv[0]);
        return -1;
    }
    
    int id = atoi(argv[1]);     // logical rank
    char* addr = argv[2];       // listening address
    // Manager m;
    Manager::registerType<ConnTcp>("TCP");
    Manager::init(argc, argv);
    Manager::listen(addr); // TCP:host:port

    // m.registerType<ConnTcp>("TCP");      // TCP
    // m.registerType<ConnMPI>(addr);   // MPI
    // m.init();

    // Listening for new connections
    if(id == 0) {

        while(true) {
            auto handle = Manager::getNext();
            // auto handle = m.getReady();
            if(handle.isValid()) {
                if(handle.isNewConnection()) {
                    handle.yield();
                    printf("Got new connection\n");
                    char buff[5]{'c','i','a','o', '\0'};
                    size_t count = 0;
                    size_t size = 5;
                    while(count < size)
                        count += handle.send(buff+count, size-count);
                    
                    break;
                }
                else handle.yield();
            }
            else {
                printf("No value in handle\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }

            // bool blocking;
            // HandleUser handle = m.getNext(blocking);
            // if(handle.new())
            //     handle.yield();
            // else....
        }
        Manager::endM();
    }
    // Trying to connect
    else {
        {
            auto handle = Manager::connect("TCP:127.0.0.1:42000");
            if(handle.isValid()) {
                size_t size = 5;
                char buff[5];
                handle.read(buff, size);
                // handle.yield();
                handle.close();
                // auto handle = m.getReady();

                std::string res{buff};
                printf("%s\n", res.c_str());
            }
        }
        Manager::endM();
    }

    return 0;
}