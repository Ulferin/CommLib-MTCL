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
#include <atomic>

#include <mpi.h>

#include "../handle.hpp"
#include "../protocolInterface.hpp"

class HandleMPIP2P : public Handle {

public:
    MPI_Comm server_comm; // MPI communicator for the specific p2p connection
    HandleMPIP2P(ConnType* parent, MPI_Comm server_comm, bool busy=true) : Handle(parent, busy), server_comm(server_comm) {}

    size_t send(const char* buff, size_t size) {
        MPI_Send(buff, size, MPI_BYTE, 0, 0, server_comm);

        return size;
    }

    size_t receive(char* buff, size_t size){
        MPI_Status status; 
        int count;
        MPI_Recv(buff, size, MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, server_comm, &status);
        MPI_Get_count(&status, MPI_BYTE, &count);
        
        return count;
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
    
    std::map<HandleMPIP2P*, bool> connections;  // Active connections for this Connector
    std::shared_mutex shm;

    inline static std::thread t1;


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
        MPI_Info info;
        MPI_Info_create( &info );
        MPI_Info_set(info, "timeout", "20");
        
        while(!finalized) {
            MPI_Comm client;
            // MPI_Publish_name("test_server", MPI_INFO_NULL, portname);
            MPI_Comm_accept(portname, info, 0, MPI_COMM_SELF, &client);

            HandleMPIP2P* handle = new HandleMPIP2P(this, client, false);
            
            std::unique_lock ulock(shm);
            connections.insert({handle, false});
            addinQ({true, handle});
        }
    }

    int listen(std::string s) {
        MPI_Open_port(MPI_INFO_NULL, portname);
        printf("portname: %s\n", portname);

        t1 = std::thread([&](){_listen(portname);});

        return 0;
    }

    void update() {
        int flag;
        MPI_Status status;

        std::unique_lock ulock(shm, std::defer_lock);

        for (auto& [handle, to_manage] : connections) {
            if(to_manage) {
                MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, handle->server_comm, &flag, &status);
                if(flag) {
                    to_manage = false;
                    addinQ({false, handle});
                }
            }
        }

        return;        
    }

    // Qui address è la porta restituita dal server MPI che ha fatto la MPI_Open_port
    Handle* connect(const std::string& address) {
        printf("[MPIP2P]Connecting to: %s\n", address.c_str());
        MPI_Comm server_comm;
        MPI_Comm_connect(address.c_str(), MPI_INFO_NULL, 0, MPI_COMM_WORLD, &server_comm);


        HandleMPIP2P* handle = new HandleMPIP2P(this, server_comm, true);

        std::unique_lock lock(shm);
        connections[handle] = false;
        
        return handle;
    }

    void notify_close(Handle* h) {
        std::unique_lock l(shm);
        MPI_Comm_disconnect(&reinterpret_cast<HandleMPIP2P*>(h)->server_comm);
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
        // MPI_Comm c;
        // printf("Trying fake connect\n");
        // MPI_Comm_connect(portname, MPI_INFO_NULL, rank, MPI_COMM_WORLD, &c);

        t1.join();

        MPI_Finalize();
    }

};

#endif