
#include <iostream>
#include <string>
#include <optional>
#include <thread>
#include <cstring>
#include <fstream>


#include "../../manager.hpp"
#include "../../protocols/tcp.hpp"
#include "../../protocols/mpi.hpp"

#define ITEMS 100

std::string random_string( size_t length ){
    auto randchar = []() -> char {
        const char charset[] =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[ rand() % max_index ];
    };
    std::string str(length,0);
    std::generate_n( str.begin(), length, randchar );
    return str;
}

int main(int argc, char** argv){
    Manager::registerType<ConnTcp>("TCP");
    Manager::registerType<ConnMPI>("MPI");
    Manager::init(argc, argv);

    std::vector<HandleUser> writeHandles;

    std::ifstream input("workers.list");
    for( std::string line; getline( input, line ); ){
        //std::cout << "Connecting to: " << line << std::endl;
        writeHandles.emplace_back(Manager::connect(line));
    }
    int workers = writeHandles.size();
    //std::cout << workers << " Workers connected\n";

    //for(auto& h : writeHandles) std::cout << (h.isValid() ? "VALID" : "INVALID") << std::endl;
    for(auto& h : writeHandles) h.yield();
    

    std::thread t([&writeHandles, workers](){
        for(int i = 0; i < ITEMS; i++)
            writeHandles[i % workers].send(random_string(50).c_str(), 50);

        for(auto& w : writeHandles)
            w.send("EOS", 3);
    });

    int receivedEOS = 0;
    char output[50];
    while(receivedEOS < workers){
        auto h = Manager::getNext();
        size_t sz = h.read(output, 50);
        if (sz) std::cout << output << std::endl;
        if (sz == 0) receivedEOS++;
    }

    t.join();

    Manager::endM();
   
    return 0;
}