
#include <iostream>
#include <string>
#include <optional>
#include <thread>
#include <cstring>


#include "../../manager.hpp"

#ifdef TCPWORKER
#include "../../protocols/tcp.hpp"
#endif

#ifdef MPIWORKER
#include "../../protocols/mpi.hpp"
#endif

int main(int argc, char** argv){
#ifdef MPIWORKER
    Manager::registerType<ConnMPI>("MPI");
#endif

#ifdef TCPWORKER
    Manager::registerType<ConnTcp>("TCP");
#endif
    Manager::init(argc, argv);

#ifdef TCPWORKER
    Manager::listen("TCP:0.0.0.0:8000");
#endif

    bool EOSreceived = false;
    char buffer[50];
    while(!EOSreceived){
        auto h = Manager::getNext();
        if (h.isNewConnection()) continue;
        size_t sz2 = 0;
        h.read((char*) &sz2, 4);

        size_t sz = h.read(buffer, sz2);
        if (sz == 50) {
            int i = 0;
            while(buffer[i]) {
                buffer[i] = toupper(buffer[i]);
                i++;
            }
            h.send(buffer, 50);
        }

        if (sz == 3){
            EOSreceived = true;
            h.close();
        }
    }

    Manager::endM();
   
    return 0;
}