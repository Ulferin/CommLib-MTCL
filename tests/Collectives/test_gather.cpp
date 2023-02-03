/*
 *
 * Gather implementation test
 *
 *
 * Compile with:
 *  $> RAPIDJSON_HOME="/rapidjson/install/path" make clean test_gather
 * 
 * Execution:
 *  $> ./test_gather 0 App1
 *  $> ./test_gather 1 App2
 *  $> ./test_gather 2 App3
 *  $> ./test_gather 3 App3
 * 
 * */

#include <iostream>
#include "../mtcl.hpp"

int main(int argc, char** argv){

    if(argc < 3) {
        printf("Usage: %s <0|1> <App1|App2>\n", argv[0]);
        return 1;
    }

    std::string listen_str{"TCP:0.0.0.0:42000"};

    int rank = atoi(argv[1]);
	Manager::init(argv[2], "test_collectives.json");

    // Root
    if(rank == 0) {
        Manager::listen(listen_str);

        auto hg = Manager::createTeam("App1:App2:App3:App4", 4, "App1", "gather");

        // Qui abbiamo assunto che il root abbia allocato abbastanza spazio
        // per il buffer --> servono num_rank*sizeof(data)
        int* buff = new int[4];
        buff[0] = 0;
        hg.receive(buff, sizeof(int));

        for(int i = 0; i < 4; i++) {
            printf("buff[%d] = %d\n", i, buff[i]);
        }

        hg.close();
    }
    else {
		auto hg = Manager::createTeam("App1:App2:App3:App4", 4, "App1", "gather");
        hg.send(&rank, sizeof(int));

        hg.close();
    }

    Manager::finalize();

    return 0;
}
