/*
 *
 * Fan-in implementation test
 *
 *
 * Compile with:
 *  $> RAPIDJSON_HOME="/rapidjson/install/path" make clean test_fanin
 * 
 * Execution:
 *  $> ./test_fanin 0 App1
 *  $> ./test_fanin 1 App2
 *  $> ./test_fanin 1 App3
 * 
 * 
 * */



#include <iostream>
#include "../../mtcl.hpp"

inline static std::string hello{"Hello team!"};
inline static std::string bye{"Bye team!"};

int main(int argc, char** argv){

    if(argc < 2) {
        printf("Usage: %s <0|1> <Server|Client1|Client2>\n", argv[0]);
        return 1;
    }

    std::string listen_str{};
    std::string connect_str{};

    int rank = atoi(argv[1]);
	Manager::init(argv[2], "test_collectives.json");

    // Root
    if(rank == 0) {
        Manager::listen("TCP:0.0.0.0:42000");
        auto hg = Manager::createTeam("App1:App2:App3", 3, "App1", "fan-in");

        hg.close();
        if(hg.isValid())
            printf("Correctly created team\n");

        char* s = new char[hello.length()+1];
        hg.receive(s, hello.length());
        s[hello.length()] = '\0';
        std::cout << "Received: " << s << std::endl;
        delete[] s;

        s = new char[bye.length()+1];
        hg.receive(s, bye.length());
        s[bye.length()] = '\0';
        printf("Received bye: %s\n", s);
        delete[] s;

        hg.close();
        
    }
    else {
        auto hg = Manager::createTeam("App1:App2:App3", 3, "App1", "fan-in");

        if(hg.isValid())
            printf("Correctly created team\n");

        if(std::string{argv[2]} == "App2") hg.send((void*)hello.c_str(), hello.length());
        if(std::string{argv[2]} == "App3") hg.send((void*)bye.c_str(), bye.length());
    }

    Manager::finalize();

    return 0;
}
