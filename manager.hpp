#ifndef MANAGER_HPP
#define MANAGER_HPP

#include <map>
#include <vector>

#include "handle.hpp"
#include "handleUser.hpp"
#include "protocolInterface.hpp"

#include <queue>

class Manager {
    /*NOTE: anche questa deve essere sincronizzata dato che l'utente potrebbe
            voler aggiungere un protocollo dopo l'inizializzazione della libreria*/
    inline static std::map<std::string, ConnType*> protocolsMap;
    
    //TODO: probabilmente una std::deque sincronizzata
    inline static std::queue<std::pair<bool, Handle*>> handleReady;
    // std::queue<Handle*> handleready;
    // std::queue<Handle*> handleNew;

    inline static std::thread t1;
    inline static bool end;


private:
    Manager() {}

public:
    // Manager(int argc, char** argv) : argc(argc), argv(argv) {}

    static void init(int argc, char** argv) {
        end = false;
        for (auto &el : protocolsMap) {
            el.second->init();
        }

        t1 = std::thread([&](){Manager::getReadyBackend();});

    }

    static void endM() {
        end = true;
        t1.join();
    }

    // HandleUser getReady(){
    //     if(handleready.empty())
    //         return HandleUser(nullptr, true);

    //     auto el = handleready.front();
    //     el->setBusy(true);
    //     handleready.pop();

        
    //     return HandleUser(el,true);    
    // }


    // HandleUser getNewConnection() {
    //     if(handleNew.empty())
    //         return HandleUser(nullptr, true);

    //     auto el = handleNew.front();
    //     el->setBusy(true);
    //     handleNew.pop();
    //     return HandleUser(el,true);
    // }

    static HandleUser getNext() {
        if(handleReady.empty())
            return HandleUser(nullptr, true, true);

        auto el = handleReady.front();
        el.second->setBusy(true);
        handleReady.pop();
        return HandleUser(el.second, true, el.first);
    }


    static void getReadyBackend() {
        while(!end){
            for(auto& [prot, conn] : protocolsMap) {
                conn->update(handleReady);
            }
        }
    }


    template<typename T>
    static ConnType* createConnType() {
        static_assert(std::is_base_of<ConnType,T>::value, "Not a ConnType subclass");
        return new T;
    }


    template<typename T>
    static void registerType(std::string protocol){
        ConnType* conn = createConnType<T>();
        //NOTE: init() direttamente nel costruttore di ConnType???
        // conn->init();
        protocolsMap[protocol] = conn;
    }

    static int listen(std::string s) {
        std::string protocol = s.substr(0, s.find(":"));
        return protocolsMap[protocol]->listen(s.substr(protocol.length(), s.length()));
    }


    static HandleUser connect(std::string s){
        // parsing protocollo
        // connect di ConnType dalla mappa dei protocolli

        // TCP:host:port || MPI:rank:tag
        std::string protocol = s.substr(0, s.find(":"));
        printf("[MANAGER]Received connection request for: %s\n", protocol.c_str());
        if(protocol.empty())
            return HandleUser(nullptr, true, true);

        if(protocolsMap.count(protocol)) {
            Handle* handle = protocolsMap[protocol]->connect(s.substr(s.find(":") + 1, s.length()));
            if(handle) {
                return HandleUser(handle, true, true);
            }
        }

        return HandleUser(nullptr, true, true);
            
    };
};

#endif