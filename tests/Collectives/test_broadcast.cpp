/*
 *
 * Simple broadcast test with two different groups
 *
 * 
 * Compile with:
 *  $> TPROTOCOL=UCX UCX_HOME=<ucx_install_path> UCC_HOME="/home/federico/install" RAPIDJSON_HOME=<rapidjson_install_path> make clean test_broadcast
 * 
 * Execution:
 *  $> ./test_broadcast 0 App1
 *  $> ./test_broadcast 1 App2
 *  $> ./test_broadcast 2 App3 
 *  $> ./test_broadcast 2 App4
 * 
 * 
 */


#include <iostream>
#include "../../mtcl.hpp"

#define MAX_MESSAGE_SIZE 100

inline static std::string hello{"Hello team!"};
inline static std::string bye{"Bye team!"};

int main(int argc, char** argv){

    if(argc < 2) {
        printf("Usage: %s <0|1> <App1|App2|App3>\n", argv[0]);
        return 1;
    }


    int rank = atoi(argv[1]);
	Manager::init(argv[2], "test_collectives.json");

    // Root
    if(rank == 0) {
        auto hg = Manager::createTeam("App1:App2:App3:App4", "App1", BROADCAST);
        auto hg2 = Manager::createTeam("App1:App2", "App1", BROADCAST);
        if(!hg.isValid() || !hg2.isValid()) {
            MTCL_PRINT(1, "[test_broadcast]:\t", "there was some error creating the teams.\n");
            return 1;
        }

        hg.send((void*)hello.c_str(), hello.length());
        hg2.send((void*)hello.c_str(), hello.length());
        hg.send((void*)bye.c_str(), bye.length());


        hg.close();
        hg2.close();
    }
    else {
		auto hg = Manager::createTeam("App1:App2:App3:App4", "App1", BROADCAST);
        if(!hg.isValid()) {
            return 1;
        }

        HandleUser hg2;
        if(rank==1) {
            hg2 = Manager::createTeam("App1:App2", "App1", BROADCAST);
            if(!hg2.isValid()) {
                return 1;
            }
        }
    
        
        char* s = new char[hello.length()+1];
        hg.receive(s, hello.length());
        s[hello.length()] = '\0';
        std::cout << "Received: " << s << std::endl;

        hg.receive(s, bye.length());
        s[bye.length()] = '\0';
        if(std::string(s) == bye)
            printf("Received bye message: %s\n", bye.c_str());
        delete[] s;

        if(rank == 1) {
            char* s2;
            size_t sz;
            hg2.probe(sz, true);
            s2 = new char[sz+1];
            hg2.receive(s2, sz);
            s2[sz] = '\0';
            std::cout << "Received: " << s2 << std::endl;
            delete[] s2;
            ssize_t res;
            do {
                size_t sz;
                res = hg2.probe(sz, true);
            } while(res != 0);
            hg2.close();
        }

        ssize_t res;
        do {
            size_t sz;
            res = hg.probe(sz, true);
        }while(res != 0);
        hg.close();
    }

    Manager::finalize();

    return 0;
}
