#ifndef MANAGER_HPP
#define MANAGER_HPP

#include <csignal>
#include <cstdlib>
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
//#include "protocols/shm.hpp"

#ifdef ENABLE_CONFIGFILE
#include "rapidjson/rapidjson.h"
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/document.h>
#endif

#ifdef ENABLE_MPI
#include "protocols/mpi.hpp"
#endif

#ifdef ENABLE_MPIP2P
#include "protocols/mpip2p.hpp"
#endif

#ifdef ENABLE_MQTT
#include "protocols/mqtt.hpp"
#endif

#ifdef ENABLE_UCX
#include "protocols/ucx.hpp"
#endif

int  mtcl_verbose = -1;

/**
 * Main class for the library
*/
class Manager {
    friend class ConnType;
   
    inline static std::map<std::string, std::shared_ptr<ConnType>> protocolsMap;    
	inline static std::queue<HandleUser> handleReady;
    
    inline static std::string appName;

#ifdef ENABLE_CONFIGFILE
    inline static std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> pools;
    inline static std::map<std::string, std::tuple<std::string, std::vector<std::string>, std::vector<std::string>>> components;
#endif

    REMOVE_CODE_IF(inline static std::thread t1);
    inline static bool end;
    inline static bool initialized = false;

    inline static std::mutex mutex;
    inline static std::condition_variable condv;

private:
    Manager() {}

#if defined(SINGLE_IO_THREAD)
	static inline void addinQ(bool b, Handle* h) {
		handleReady.push(HandleUser(h, true, b));
	}
#else	
    static inline void addinQ(bool b, Handle* h) {
        std::unique_lock lk(mutex);
        handleReady.push(HandleUser(h, true, b));
		condv.notify_one();
    }
#endif
	
