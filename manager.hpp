#ifndef MANAGER_HPP
#define MANAGER_HPP

#include <map>
#include <vector>

#include "handle.hpp"
#include "handleUser.hpp"
#include "protocolInterface.hpp"

#include <queue>


class Manager {
public:
    std::map<std::string, ConnType*> protocolsMap;
    std::vector<ConnType*> protocols;

    // Code sincronizzate
    std::queue<Handle*> handleready;
    std::queue<Handle*> handleNew;

    std::optional<HandleUser> getReady(){
        // NOTE: Chekc empty
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
        while(true){
            for(auto& c : protocols){
                c->update(handleready, handleNew);
            }
        }
    }

    // Qualcosa così per creare gli oggetti associati ad un sottotipo?
    template<typename T>
    static ConnType* createConnType() {
        static_assert(std::is_base_of<ConnType,T>::value, "Not a ConnType subclass");
        return new T;
    }

    template<typename T>
    void registerType(std::string s){
        protocolsMap[s] = createConnType<T>();
    }

    std::optional<HandleUser> connect(std::string s){
        // parsing protocollo
        // connect di ConnType dalla mappa dei protocolli

        // TCP:host:port || MPI:rank:tag
        std::string protocol = s.substr(0, s.find(":"));
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