/*
 * 1- Activate ompi-server and server executable: refer to mpi_p2p_server.cpp
 *
 * 2- run client:
 *      $ mpirun -n 1 --ompi-server file:uri_addr.txt ./mpi_p2p_client.out "MPIP2P:<portname>"
 * 
 */


#include <iostream>
#include <string>
#include <optional>
#include <thread>

#include <mpi.h>


#include "../manager.hpp"
#include "../protocols/mpip2p.hpp"


int main(int argc, char** argv){

    Manager::registerType<ConnMPIP2P>("MPIP2P");
    Manager::init(argc, argv);
    // Manager::listen("MPIP2P");

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if(rank == 0) {
        {
            auto handle = Manager::connect("MPIP2P:");
            if(handle.isValid()) {
                char buff[5]{'c','i','a','o','\0'};
                size_t size = 5;
                // std::this_thread::sleep_for(std::chrono::milliseconds(10000));
                handle.send(buff, size);
                printf("sent\n");
                handle.close();
            }
        }
        Manager::endM();
    }

    return 0;
}