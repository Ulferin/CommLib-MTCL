/*
 *
 * Testing Fan-in/Fan-out to implement reduce(sum) operation over arbitrary number
 * of intermediate workers
 *
 *
 * Compile with:
 *  $> RAPIDJSON_HOME="/rapidjson/install/path" make clean test_reducesum
 * 
 * Execution:
 *  $> ./test_reducesum 0 App1 size
 *  $> ./test_reducesum 2 App3 size
 *  $> for i in {1..size}; do ./test_reducesum 1 App2 size & done
 * 
 * 
 * */



#include <iostream>
#include "../../mtcl.hpp"

inline static int num_elements{100};
inline static int expected_res{5050};

inline static std::string hello{"Hello team!"};
inline static std::string bye{"Bye team!"};

int main(int argc, char** argv){

    if(argc < 4) {
        printf("Usage: %s <0|1|2> <App1|App2|App3> <App2 instances>\n", argv[0]);
        return 1;
    }


    int rank = atoi(argv[1]);
	Manager::init(argv[2], "test_collectives.json");
    int size = atoi(argv[3]);

    // Root
    if(rank == 0) {
        Manager::listen("TCP:0.0.0.0:42000");
        auto hg = Manager::createTeam("App1:App2", size+1, "App1", "fan-out");
        if(hg.isValid())
            printf("Correctly created team\n");

        for(int i = 1; i <= num_elements; i++) {
            hg.send(&i, sizeof(int));
        }


        hg.close();
    }
    // Worker
    else if(rank == 1){
        auto hg_fanout = Manager::createTeam("App1:App2", size+1, "App1", "fan-out");
        auto hg_fanin = Manager::createTeam("App2:App3", size+1, "App3", "fan-in");
        if(hg_fanout.isValid())
            printf("Correctly created team\n");

        int partial = 0;
        int res = 0;

        do {
            int el = 0;
            res = hg_fanout.receive(&el, sizeof(int));
            partial += el;
            printf("Received el: %d - partial is: %d\n", el, partial);
        } while(res > 0);
        hg_fanin.send(&partial, sizeof(int));
        // hg_fanout.close();
        // hg_fanin.close();
    }
    // Gatherer
    else {
        Manager::listen("TCP:0.0.0.0:42001");
        auto hg_fanin = Manager::createTeam("App2:App3", size+1, "App3", "fan-in");
        int total = 0;
        for(int i = 0; i < size; i++) {
            int el;
            hg_fanin.receive(&el, sizeof(int));
            total += el;
        }

        printf("Total is: %d, expected was: %d\n", total, expected_res);

        hg_fanin.close();
    }

    Manager::finalize();

    return 0;
}
