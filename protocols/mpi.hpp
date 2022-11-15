#ifndef MPI_HPP
#define MPI_HPP
#include <handle.hpp>
#include "protocolInterface.hpp"
#include "mpi.h"
#include <vector>
#include <assert.h>
#include <tuple>

#define CONNECTION_TAG 0

class HandleMPI : public Handle {

public:
    int rank;
    int tag;
    HandleMPI(ConnType* parent, int rank, int tag, bool busy=true): Handle(parent,busy), rank(rank), tag(tag){}

    size_t send(char* buff, size_t size) {
        MPI_Send(buff, size, MPI_BYTE, rank, tag, MPI_COMM_WORLD);

        return size;
    }

    size_t receive(char* buff, size_t size){
        MPI_Status status; 
        int count;
        MPI_Recv(buff, size, MPI_BYTE, rank, tag, MPI_COMM_WORLD, &status);

        MPI_Get_count(&status, MPI_BYTE, &count);
        
        return count;
    }
};

class ConnMPI : public ConnType {
public:
    // Supponiamo tutte le comunicazioni avvengano sul comm_world
    // map è <rank, tag> ---> <free, handle object>
    // nuova map è <hash_val> --> <free, <rank,tag>, handle object>
    // std::map<std::pair<int,int>, std::pair<bool, Handle<ConnMPI>*>> handles;

    Handle* lastReady;
    int rank;
    // std::map<HandleMPI*, bool> connections;
    std::vector<HandleMPI*> connections;

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

    int listen(std::string) {
        return 0;
    }


    Handle* connect(std::string dest) {
        // in pratica questo specifica il tag utilizzato per le comunicazioni successive
        // per ora solo tag, poi si vede

        //parse della stringa
        int rank = stoi(dest.substr(0, dest.find(":")));
        int tag = stoi(dest.substr(dest.find(":") + 1, dest.length()));
        
        int header[1];
        header[0] = tag;
        MPI_Send(header, 1, MPI_INT, rank, CONNECTION_TAG, MPI_COMM_WORLD);

        // creo l'handle
        auto* handle = new HandleMPI(this, rank, tag, true);
        connections.push_back(handle);    

        return handle;
    }


    void update(std::queue<Handle*>& q, std::queue<Handle*>& qnew) {
        int flag;
        MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, CONNECTION_TAG, MPI_COMM_WORLD, &flag, &status);
        if(flag) {
            // Qui dobbiamo gestire la nuova connessione e aggiungerla a quelle libere
            
            int headersLen;
            MPI_Get_count(&status, MPI_LONG, &headersLen);
            int header[headersLen];
            int rank = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            
            if (MPI_Recv(header, headersLen, MPI_INT, status.MPI_SOURCE, CONNECTION_TAG, MPI_COMM_WORLD, &status) != MPI_SUCCESS) {
                printf("Error on Recv Receiver primo in alto\n");
                //NOTE: assert(false) !!!!
                assert(false);
            }
            
            int source = status.MPI_SOURCE;
            int source_tag = header[0];
            HandleMPI* handle = new HandleMPI(this, source, source_tag, true);
            connections.push_back(handle);
            qnew.push(handle);
        }

        for (HandleMPI* el : connections) {
            if(!el->isBusy()) {
                MPI_Iprobe(el->rank, el->tag, MPI_COMM_WORLD, &flag, &status);
                if(flag) {
                    q.push(el);
                }
            }
        }
        
    }

    void notify_yield(Handle* h) {
        return;
    }

    void notify_request(Handle* h) {
        return;
    }
    
    void removeConnection(HandleMPI* handle) {
        // if(connections[handle]){
        //     printf("Handle is busy\n");
        //     return;
        // }
        // connections.erase(handle);
        return;
    }
};



#endif