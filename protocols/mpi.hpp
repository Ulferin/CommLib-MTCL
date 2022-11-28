#ifndef MPI_HPP
#define MPI_HPP

#include <vector>
#include <map>
#include <tuple>
#include <shared_mutex>

#include <mpi.h>

#include "../handle.hpp"
#include "../protocolInterface.hpp"

#define CONNECTION_TAG 0

class HandleMPI : public Handle {

public:
    bool closing = false;
    int rank;
    int tag;
    HandleMPI(ConnType* parent, int rank, int tag, bool busy=true): Handle(parent,busy), rank(rank), tag(tag){}

    ssize_t send(const char* buff, size_t size) {
        if (MPI_Send(buff, size, MPI_BYTE, rank, tag, MPI_COMM_WORLD) != MPI_SUCCESS)
            return -1;

        return size;
    }

    ssize_t receive(char* buff, size_t size){
        MPI_Status status; 
        int count;
        int flag = 0;

        while(true){
            MPI_Iprobe(rank, tag, MPI_COMM_WORLD, &flag, &status);
            if (flag) {
                MPI_Recv(buff, size, MPI_BYTE, rank, tag, MPI_COMM_WORLD, &status);
                MPI_Get_count(&status, MPI_BYTE, &count);
                return count;
            }
            else {
                 MPI_Iprobe(rank, -tag, MPI_COMM_WORLD, &flag, &status);
                 if (flag) {
                    closing = true;
                    char in;
                    MPI_Recv(&in, 1, MPI_BYTE, rank, -tag, MPI_COMM_WORLD, &status);
                    return 0;
                 }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
};



class ConnMPI : public ConnType {
protected:
    // Supponiamo tutte le comunicazioni avvengano sul comm_world
    // map è <rank, tag> ---> <free, handle object>
    // nuova map è <hash_val> --> <free, <rank,tag>, handle object>
    // std::map<std::pair<int,int>, std::pair<bool, Handle<ConnMPI>*>> handles;

    int rank;
    std::map<HandleMPI*, bool> connections;
    // std::vector<HandleMPI*> connections;

    std::shared_mutex shm;

public:

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


    Handle* connect(const std::string& dest) {
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
        std::unique_lock lock(shm);
        connections.insert({handle, false});

        return handle;
    }


    void update() {

        std::unique_lock ulock(shm, std::defer_lock);

        int flag;
        MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, CONNECTION_TAG, MPI_COMM_WORLD, &flag, &status);
        if(flag) {
            int headersLen;
            MPI_Get_count(&status, MPI_INT, &headersLen);
            int header[headersLen];
            
            if (MPI_Recv(header, headersLen, MPI_INT, status.MPI_SOURCE, CONNECTION_TAG, MPI_COMM_WORLD, &status) != MPI_SUCCESS) {
                printf("Error on Recv Receiver primo in alto\n");
                throw;
            }
            
            int source = status.MPI_SOURCE;
            int source_tag = header[0];
            HandleMPI* handle = new HandleMPI(this, source, source_tag, false);
            ulock.lock();
            connections.insert({handle, false});
            addinQ({true, handle});
            ulock.unlock();
        }

        

        ulock.lock();
        for (auto& [handle, to_manage] : connections) {
            if(to_manage) {
                MPI_Iprobe(handle->rank, handle->tag, MPI_COMM_WORLD, &flag, &status);
                if (!flag)
                    MPI_Iprobe(handle->rank, -handle->tag, MPI_COMM_WORLD, &flag, &status);

                if (flag) {
                    to_manage = false;
                    addinQ({false, handle});
                }
            }
        }

        ulock.unlock();
        
    }


    void notify_close(Handle* h) {
        HandleMPI* hMPI = reinterpret_cast<HandleMPI*>(h);
        if (!hMPI->closing){
            char a = 0;
            MPI_Send(&a, 1, MPI_BYTE, hMPI->rank, -hMPI->tag, MPI_COMM_WORLD);
        }

        std::unique_lock l(shm);
        connections.erase(reinterpret_cast<HandleMPI*>(h));
    }


    void notify_yield(Handle* h) {
        std::unique_lock l(shm);
        connections[reinterpret_cast<HandleMPI*>(h)] = true;
    }


    void end() {
        auto modified_connections = connections;
        for(auto& [handle, to_manage] : modified_connections)
            if(to_manage)
                setAsClosed(handle);

        MPI_Finalize();
    }
};



#endif