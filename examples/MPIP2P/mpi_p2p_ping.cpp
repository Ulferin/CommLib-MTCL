/*
 * 1- Activate ompi-server and server executable: refer to mpi_p2p_server.cpp
 *
 * 2- run client:
 *      $ mpirun -n 1 --ompi-server file:uri_addr.txt ./mpi_p2p_ping.out "MPIP2P:<portname>"
 * 
 */


#include <iostream>
#include <string>
#include <optional>
#include <thread>

#include <mpi.h>


#include "../../manager.hpp"
#include "../../protocols/mpip2p.hpp"


int main(int argc, char** argv){

    Manager::registerType<ConnMPIP2P>("MPIP2P");
    Manager::init(argc, argv);

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if(rank == 0) {
        {
            auto handle = Manager::connect("MPIP2P:");
            if(handle.isValid()) {
                char buff[5]{'p','i','n','g','\0'};
                ssize_t size = 5;

                handle.send(buff, size);
                printf("Sent: \"%s\"\n", buff);
            }
            // implicit handle.yield() when going out of scope
        }

        auto handle = Manager::getNext();
        char buff[5];
        ssize_t size = 5;
        
        if(handle.read(buff, size) == 0)
            printf("Connection has been closed by the server.\n");
        else {
            printf("Received: \"%s\"\n", buff);
            handle.close();
            printf("Connection closed locally, notified the server.\n");
        }
        Manager::endM();
    }

    return 0;
}