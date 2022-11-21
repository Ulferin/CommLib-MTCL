#ifndef MANAGER_HPP
#define MANAGER_HPP

#include <map>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "handle.hpp"
#include "handleUser.hpp"
#include "protocolInterface.hpp"

#define POLLINGTIMEOUT 10

class Manager {
    friend class ConnType;
    // friend void ConnType::addinQ(std::pair<bool, Handle*>);

    /*NOTE: anche questa deve essere sincronizzata dato che l'utente potrebbe
            voler aggiungere un protocollo dopo l'inizializzazione della libreria*/
    inline static std::map<std::string, std::shared_ptr<ConnType>> protocolsMap;
    
    //TODO: probabilmente una std::deque sincronizzata
    inline static std::queue<std::pair<bool, Handle*>> handleReady;
    // std::queue<Handle*> handleready;
    // std::queue<Handle*> handleNew;

    inline static std::thread t1;
    inline static bool end;
    inline static bool initialized = false;

    inline static std::mutex mutex;
    inline static std::condition_variable condv;

private:
    Manager() {}

    static void addinQ(std::pair<bool, Handle*> el) {
        std::unique_lock lk(mutex);
        handleReady.push(el);
        lk.unlock();
        condv.notify_one();
    }

public:
    // Manager(int argc, char** argv) : argc(argc), argv(argv) {}

    static void init(int argc, char** argv) {
        end = false;
        initialized = true;
        for (auto &el : protocolsMap) {
            el.second->init();
        }

        t1 = std::thread([&](){Manager::getReadyBackend();});

    }

    static void endM() {
        end = true;
        t1.join();

        for (auto [_,v]: protocolsMap) {
            v->end();
        }
    }

    static HandleUser getNext() {
        std::unique_lock lk(mutex);
        condv.wait(lk, [&]{return !handleReady.empty();});

        auto el = handleReady.front();
        handleReady.pop();
        lk.unlock();
        // el.second->incrementReferenceCounter();
        el.second->setBusy(true);

        return HandleUser(el.second, true, el.first);
    }


    static void getReadyBackend() {
        while(!end){
            for(auto& [prot, conn] : protocolsMap) {
                conn->update();
            }
            // To prevent starvation of application threads
            std::this_thread::sleep_for(std::chrono::milliseconds(POLLINGTIMEOUT));
        }
    }


    template<typename T>
    static void registerType(std::string protocol){
        static_assert(std::is_base_of<ConnType,T>::value, "Not a ConnType subclass");
        if(initialized) {
            printf("Manager was already initialized. Impossible to register new protocols.\n");
            return;
        }

        protocolsMap[protocol] = std::shared_ptr<T>(new T);
        

        protocolsMap[protocol]->addinQ = [&](std::pair<bool, Handle*> item){
            Manager::addinQ(item);
        };

    }

    static int listen(std::string s) {
        std::string protocol = s.substr(0, s.find(":"));
        return protocolsMap[protocol]->listen(s.substr(protocol.length(), s.length()));
    }


    static HandleUser connect(std::string s) {

        // TCP:host:port || MPI:rank:tag
        std::string protocol = s.substr(0, s.find(":"));
        printf("[MANAGER]Received connection request for: %s\n", protocol.c_str());
        if(protocol.empty())
            return HandleUser(nullptr, true, true);

        if(protocolsMap.count(protocol)) {
            Handle* handle = protocolsMap[protocol]->connect(s.substr(s.find(":") + 1, s.length()));
            // handle->incrementReferenceCounter();
            if(handle) {
                return HandleUser(handle, true, true);
            }
        }

        return HandleUser(nullptr, true, true);
            
    };

};

#endif