#ifndef MANAGER_HPP
#define MANAGER_HPP

#include <map>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "handle.hpp"
#include "handleUser.hpp"
#include "protocolInterface.hpp"
#include "protocols/tcp.hpp"

#ifdef ENABLE_CONFIGFILE
#include "rapidjson/rapidjson.h"
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/document.h>
#endif

#ifdef ENABLE_MPI
#include "protocols/mpi.hpp"
#include "protocols/mpip2p.hpp"
#endif

#ifdef ENABLE_MQTT
#include "protocols/mqtt.hpp"
#endif

#define POLLINGTIMEOUT 10
/**
 * Main class for the library
*/
class Manager {
    friend class ConnType;
   
    inline static std::map<std::string, std::shared_ptr<ConnType>> protocolsMap;
    
    //TODO: probabilmente una std::deque sincronizzata
    inline static std::queue<std::pair<bool, Handle*>> handleReady;

#ifdef ENABLE_CONFIGFILE
    inline static std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> pools;
    inline static std::map<std::string, std::tuple<std::string, std::vector<std::string>, std::vector<std::string>>> components;
#endif

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

    static void getReadyBackend() {
        while(!end){
            for(auto& [prot, conn] : protocolsMap) {
                conn->update();
            }
            // To prevent starvation of application threads
            std::this_thread::sleep_for(std::chrono::milliseconds(POLLINGTIMEOUT));
        }
    }

#ifdef ENABLE_CONFIGFILE
    static std::vector<std::string> JSONArray2VectorString(rapidjson::GenericArray& arr){
        std::vector<std::string> output;
        for(auto& e : arr)  
            output.push_back(e.GetString());
        
        return output;
    }
    
