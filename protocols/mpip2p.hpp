#ifndef MPIP2P_HPP
#define MPIP2P_HPP

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <vector>
#include <queue>
#include <map>
#include <shared_mutex>
#include <thread>
#include <pthread.h>
#include <atomic>
#include <signal.h>

#include <mpi.h>

#include "../handle.hpp"
#include "../protocolInterface.hpp"

#define MPIP2PSLEEPUS 1000
#define DISCONNECTTAG 42
#define STOP_PROCESS "stop_accept.out"
#define PUBLISH_NAME "test_server"

class HandleMPIP2P : public Handle {

public:
    MPI_Comm server_comm; // MPI communicator for the specific p2p connection
    bool closing = false;
    HandleMPIP2P(ConnType* parent, MPI_Comm server_comm, bool busy=true) : Handle(parent, busy), server_comm(server_comm) {}

    ssize_t send(const char* buff, size_t size) {
        MPI_Send(buff, size, MPI_BYTE, 0, 0, server_comm);
        return size;
    }

    ssize_t receive(char* buff, size_t size){
        MPI_Status status; 
        int count, flag;
        while(true) {
            if(MPI_Iprobe(MPI_ANY_SOURCE, 0, server_comm, &flag, &status)!=MPI_SUCCESS) {
                printf("Error in probe\n");
                return -1;
            }
            if(flag) {
                MPI_Recv(buff, size, MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, server_comm, &status);
                MPI_Get_count(&status, MPI_BYTE, &count);
                return count;
            }
            else if(closing) {
                MPI_Comm_free(&server_comm);
                return 0;
            }

            std::this_thread::sleep_for(std::chrono::microseconds(MPIP2PSLEEPUS));
        }
        
        return 0;
    }


    ~HandleMPIP2P() {}

};


class ConnMPIP2P : public ConnType {
private:
    // enum class ConnEvent {close, yield};

protected:
    std::string address;
    char portname[MPI_MAX_PORT_NAME];
    
    int rank;
    std::atomic<bool> finalized = false;
    bool listening = false;
    
    std::map<HandleMPIP2P*, bool> connections;  // Active connections for this Connector
    std::shared_mutex shm;

    inline static std::thread t1;
    pthread_t pt1;
    MPI_Errhandler errhdl;



public:

   ConnMPIP2P(){};
   ~ConnMPIP2P(){};

    int init() {
        int provided;
        if (MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &provided) != MPI_SUCCESS)
            return -1;

        // no thread support 
        if (provided < MPI_THREAD_MULTIPLE){
            printf("No thread support by MPI\n");
            return -1;
        }

        MPI_Comm_rank(MPI_COMM_WORLD, &rank);

        return 0;
    }

    void _listen(char* portname) {

        listening = true;
        MPI_Publish_name(PUBLISH_NAME, MPI_INFO_NULL, portname);
        
        while(!finalized) {
            MPI_Comm client;
            MPI_Comm_accept(portname, MPI_INFO_NULL, 0, MPI_COMM_WORLD, &client);

            HandleMPIP2P* handle = new HandleMPIP2P(this, client, false);
            
            std::unique_lock ulock(shm);
            connections.insert({handle, false});
            addinQ({true, handle});
        }

        printf("Accept thread finalized.\n");
        
    }

    int listen(std::string s) {
        MPI_Open_port(MPI_INFO_NULL, portname);
        printf("Listening on portname: %s\n", portname);

        t1 = std::thread([&](){_listen(portname);});

        return 0;
    }

    void update() {
        int flag;
        MPI_Status status;

        std::unique_lock ulock(shm);
        for (auto& [handle, to_manage] : connections) {
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, handle->server_comm, &flag, &status);
            if(flag) {
                if(status.MPI_TAG == DISCONNECTTAG) {
                    int headersLen;
                    MPI_Get_count(&status, MPI_INT, &headersLen);
                    int header[headersLen];
                    
                    if (MPI_Recv(header, headersLen, MPI_INT, status.MPI_SOURCE, DISCONNECTTAG, handle->server_comm, &status) != MPI_SUCCESS) {
                        printf("Error on Recv disconnect tag\n");
                        throw;
                    }
                    handle->closing = true;
                }

                if(to_manage) {
                    to_manage = false;
                    addinQ({false, handle});
                }
            }
        }

        return;        
    }

    /* Probabilmente qui il parametro della connect può diventare la stringa
     * della lookup. Attualmente inutilizzato.
     */
    Handle* connect(const std::string& address) {
        MPI_Lookup_name(PUBLISH_NAME, MPI_INFO_NULL, portname);
        MPI_Comm server_comm;
        MPI_Comm_connect(portname, MPI_INFO_NULL, 0, MPI_COMM_WORLD, &server_comm);
        printf("[MPIP2P]Connecting to: %s\n", portname);


        HandleMPIP2P* handle = new HandleMPIP2P(this, server_comm, true);

        std::unique_lock lock(shm);
        connections[handle] = false;
        
        return handle;
    }

    void notify_close(Handle* h) {
        std::unique_lock l(shm);
        HandleMPIP2P* handle = reinterpret_cast<HandleMPIP2P*>(h);
        if (!handle->closing){
            int aux = 0;
            MPI_Send(&aux, 1, MPI_INT, 0, DISCONNECTTAG, handle->server_comm);
            MPI_Comm_disconnect(&handle->server_comm);
        }
        connections.erase(reinterpret_cast<HandleMPIP2P*>(h));
    }


    void notify_yield(Handle* h) override {
        std::unique_lock l(shm);
        connections[reinterpret_cast<HandleMPIP2P*>(h)] = true;
    }

    void end() {
        auto modified_connections = connections;
        for(auto& [handle, to_manage] : modified_connections)
            if(to_manage)
                setAsClosed(handle);

        finalized = true;
        if(listening) {
            MPI_Comm c;
            char* a[2];
            a[0] = portname;
            a[1] = NULL;

            /* Spawns utility MPI process to issue a fake MPI_Comm_connect and
             * wake up the blocking accept
             */
            MPI_Comm_spawn(STOP_PROCESS, a, 1, MPI_INFO_NULL, 0, MPI_COMM_SELF, &c, MPI_ERRCODES_IGNORE);
            
            t1.join();

            MPI_Unpublish_name(PUBLISH_NAME, MPI_INFO_NULL, portname);
            MPI_Close_port(portname);
        }

        MPI_Finalize();
        printf("Finalized\n");
    }

};

#endif