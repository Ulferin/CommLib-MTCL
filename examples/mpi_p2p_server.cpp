/*
 * 1- Activate rendez-vous server:
 *      $ ompi-server -r uri_addr.txt
 *
 * 2- run server:
 *      $ mpirun -n 1 --ompi-server file:uri_addr.txt mpi_p2p_server.out
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
    Manager::listen("MPIP2P");

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Listening for new connections
    if(rank == 0) {

        while(true) {
            auto handle = Manager::getNext();

            if(handle.isValid()) {
                if(handle.isNewConnection()) {
                    // handle.yield();
                    printf("Got new connection\n");
                    char buff[5];
                    size_t size = 5;
                    handle.read(buff, size);

                    std::string res{buff};
                    printf("%s\n", res.c_str());
                    
                    break;
                }
                else handle.yield();
            }
            else {
                printf("No value in handle\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }
        Manager::endM();
    }

    return 0;
}