    static void parseConfig(std::string& f){
        std::ifstream ifs(f);
        if ( !ifs.is_open() )
        {
            std::cerr << "Could not open file for reading!\n";
            return EXIT_FAILURE;
        }
        rapidjson::IStreamWrapper isw { ifs };
        rapidjson::Document doc;
        doc.ParseStream( isw );

        assert(doc.IsObject());

        if (doc.HasMember("pools") && doc["pools"].IsArray())
            // architecture 
            for (auto& c : doc["pools"].GetArray())
                if (c.IsObject() && c.HasMember("name") && c["name"].IsString() && c.HasMember("proxyIPs") && c["proxyIPs"].IsArray() && c.HasMember("nodes") && c["nodes"].IsArray()){
                    auto name = c["name"].GetString();
                    if (pools.count(name)) 
                        std::cerr << "One pool element is duplicate on configuration file. I'm overwriting it.\n";
                    
                    pools[name] = std::make_pair(JSONArray2VectorString(c["proxyIPs"].GetArray()), JSONArray2VectorString(c["nodes"].GetArray()));
                } else 
                        std::cerr << "A object in pool is not well defined. Skipping it.\n";
        
        if (doc.HasMember("components") && doc["components"].IsArray())
            // components
            for(auto& c : doc["components"].GetArray())
                if (c.IsObject() && c.HasMember("name") && c["name"].IsString() && c.HasMember("host") && c["host"].IsString() && c.HasMember("protocols") && c["protocols"].IsArray()){
                    auto name = c["name"].GetString();
                    if (components.count(name))
                        std::cerr << "One component element is duplicate on configuration file. I'm overwriting it.\n";
                    auto listen_strs = (c.HasMember("listen-endpoints") && c["listen-endpoints"].IsArray()) ? JSONArray2VectorString(c["listen-endpoints"].GetArray()) : {};
                    components[name] = std::make_tuple(c["host"].GetString(), JSONArray2VectorString(c["protocols"].GetArray(), listen_strs);
                } else
                    std::cerr << "A object in components is not well defined. Skipping it.\n";
    }
#endif

public:

    /**
     * \brief Initialization of the manager. Required to use the library
     * 
     * Internally this call create the backend thread that performs the polling over all registered protocols.
     * 
     * @param configFile (Optional) Path of the configuration file for the application. It can be a unique configuration file containing both architecture information and application specific information (deployment included).
     * @param configFile2 (Optional) Additional configuration file in the case architecture information and application information are splitted in two separate files. 
    */
    static void init(std::string configFile1 = "", std::string configFile2 = "") {
        end = false;
        initialized = true;

        registerType<ConnTcp>("TCP");

#ifdef ENABLE_MPI
        registerType<ConnMPI>("MPI");
        registerType<ConnMPIP2P>("MPIP2P");
#endif

#ifdef ENABLE_MQTT
        registerType<ConnMQTT>("MQTT");
#endif

#ifdef ENABLE_CONFIGFILE
        if (!configFile1.empty()) parseConfig(configFile1);
        if (!configFile2.empty()) parseConfig(configFile2);
#else
     // 
#endif

        for (auto &el : protocolsMap) {
            el.second->init();
        }
#ifdef ENABLE_CONFIGFILE
        // listen da file di config se ce ne sono
#endif
        t1 = std::thread([&](){Manager::getReadyBackend();});

    }

    /**
     * \brief Finalize the manger closing all the pending open connections.
     * 
     * From this point on, no more interaction with the library and the manager should be done. Ideally this call must be invoked just before closing the application (return statement of the main function).
     * Internally it stops the polling thread started at the initialization and call the end method of each registered protocols.
    */
    static void finalize() {
        end = true;
        t1.join();

        for (auto [_,v]: protocolsMap) {
            v->end();
        }
    }

    /**
     * \brief Get an handle that is ready to receive.
     * 
     * The function is blocking in case there are no ready handles. The returned value is and Handle passed by value.
    */
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

    /**
     * \brief Same as getNext method but return an handle stored in heap.
    */
    /*static HandleUser* getNextPtr() {
        return new HandleUser(std::move(getNext()));
    }*/

    /**
     * \brief Create an instance of the protocol implementation.
     * 
     * @tparam T class representing the implementation of the protocol being register
     * @param name string representing the name of the instance of the protocol
    */
    template<typename T>
    static void registerType(std::string name){
        static_assert(std::is_base_of<ConnType,T>::value, "Not a ConnType subclass");
        if(initialized) {
            printf("Manager was already initialized. Impossible to register new protocols.\n");
            return;
        }

        protocolsMap[name] = std::shared_ptr<T>(new T);
        
        protocolsMap[name]->addinQ = [&](std::pair<bool, Handle*> item){
            Manager::addinQ(item);
        };

        protocolsMap[name]->instanceName = name;
    }

    /**
     * \brief Listen on incoming connections.
     * 
     * Perform the listen operation on a particular protocol and parameters given by the string passed as parameter.
     * 
     * @param connectionString URI containing parameters to perform the effective listen operation
    */
    static int listen(std::string s) {
        std::string protocol = s.substr(0, s.find(":"));
        
        if (!protocolsMap.count(protocol)){
            std::cerr << "Protocol not registered in the runtime!\n";
            return -1;
        }

        return protocolsMap[protocol]->listen(s.substr(protocol.length(), s.length()));
    }

    /**
     * \brief Connect to a peer
     * 
     * Connect to a peer following the URI passed in the connection string or following a label defined in the configuration file.
     * The URI is of the form "PROTOCOL:param:param: ... : param"
     * 
     * @param connectionString URI of the peer or label 
    */
    static HandleUser connect(std::string s) {
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

    /**
     * \brief Given an handle return the name of the protocol instance given in phase of registration.
     * 
     * Example. If TCP implementation was registered as Manager::registerType<ConnTcp>("EX"), this function on handles produced by that kind of protocol instance will return "EX".
    */
    static std::string getTypeOfHandle(HandleUser& h){
        return h.realHandle->parent->instanceName;
    }

};

#endif