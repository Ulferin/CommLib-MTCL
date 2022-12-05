/*
 * 1- Activate rendez-vous server:
 *      $ ompi-server -r uri_addr.txt
 *
 * 2- run server:
 *      $ mpirun -n 1 --ompi-server file:uri_addr.txt mpi_p2p_pong.out
 * 
 */

#include <iostream>
#include <string>
#include <optional>
#include <thread>

#include <mpi.h>


#include "../../manager.hpp"
#include "../../protocols/mpip2p.hpp"

#define MAX_NUM_CLIENTS 4

int main(int argc, char** argv){

    Manager::registerType<ConnMPIP2P>("MPIP2P");
    Manager::init(argc, argv);
    Manager::listen("MPIP2P");

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Listening for new connections
    if(rank == 0) {
        int count = 0;
        while(count < MAX_NUM_CLIENTS) {
            auto handle = Manager::getNext();

            if(handle.isValid()) {
                if(handle.isNewConnection()) {
                    printf("Got new connection\n");
                    char buff[5];
                    size_t size = 5;
                    if(handle.read(buff, size) == 0)
                        printf("Connection closed by peer\n");
                    else {
                        std::string res{buff};
                        printf("Received \"%s\"\n", res.c_str());
                    }

                    char reply[5]{'p','o','n','g','\0'};
                    handle.send(reply, size);
                    printf("Sent: \"%s\"\n",reply);
                }
                else {
                    printf("Waiting for connection close...\n");
                    char tmp;
                    size_t size = 1;
                    if(handle.read(&tmp, size) == 0) {
                        printf("Connection closed by peer\n");
                        handle.close();
                        count++;
                    }
                }
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