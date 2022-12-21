/*
 * Simple ping-pong example between a server and one (or more) clients. All the
 * connections take place with the same protocol declared at compilation time
 * via specified PROTOCOL.
 * 
 * - The clients create a connection toward the server and send a "ping" message,
 * then they wait for a reply and close the connection.
 * 
 * - The server expects to receive a fixed number of connections, replies with
 * the same message to all the requests and terminate after all the clients
 * have closed their endpoint.
 * The amount of accepted connections can be tweaked via the MAX_NUM_CLIENTS macro
 * in this file. After this amount of connections the server stops accepting new
 * connections and terminates.
 * 
 * 
 * === Compilation ===
 * 
 * make pingpong PROTOCOL=<protocol_name>
 * 
 * TCP:     make pingpong PROTOCOL=TCP
 * MQTT:    make pingpong PROTOCOL=MQTT
 * MPI:     make pingpong PROTOCOL=MPI
 * MPIP2P   make pingpong PROTOCOL=MPIP2P
 * 
 * 
 * === Execution ===
 * TCP:
 *  - start pingpong server: ./pingpong 0
 *  - start pingpong client: ./pingpong 1
 * 
 * MQTT:
 *  - start mosquitto broker: mosquitto -v
 *  - start pingpong server: ./pingpong 0
 *  - start pingpong client: ./pingpong 1 0:app0
 * 
 * MPIP2P:
 *  - start ompi-server: ompi-server --report-uri uri_file.txt --no-daemonize
 *  - start pingpong server: mpirun -n 1 --ompi-server file:uri_file.txt ./pingpong 0
 *  - start pingpong client: mpirun -n 1 --ompi-server file:uri_file.txt ./pingpong 1
 * 
 * MPI:
 *  - start pingpong server/client as MPMD: mpirun -n 1 ./pingpong 0 : -n 4 ./pingpong 1
 * 
 * 
 */

#include <iostream>
#include <string>
#include <optional>
#include <thread>

#include <mpi.h>


#include "../manager.hpp"
#include "../protocols/tcp.hpp"
#include "../protocols/mpi.hpp"
#include "../protocols/mqtt.hpp"
#include "../protocols/mpip2p.hpp"

#define MAX_NUM_CLIENTS 4

int main(int argc, char** argv){

    if(argc < 2) {
        printf("Usage: %s <rank>\n", argv[0]);
        return 1;
    }

    std::string listen_str{};
    std::string connect_str{};


#ifdef PROT_MPIP2P
    Manager::registerType<ConnMPIP2P>("MPIP2P");
    listen_str = {"MPIP2P:published_label"};
    connect_str = {"MPIP2P:published_label"};
#endif

#ifdef PROT_TCP
    Manager::registerType<ConnTcp>("TCP");
    listen_str = {"TCP:0.0.0.0:42000"};
    connect_str = {"TCP:0.0.0.0:42000"};
#endif

/*NOTE: MPI has no need to call the listen function. We build the listen_str
        to make the Manager happy in this "protocol-agnostic" example.
*/
#ifdef PROT_MPI
    Manager::registerType<ConnMPI>("MPI");
    listen_str = {"MPI:"};
    connect_str = {"MPI:0:5"};
#endif


#ifdef PROT_MQTT
    Manager::registerType<ConnMQTT>("MQTT");
    listen_str = {"MQTT:0"};
    connect_str = {"MQTT:0:app0"};
#endif

    int rank = atoi(argv[1]);

    // Listening for new connections, expecting "ping", sending "pong"
    if(rank == 0) {
        Manager::init();
        Manager::listen(listen_str);

        int count = 0;
        while(count < MAX_NUM_CLIENTS) {

            auto handle = Manager::getNext();

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
    }
    // Connecting to server, sending "ping", expecting "pong"
    else {

        /*NOTE: currently MQTT needs to call listen even on "non-server" nodes.
                This will change after the ID for this particular node can be retrieved
                via configuration file.
                This will be probably solved with the introduction of the configuration
                file, where the appID can be used as an ID for the broker.
        */
#ifdef PROT_MQTT
        Manager::listen(listen_str.append("1"));
#endif
        Manager::init();
        {
            auto handle = Manager::connect(connect_str);
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
    }

    Manager::finalize();

    return 0;
}