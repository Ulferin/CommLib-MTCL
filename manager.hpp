#ifndef MANAGER_HPP
#define MANAGER_HPP

#include <map>
#include <vector>

#include "handle.hpp"
#include "handleUser.hpp"
#include "protocolInterface.hpp"


class Manager {
    std::map<std::string, ConnType*> protocolsMap;
    std::vector<ConnType*> protocols;
    void* nextReady; // coda thread safe 1 produttore (thread che fa polling) n consumatori che sono quelli che chiamano getReady

    void* getReady(){
        void* tmp = nextReady;
        nextReady = nullptr;
        // setta closed to false
        return tmp;
    }

    void getReadyBackend(){
        while(true){
            for(auto& c : protocols){
                void* ready = c->getReady();
                if (ready) nextReady = ready;
            }
        }
        // dormi per n ns
        // riprova da capo
    }

    void yield(Handle* n){
        // come faccio a sapere il tipo di connessioni in N??

    }

    // Qualcosa così per creare gli oggetti associati ad un sottotipo?
    template<typename T>
    ConnType* createConnType() {
        static_assert(std::is_base_of<ConnType,T>::value, "Not a ConnType subclass");
        return new T;
    }

    template<typename T>
    void registerType(std::string s){
        protocolsMap[s] = createConnType<T>();
    }

    static void connect(std::string s){

    };
};

#endif