#include <iostream>
#include <vector>
#include <mpi.h>

class ConnType {

    virtual void init();
    virtual void send();
    virtual void receive();
    virtual bool isReady();
};

class ConnTcp : public ConnType {
    
    bool isReady(){
        if (isCLosed) return false;
    }

    Handle getHandle
};

class ConnMPI : public ConnType {
    // rank - tag - comm
    bool isReady(){
        MPI_IProbe(/**/);
        // se pronto return true altrimenti false;
    }
};


class Manager {
    std::vector<ConnType*> connections;
    void* nextReady;

    void* getReady(){
        void* tmp = nextReady;
        nextReady = nullptr;
        // setta closed to false
        return tmp;
    }

    void getReadyBackend(){
        for(auto& c : connections)
            if (c.isReady())
                nextReady = c;
       

        // dormi per n ns
        // riprova da capo
    }
};





int main(int argc, char** argv){
    manager.connect()// TCP : hostname:porta // MPI COMM_WORL:rank:tag

    return 0;
}