    static void getReadyBackend() {
        while(!end){
            for(auto& [prot, conn] : protocolsMap) {
                conn->update();
            }			
			if constexpr (IO_THREAD_POLL_TIMEOUT)
				std::this_thread::sleep_for(std::chrono::microseconds(IO_THREAD_POLL_TIMEOUT));
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
        if ( !ifs.is_open() ) {
			MTCL_ERROR("[Manager]:\t", "parseConfig: cannot open file %s for reading, skip it\n",
					   f.c_str());
            return;
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
						MTCL_ERROR("[Manager]:\t", "parseConfig: one pool element is duplicate on configuration file. I'm overwriting it.\n");
                    
                    pools[name] = std::make_pair(JSONArray2VectorString(c["proxyIPs"].GetArray()), JSONArray2VectorString(c["nodes"].GetArray()));
                } else
					MTCL_ERROR("[Manager]:\t", "parseConfig: an object in pool is not well defined. Skipping it.\n");
        
        if (doc.HasMember("components") && doc["components"].IsArray())
            // components
            for(auto& c : doc["components"].GetArray())
                if (c.IsObject() && c.HasMember("name") && c["name"].IsString() && c.HasMember("host") && c["host"].IsString() && c.HasMember("protocols") && c["protocols"].IsArray()){
                    auto name = c["name"].GetString();
                    if (components.count(name))
						MTCL_ERROR("[Manager]:\t", "parseConfig: one component element is duplicate on configuration file. I'm overwriting it.\n");
					
                    auto listen_strs = (c.HasMember("listen-endpoints") && c["listen-endpoints"].IsArray()) ? JSONArray2VectorString(c["listen-endpoints"].GetArray()) : {};
                    components[name] = std::make_tuple(c["host"].GetString(), JSONArray2VectorString(c["protocols"].GetArray(), listen_strs);
                } else
					  MTCL_ERROR("[Manager]:\t", "parseConfig: an object in components is not well defined. Skipping it.\n");
    }
#endif

public:

    /**
     * \brief Initialization of the manager. Required to use the library
     * 
     * Internally this call creates the backend thread that performs the polling over all registered protocols.
     * 
     * @param appName Application ID for this application instance
     * @param configFile (Optional) Path of the configuration file for the application. It can be a unique configuration file containing both architecture information and application specific information (deployment included).
     * @param configFile2 (Optional) Additional configuration file in the case architecture information and application information are splitted in two separate files. 
    */
    static void init(std::string appName, std::string configFile1 = "", std::string configFile2 = "") {
		std::signal(SIGPIPE, SIG_IGN);

		char *level;
		if ((level=std::getenv("MTCL_VERBOSE"))!= NULL) {
			if (std::string(level)=="all" ||
				std::string(level)=="ALL" ||
				std::string(level)=="max" ||
				std::string(level)=="MAX")
				mtcl_verbose = std::numeric_limits<int>::max(); // print everything
			else
				try {
					mtcl_verbose=std::stoi(level);
					if (mtcl_verbose<=0) mtcl_verbose=1;
				} catch(...) {
					MTCL_ERROR("[Manger]:\t", "invalid MTCL_VERBOSE value, it should be a number or all|ALL|max|MAX\n");
				}
		}
		
        Manager::appName = appName;

		// default transports protocol
        registerType<ConnTcp>("TCP");
		//registerType<ConnSHM>("SHM");

#ifdef ENABLE_MPI
        registerType<ConnMPI>("MPI");
#endif

#ifdef ENABLE_MPIP2P
        registerType<ConnMPIP2P>("MPIP2P");
#endif

#ifdef ENABLE_MQTT
        registerType<ConnMQTT>("MQTT");
#endif

#ifdef ENABLE_UCX
        registerType<ConnUCX>("UCX");
#endif

#ifdef ENABLE_CONFIGFILE
        if (!configFile1.empty()) parseConfig(configFile1);
        if (!configFile2.empty()) parseConfig(configFile2);
#else
     // 
#endif
		end = false;
        for (auto &el : protocolsMap) {
            if (el.second->init(appName) == -1) {
				MTCL_PRINT(100, "[Manager]:\t", "ERROR initializing protocol %s\n", el.first.c_str());
			}
        }
#ifdef ENABLE_CONFIGFILE
        // listen da file di config se ce ne sono
#endif

        REMOVE_CODE_IF(t1 = std::thread([&](){Manager::getReadyBackend();}));

        initialized = true;
    }

    /**
     * \brief Finalize the manger closing all the pending open connections.
     * 
     * From this point on, no more interaction with the library and the manager should be done. Ideally this call must be invoked just before closing the application (return statement of the main function).
     * Internally it stops the polling thread started at the initialization and call the end method of each registered protocols.
    */
    static void finalize() {
		end = true;
        REMOVE_CODE_IF(t1.join());

        for (auto [_,v]: protocolsMap) {
            v->end();
        }
    }

    /**
     * \brief Get an handle ready to receive.
     * 
     * The returned value is an Handle passed by value.
    */  
#if defined(SINGLE_IO_THREAD)
    static inline HandleUser getNext(std::chrono::microseconds us=std::chrono::hours(87600)) {
		if (!handleReady.empty()) {
			auto el = std::move(handleReady.front());
			handleReady.pop();
			return el;
		}
		// if us is not multiple of the IO_THREAD_POLL_TIMEOUT we wait a bit less....
		// if the poll timeout is 0, we just iterate us times
		size_t niter = us.count(); // in case IO_THREAD_POLL_TIMEOUT is set to 0
		if constexpr (IO_THREAD_POLL_TIMEOUT)
			niter = us/std::chrono::milliseconds(IO_THREAD_POLL_TIMEOUT);
		if (niter==0) niter++;
		size_t i=0;
		do { 
			for(auto& [prot, conn] : protocolsMap) {
				conn->update();
			}
			if (!handleReady.empty()) {
				auto el = std::move(handleReady.front());
				handleReady.pop();
				return el;
			}
			if (i >= niter) break;
			++i;
			if constexpr (IO_THREAD_POLL_TIMEOUT)
				std::this_thread::sleep_for(std::chrono::microseconds(IO_THREAD_POLL_TIMEOUT));	
		} while(true);
		return HandleUser(nullptr, true, true);
    }	
#else	
    static inline HandleUser getNext(std::chrono::microseconds us=std::chrono::hours(87600)) { 
        std::unique_lock lk(mutex);
        if (condv.wait_for(lk, us, [&]{return !handleReady.empty();})) {
			auto el = std::move(handleReady.front());
			handleReady.pop();
			lk.unlock();
			return el;
		}
        return HandleUser(nullptr, true, true);
    }
#endif
	
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
			MTCL_ERROR("[Manager]:\t", "The Manager has been already initialized. Impossible to register new protocols.\n");
            return;
        }

        protocolsMap[name] = std::shared_ptr<T>(new T);
        
        protocolsMap[name]->addinQ = [&](bool b, Handle* h){ Manager::addinQ(b,h); };

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
			errno=EPROTO;
            return -1;
        }
        return protocolsMap[protocol]->listen(s.substr(protocol.length()+1, s.length()));
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
