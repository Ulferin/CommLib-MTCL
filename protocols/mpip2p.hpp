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


void signal_handler(int signal_num)
{
    std::cout << "The interrupt signal is (" << signal_num
         << "). \n";
  
    pthread_exit(0);
}

void userDefinedErrHandler( MPI_Comm *comm, int *err, ... )
{
    if (*err != MPI_ERR_OTHER) {
      printf( "Unexpected error code\n" );fflush(stdout);
    }
    
    printf("Inside error handler\n");fflush(stdout);
    return;
}


class HandleMPIP2P : public Handle {

public:
    MPI_Comm server_comm; // MPI communicator for the specific p2p connection
    HandleMPIP2P(ConnType* parent, MPI_Comm server_comm, bool busy=true) : Handle(parent, busy), server_comm(server_comm) {}

    ssize_t send(const char* buff, size_t size) {
        int size_conn;
        MPI_Comm_size(server_comm, &size_conn);
        printf("size: %d\n", size_conn);
        MPI_Send(buff, size, MPI_BYTE, 0, 0, server_comm);

        return size;
    }

    ssize_t receive(char* buff, size_t size){
        MPI_Status status; 
        int count;
        int rank_conn;
        MPI_Comm_rank(server_comm, &rank_conn);
        printf("Receiving on %d\n", rank_conn);
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

        // MPI_Comm_set_errhandler(MPI_COMM_WORLD, errhdl);
        // MPI_Comm_create_errhandler(&userDefinedErrHandler, &errhdl);
        // MPI_Comm_set_errhandler(MPI_COMM_WORLD, errhdl);
        // MPI_Comm_set_errhandler(MPI_COMM_SELF, errhdl);
        return 0;
    }

    void _listen(char* portname) {
        signal(SIGUSR1, signal_handler);

        listening = true;
        MPI_Publish_name("test_server", MPI_INFO_NULL, portname);
        
        while(!finalized) {
            MPI_Comm client;
            MPI_Comm_accept(portname, MPI_INFO_NULL, 0, MPI_COMM_WORLD, &client);

            HandleMPIP2P* handle = new HandleMPIP2P(this, client, false);
            
            std::unique_lock ulock(shm);
            connections.insert({handle, false});
            addinQ({true, handle});
        }

        printf("finalized!\n");
        
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
        // char portname[MPI_MAX_PORT_NAME]; 
        MPI_Lookup_name("test_server", MPI_INFO_NULL, portname);
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



        if(listening) {
            MPI_Comm c;
            char* a[2];
            a[0] = portname;
            a[1] = NULL;
            MPI_Comm_spawn("stop_accept", a, 1, MPI_INFO_NULL, 0, MPI_COMM_SELF, &c, MPI_ERRCODES_IGNORE);
            // Fake abort
            // int pid = fork();
            // if(pid == 0) {
            //     MPI_Init(0, NULL);
            //     printf("Pid 0\n");
            //     fflush(stdout);
                // char port[MPI_MAX_PORT_NAME];
            //     // MPI_Lookup_name("test_server", MPI_INFO_NULL, port);
            //     printf("port: %s\n", portname);
            // MPI_Comm_connect(portname, MPI_INFO_NULL, 0, MPI_COMM_WORLD, &c);
            //     printf("Trying fake connect\n");
            //     MPI_Finalize();
            //     exit(0);
            // }

            // Cancelling thread
            // pthread_cancel(t1.native_handle());
            // pthread_kill(t1.native_handle(), SIGUSR1);
            // printf("dopo kill\n");
            // printf("Killing the thread\n");
            t1.join();
            // printf("dopo join\n");
            // t1.std::thread::~thread();
            

            // Aborting MPI
            // printf("Detached\n");
            // MPI_Abort(MPI_COMM_SELF, 1);
            // t1.join();
        }

        MPI_Finalize();
        printf("Finalized\n");
    }

};

#endif