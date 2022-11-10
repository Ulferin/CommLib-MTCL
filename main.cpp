#include <iostream>
#include <vector>
#include "mpi.h"



class Handle {
    virtual void send();
    virtual void receive();
    virtual void test();
};

class ConnType {

    virtual void init();
    virtual void* getReady();
    virtual void manage(void* handle);
    virtual void unmanage(void* handle);

};

class ConnTcp : public ConnType {
    // mutex per gestione del set (tutte le operazioni sul set)
    // map tra descriptor e handle;
    // set 
    void* getReady(){
        lock()
        select(timeout);
         // se nuova connessione aggiuni nella mappa e crea nuovo handle e modifica il set
        unlock()
        // select TCP
    }

    void manage(void* h){
        lock();
        // aggiunge al set
        unlock();
    }
};

class HandleTCP : public Handle {
    
};



class ConnMPI : public ConnType {
    //map tra std::pair<std::atomic<bool>, tuple<int, int, int>> -> handle
    // se
    void*  getReady(){
        // check nuove connessioni con IProbe(ANY_SOURCE, TAG_NUOVE_CONNESSIONI);

        for(handle) IProbe 
        //MPI_IProbe(/**/);
        // se pronto return true altrimenti false;
    }
};


class Manager {
    std::vector<ConnType*> protocols;
    void* nextReady;

    void* getReady(){
        void* tmp = nextReady;
        nextReady = nullptr;
        // setta closed to false
        return tmp;
    }

    void getReadyBackend(){
        while(true){
            for(auto& c : protocols){
                auto* ready = c.getReady();
                if (ready) nextReady = ready;
            }
            nanosleep(2000);
        }
        // dormi per n ns
        // riprova da capo
    }

    void connect(/* tipo connessione TCP:MPI, parametri connessione*/);
};


class Handle{
    void yield(){
        bool blocked = true;
    
       // comunica al manager che deve gestire questo handle (modifica il bool se mpi, aggiungi nel set se TCP)
    } 

    void send (){
        if (blocked) throw err;
    }
}


int main(int argc, char** argv){
    manager.registerProtocol<ConnTcp>("TCP");

    auto handle = manager.connect<ConnTCP>("hostname:port");// TCP : hostname:porta // MPI COMM_WORL:rank:tag

    handle.send()
    handle.yield();
    
    handle = getReady();
    if (handle){

    }


    handle.yield();

    return 0;
}