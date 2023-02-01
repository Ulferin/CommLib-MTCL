/*
 *
 * Simple collectives test to check if functionalities are working as intended
 * and to keep track of features yet to be implemented
 *
 * [x] Definizione interfaccia gruppo
 * [x] Sincronizzazione su accept da parte del root 
 * [x] Broadcast collective 
 * [ ] Fan-in/Fan-out
 * [ ] Restituzione gruppo a Manager
 * 
 */

#define ENABLE_CONFIGFILE

#include <iostream>
#include "../mtcl.hpp"

#define MAX_MESSAGE_SIZE 100

inline static std::string hello{"Hello team!"};
inline static std::string bye{"Bye team!"};

int main(int argc, char** argv){

    if(argc < 2) {
        printf("Usage: %s <0|1> <Server|Client1|Client2>\n", argv[0]);
        return 1;
    }

    std::string listen_str{};
    std::string connect_str{};


    listen_str = {"TCP:0.0.0.0:42000"};
    connect_str = {"TCP:0.0.0.0:42000"};

    int rank = atoi(argv[1]);
	Manager::init(argv[2], "test_collectives.json");

    // Root
    auto hg = Manager::createTeam("App1:App2:App3", "App1", "broadcast");
    if(rank == 0) {
        Manager::listen(listen_str);

        auto hg2 = Manager::createTeam("App1:App2", "App2", "broadcast");

        hg.send((void*)hello.c_str(), hello.length());
        hg2.send((void*)hello.c_str(), hello.length());
        hg.send((void*)bye.c_str(), bye.length());


    }
    else {
		// auto hg = Manager::createTeam("App1:App2:App3", "App1", "broadcast");

        HandleGroup hg2;
        if(rank==1)
            hg2 = Manager::createTeam("App1:App2", "App1", "broadcast");
        
        char* s = new char[hello.length()+1];

        hg.receive(s, hello.length());
        s[hello.length()] = '\0';
        std::cout << "Received: " << s << std::endl;

        hg.receive(s, bye.length());
        s[bye.length()] = '\0';
        if(std::string(s) == bye)
            printf("Received bye message\n");

        if(rank == 1) {
            char* s2 = new char[hello.length()+1];
            hg2.receive(s2, hello.length());
            s2[hello.length()] = '\0';
            std::cout << "Received: " << s2 << std::endl;
        }
    }

    Manager::finalize();

    return 0;
}
