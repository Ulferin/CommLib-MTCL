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
 *  $> ./test_gather 3 App4
 * 
 * */

#include <iostream>
#include <string>
#include "../../mtcl.hpp"

int main(int argc, char** argv){

    if(argc < 3) {
        printf("Usage: %s <0|1> <App1|App2>\n", argv[0]);
        return 1;
    }

    int rank = atoi(argv[1]);
	Manager::init(argv[2], "test_gather.json");

    auto hg = Manager::createTeam("App1:App2:App3:App4", "App1", GATHER);
    if(hg.isValid()) printf("Created team with size: %d\n", hg.size());
    
    std::string data{argv[2]};
    data += '\0';

    char* buff = nullptr;
    if(rank == 0) buff = new char[hg.size()*data.length()];

    hg.execute(data.c_str(), data.length(), buff, data.length());

    // Root
    if(rank == 0) {
        for(int i = 0; i < hg.size(); i++) {
            std::string res(buff+(i*data.length()), data.length());
            printf("buff[%d] = %s\n", i, res.c_str());
        }
    }
    hg.close();

    Manager::finalize();

    return 0;
}
