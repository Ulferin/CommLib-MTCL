#include <iostream>
#include <vector>
#include <string>
#include <assert.h>
#include <map>
#include <tuple>
#include <mpi.h>
#include <optional>


#include "manager.hpp"


int main(int argc, char** argv){
    Manager m(argc,argv);
    manager.registerProtocol<ConnTcp>("TCP");

     Manager::connect("TCP://hostname:port").yield();
     Manager::connect("TCP://hostname:port").yield();
     Manager::connect("TCP://hostname:port").yield();
     Manager::connect("TCP://hostname:port", "master").yield();
    Manger::connect("MPI://0:10");
    

    while(true){
        auto handle = Manger.getReady();

        // send/receive
        auto* handle2 = &handle;


        handle.yield();

        handle2->receive(); // errore handle2 è invalidato
    }

    

    Manager.getHandleFromLabel("master");

    auto handle = Manager::connect("TCP://hostname:port");// TCP : hostname:porta // MPI COMM_WORL:rank:tag

    handle->send();

    handle.yield();

    
    handle = manger.getReady();
    handle->receive(buff, size);
    handle.yield();




    return 0;
}