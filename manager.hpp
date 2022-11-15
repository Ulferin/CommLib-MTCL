#ifndef MANAGER_HPP
#define MANAGER_HPP

#include <map>
#include <vector>

#include "handle.hpp"
#include "handleUser.hpp"
#include "protocolInterface.hpp"

#include <queue>


class Manager {
    std::map<std::string, ConnType*> protocolsMap;
    
    //TODO: Code sincronizzate
    std::queue<Handle*> handleready;
    std::queue<Handle*> handleNew;

    int argc;
    char** argv;

    bool end = false;

public:
    Manager(int argc, char** argv) : argc(argc), argv(argv) {}

    void init() {
        for (auto &&el : protocolsMap) {
            el.second->init();
        }
    }

    void endM() {
        end = true;
    }

    std::optional<HandleUser> getReady(){
        if(handleready.empty())
            return {};

        auto el = handleready.front();
        el->setBusy(true);
        handleready.pop();
        return {HandleUser(el, true)};
    }


    std::optional<HandleUser> getNewConnection() {
        if(handleNew.empty())
            return {};

        auto el = handleNew.front();
        el->setBusy(true);
        handleNew.pop();
        return {HandleUser(el,true)};
    }


    void getReadyBackend(){
        while(!end){
            for(auto& [prot, conn] : protocolsMap) {
                conn->update(handleready, handleNew);
            }
        }
    }


    template<typename T>
    static ConnType* createConnType(std::string s) {
        static_assert(std::is_base_of<ConnType,T>::value, "Not a ConnType subclass");
        return new T(s);
    }


    template<typename T>
    void registerType(std::string s){
        std::string protocol = s.substr(0, s.find(":"));
        protocolsMap[protocol] = createConnType<T>(s);
    }


    std::optional<HandleUser> connect(std::string s){
        // parsing protocollo
        // connect di ConnType dalla mappa dei protocolli

        // TCP:host:port || MPI:rank:tag
        std::string protocol = s.substr(0, s.find(":"));
        printf("[MANAGER]Received connection request for: %s\n", protocol.c_str());
        if(protocol.empty())
            return {};

        if(protocolsMap.count(protocol)) {
            Handle* handle = protocolsMap[protocol]->connect(s.substr(s.find(":") + 1, s.length()));
            if(handle) {
                return {HandleUser(handle, true)};
            }
        }

        return {};
            
    };
};

#endif