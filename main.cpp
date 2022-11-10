#include <iostream>
#include <vector>
#include "mpi.h"


template<typename T>
class Handle {
    typedef TT T;
    void send(char* buff, size_t size){
        T::getInstance()->send(this, buff, size);
    }

    void receive(char* buff, size_t size){
        T::getInstance()->receive(this, buff, size);
    }

    void yield(){
        T::getInstance()->yield(this);
    }
};

struct ConnType {
    
    template<typename T>
    static T* getInstance() {
        static T ct;
        return &ct;
    }

    virtual void init() = 0;
    virtual void connect(std::string);
    virtual void removeConnection(std::string);
    virtual void removeConnection(Handle<ConnType>*);
    virtual void update(); // chiama il thread del manager
    virtual Handle<ConnType>* getReady();
    virtual void manage(Handle<ConnType>*);
    virtual void unmanage(Handle<ConnType>*);
    virtual void end();

    virtual void send(Handle<ConnType>*, char*, size_t);
    virtual void receive(Handle<ConnType>*, char*, size_t);
    virtual void yield(Handle<ConnType>*);

};

class ConnTcp : public ConnType {
    void init() {

    }
};


class ConnMPI : public ConnType {
   void init(){
        MPI_Init()
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

    void yield(Handle* n){
        // come faccio a sapere il tipo di connessioni in N??

    }

    map<std::string, ConnType*> protocolsMap;

    template<typename T>
    void registerType(std::string s){
        protocolsMap[s] = T::getInstance();
    }

    static void connect(std::string s){

    };
};



int main(int argc, char** argv){
    Manager m(argc,argv);
    //manager.registerProtocol<ConnTcp>("TCP");

    auto handle = Manager::connect<ConnTCP>("TCP://hostname:port");// TCP : hostname:porta // MPI COMM_WORL:rank:tag

    handle.send()
    handle.yield();





    
    handle = getReady();
    if (handle){

    }


    handle.yield();

    return 0;
}