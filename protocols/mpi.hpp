#ifndef MPI_HPP
#define MPI_HPP
#include <handle.hpp>
#include "protocolInterface.hpp"
#include "mpi.h"

#define CONNECTION_TAG 0

class HandleMPI : public Handle {
    int rank;
    int tag;

    HandleMPI(ConnType* parent, int rank, int tag): Handle(parent), rank(rank), tag(tag){}

    void send(char* buff, size_t size){
        MPI_Send(buff, size, MPI_BYTE, rank, tag, MPI_COMM_WORLD);
    }

    void receive(char* buff, size_t size){
        MPI_Recv(buff, size, MPI_BYTE, rank, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
};

class ConnMPI : public ConnType {
public:
    // Supponiamo tutte le comunicazioni avvengano sul comm_world
    // map è <rank, tag> ---> <free, handle object>
    // nuova map è <hash_val> --> <free, <rank,tag>, handle object>
    // std::map<std::pair<int,int>, std::pair<bool, Handle<ConnMPI>*>> handles;

    Handle* lastReady;
    std::map<HandleMPI*, bool> connections;

    void init(){
        MPI_Init(NULL, NULL);
    }


    HandleUser connect(std::string dest) {
        // in pratica questo specifica il tag utilizzato per le comunicazioni successive
        // per ora solo tag, poi si vede

        //parse della stringa

        int header[1];
        header[0] = tag;
        int dest_rank = stoi(dest);
        MPI_Send(header, 1, MPI_INT, dest_rank, CONNECTION_TAG, MPI_COMM_WORLD);

        // creo l'handle
        auto* handle = new HandleMPI(rank. tag);
        connections[handle] = false;    

        return createHandleUser(handle);
    }


    void update() {
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
            // Genera ID univoco per la stringa "rank:tag"
            size_t hash_val = std::hash<std::string>{}(std::to_string(source) + ":" + std::to_string(source_tag));
            handles[hash_val] = std::make_tuple(true, std::make_pair(source, source_tag), new Handle<ConnMPI>(hash_val));
        }

        for (auto &[k, v] : handles) {
            if(std::get<0>(v)) {
                MPI_Iprobe(std::get<1>(v).first, std::get<1>(v).second, MPI_COMM_WORLD, &flag, &status);
                if(flag) {
                     std::get<0>(v) = false;

                    // Qui passare al manager l'handle da restituire all'utente dopo chiamata a getReady?
                    // Sincronizzato?
                    lastReady = std::get<2>(v);
                }
            }
        }
        
    }


    Handle<ConnType>* getReady() {
        // Esplode con un return di questo tipo?
        return (Handle<ConnType>*)lastReady;
    }


    void removeConnection(std::string connection) {
        // Assumendo che la stringa per specificare le connessioni sia rank:tag
        size_t key = std::hash<std::string>{}(connection);
        if(checkFree(key)){
            printf("Handle is busy\n");
            return;
        }

        // Questa ha bisogno di sincronizzazione? Magari sì, visto che la removeConnection
        // potrebbe essere chiamata anche da utente nel caso volesse chiudere la connessione a mano
        // (magari questa per MPI ha poco senso)
        handles.erase(key);
    }

    
    void removeConnection(HandleMPI* handle) {
        if(connections[handle]){
            printf("Handle is busy\n");
            return;
        }
        connections.erase(handle);
    }



    void yield(Handle<ConnType>* handle) {
        size_t key = handle->ID;

        // Per il momento impostiamo a true, ma bisogna vedere come rimuovere il
        // controllo dall'utente che ha fatto yield. Magari con un puntatore offuscato
        // come abbiamo discusso in precedenza
        std::get<0>(handles[key]) = true;
    }


    // Funzione di utilità per vedere se handle è in uso oppure no
    bool checkFree(size_t key) {
        return std::get<0>(handles[key]);
    }
};



#endif