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

/*
                        Worker1

                        Worker2
Produttore --Scatter-->            --Gather--> Consumer 
                        WorkerN

auto hg = Manager::createTeam("App1:App2", 4, "App1", "scatter");
auto hg = Manager::createTeam("App2x10:App3", 4, "App3", "gather");

*/

int main(int argc, char** argv){

    if(argc < 3) {
        printf("Usage: %s <0|1> <App1|App2>\n", argv[0]);
        return 1;
    }

    std::string listen_str{"TCP:0.0.0.0:42000"};

    int rank = atoi(argv[1]);
	Manager::init(argv[2], "test_collectives.json");
    if(rank == 0) Manager::listen(listen_str);

    // Root
    auto hg = Manager::createTeam("App1:App2:App3", "App1", "gather");
    int size = hg.size();

    //if(rank==0) {
    // hg.yield();
    // auto h = Manager::getNext();
    // if(h.isCollective()) {
    //      h.execute();
    // }
    //    for(int i = 0; i < 4; i++) {
    //        printf("buff[%d] = %d\n", i, buff[i]);
    //    }
    //}
    //else {hg.execute();}

    // Qui abbiamo assunto che il root abbia allocato abbastanza spazio
    // per il buffer --> servono num_rank*sizeof(data)
    int* buff;
    if(rank == 0) {
        buff = new int[size];
        // buff[rank] = rank;
    }

    //TODO: check buffer in base a sender/receiver, vogliamo poter accettare NULL
    hg.execute(&rank, sizeof(int), rank==0 ? buff : NULL, sizeof(int));

    if(rank == 0)
        for(int i = 0; i < size; i++) {
            printf("buff[%d] = %d\n", i, buff[i]);
        }

    hg.close();

    
    Manager::finalize();

    return 0;
}
