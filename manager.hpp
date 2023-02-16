#ifndef MANAGER_HPP
#define MANAGER_HPP

#include <csignal>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <sstream>

#include "handle.hpp"
#include "handleUser.hpp"
#include "protocolInterface.hpp"
#include "protocols/tcp.hpp"
#include "protocols/shm.hpp"
// #define ENABLE_CONFIGFILE
#ifdef ENABLE_CONFIGFILE
#include <fstream>
#include <rapidjson/rapidjson.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/document.h>
#include "collectives/collectiveContext.hpp"
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
    inline static std::map<std::string, std::vector<Handle*>> groupsReady;
    inline static std::set<std::string> listening_endps;
    
    inline static std::string appName;
    inline static std::string poolName;

#ifdef ENABLE_CONFIGFILE
    inline static std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> pools;
    inline static std::map<std::string, std::tuple<std::string, std::vector<std::string>, std::vector<std::string>>> components;
#endif

    REMOVE_CODE_IF(inline static std::thread t1);
    inline static bool end;
    inline static bool initialized = false;

    inline static std::mutex mutex;
    inline static std::mutex group_mutex;
    inline static std::condition_variable condv;
    inline static std::condition_variable group_cond;

private:
    Manager() {}

#if defined(SINGLE_IO_THREAD)
	static inline void addinQ(bool b, Handle* h) {
        int collective = 0;

        if(b) {
            size_t size;
            h->probe(size, true);
            h->receive(&collective, sizeof(int));

            // Se collettiva, leggo il teamID e aggiorno il contesto
            // Questo serve per sincronizzazione del root sulle accept
            if(collective) {
                size_t size;
                h->probe(size, true);
                char* teamID = new char[size+1];
                h->receive(teamID, size);
                teamID[size] = '\0';

                printf("Received connection for team: %s\n", teamID);

                if(groupsReady.count(teamID) == 0)
                    groupsReady.emplace(teamID, std::vector<Handle*>{});

                groupsReady.at(teamID).push_back(h);
                delete[] teamID;
                return;
            }
        }

		handleReady.push(HandleUser(h, true, b));
	}
#else	
    static inline void addinQ(bool b, Handle* h) {

        // new connection, read handle type (p2p=0, collective=1)
        //
        // Con mappa del contesto riesco a separare due 
        // Se l'handle è associato alla collettiva manda ulteriori
        // dati con "participants:root:type"
        // Con quella stringa possiamo associare uno ed un solo contesto a tutti
        // gli handle della stessa collettiva in modo da aggiornare stato della
        // collettiva da qui in avanti
        int collective = 0;

        // Ad ogni nuova connessione controllo se l'handle è riferito a collettiva
        if(b) {
            size_t size;
            h->probe(size, true);
            h->receive(&collective, sizeof(int));
        
            // Se collettiva, leggo il teamID e aggiorno il contesto
            // Questo serve per sincronizzazione del root sulle accept
            if(collective) {
                size_t size;
                h->probe(size, true);
                char* teamID = new char[size+1];
                h->receive(teamID, size);
                teamID[size] = '\0';

                printf("Received connection for team: %s\n", teamID);

                std::unique_lock lk(group_mutex);
                if(groupsReady.count(teamID) == 0)
                    groupsReady.emplace(teamID, std::vector<Handle*>{});

                groupsReady.at(teamID).push_back(h);
                delete[] teamID;
                group_cond.notify_one();
                return;
            }
        }

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


            // for(HandleGroup hg : groups) {
            //     bool ready = hg->update;
            //     if(ready) addinQ(hg,....);
            // }
        }
    }

#ifdef ENABLE_CONFIGFILE
    template <bool B, typename T>
    static std::vector<std::string> JSONArray2VectorString(const rapidjson::GenericArray<B, T>& arr){
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

        if (doc.HasMember("pools") && doc["pools"].IsArray()){
            // architecture 
            for (auto& c : doc["pools"].GetArray())
                if (c.IsObject() && c.HasMember("name") && c["name"].IsString() && c.HasMember("proxyIp") && c["proxyIp"].IsArray() && c.HasMember("nodes") && c["nodes"].IsArray()){
                    auto name = c["name"].GetString();
                    if (pools.count(name))
						MTCL_ERROR("[Manager]:\t", "parseConfig: one pool element is duplicate on configuration file. I'm overwriting it.\n");
                    
                    pools[name] = std::make_pair(JSONArray2VectorString(c["proxyIp"].GetArray()), JSONArray2VectorString(c["nodes"].GetArray()));
                } else
					MTCL_ERROR("[Manager]:\t", "parseConfig: an object in pool is not well defined. Skipping it.\n");
        }
        if (doc.HasMember("components") && doc["components"].IsArray()){
            // components
            for(auto& c : doc["components"].GetArray())
                if (c.IsObject() && c.HasMember("name") && c["name"].IsString() && c.HasMember("host") && c["host"].IsString() && c.HasMember("protocols") && c["protocols"].IsArray()){
                    auto name = c["name"].GetString();
                    if (components.count(name))
						MTCL_ERROR("[Manager]:\t", "parseConfig: one component element is duplicate on configuration file. I'm overwriting it.\n");
					
                    auto listen_strs = (c.HasMember("listen-endpoints") && c["listen-endpoints"].IsArray()) ? JSONArray2VectorString(c["listen-endpoints"].GetArray()) : std::vector<std::string>();
                    components[name] = std::make_tuple(c["host"].GetString(), JSONArray2VectorString(c["protocols"].GetArray()), listen_strs);
                } else
					  MTCL_ERROR("[Manager]:\t", "parseConfig: an object in components is not well defined. Skipping it.\n");
        }
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
		registerType<ConnSHM>("SHM");

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

        // if the current appname is not found in configuration file, abort the execution.
        if (components.find(appName) == components.end()){
            MTCL_ERROR("[Manager]", "Component %s not found in configuration file\n", appName.c_str());
            abort();
        }

        // set the pool name if in the host definition
        poolName = getPoolFromHost(std::get<0>(components[appName]));

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
        // Automatically listen from endpoints listed in config file
        for(auto& le : std::get<2>(components[Manager::appName])){
            Manager::listen(le);
        }
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
        // Check if Manager has already this endpoint listening
        if(listening_endps.count(s) != 0) return 0;
        listening_endps.insert(s);

        std::string protocol = s.substr(0, s.find(":"));
        
        if (!protocolsMap.count(protocol)){
			errno=EPROTO;
            return -1;
        }
        return protocolsMap[protocol]->listen(s.substr(protocol.length()+1, s.length()));
    }


    static Handle* connectHandle(std::string s) {
        std::string protocol = s.substr(0, s.find(":"));
       
        if(protocol.empty()){
            // vedo se uso il file di config e provo a ciclo tutte le listen del componente di destinazione 
#ifdef ENABLE_CONFIGFILE
            if (components.count(s))
                for(auto& le : std::get<2>(components[s])){
                    std::string sWoProtocol = le.substr(le.find(":") + 1, le.length());
                    std::string remote_protocol = le.substr(0, le.find(":"));
                    if (protocolsMap.count(remote_protocol)){
                        auto* h = protocolsMap[remote_protocol]->connect(sWoProtocol);
                        if (h) return h;
                    }
                }
#endif
            // stampa di errore??
            return nullptr;
        }

        // POSSIBILE LABEL
        std::string appLabel = s.substr(s.find(":") + 1, s.length());

        #ifdef ENABLE_CONFIGFILE
            if (components.count(appLabel)){ // l'utente ha scritto una label effettivamente
                auto& component = components.at(appLabel);
                std::string& host = std::get<0>(component); // host= [pool:]hostname
                std::string pool = getPoolFromHost(host);

                    if (pool != poolName){ // se entrambi vuoti o uguali non entro 
                        std::string connectionString2Proxy;
                        if (poolName.empty() && !pool.empty()){ // passo dal proxy del pool di destinazione
                            // connect verso il proxy di pool
                            if (protocol == "UCX" || protocol == "TCP"){
                               for (auto& ip: pools[pool].first){
                                // if the ip contains a port is better to skip it, probably is a tunnel used betweens proxies
                                Handle* handle;
				if (ip.find(":") != std::string::npos) 
                                	handle = protocolsMap["TCP"]->connect(ip);
				else
					 handle = protocolsMap[protocol]->connect(ip + ":" + (protocol == "UCX" ? "13001" : "13000"));
                                //handle->send(s.c_str(), s.length());
                                if (handle){
					handle->send(s.c_str(), s.length());
				       	return handle;
				}
                               }
                            } else {
                                auto* handle = protocolsMap[protocol]->connect("PROXY-" + pool);
                                handle->send(s.c_str(), s.length());
                                if (handle) return handle;
                            }
                        }
                    
                        if (!poolName.empty() && !pool.empty()){ // contatto il mio proxy
                            if (protocol == "UCX" || protocol == "TCP"){
                               for (auto& ip: pools[poolName].first){
                                auto* handle = protocolsMap[protocol]->connect(ip + ":" + (protocol == "UCX" ? "13001" : "13000"));
                                handle->send(s.c_str(), s.length());
                                if (handle) return handle;
                               }
                            } else {
                                auto* handle = protocolsMap[protocol]->connect("PROXY-" + poolName);
                                handle->send(s.c_str(), s.length());
                                if (handle) return handle;
                            }
                        }
                        return nullptr;
                    } else {
                        // connessione diretta
                        for (auto& le : std::get<2>(component))
                            if (le.find(protocol) != std::string::npos){
                                auto* handle = protocolsMap[protocol]->connect(le.substr(le.find(":") + 1, le.length()));
                                if (handle) return handle;
                            }
                        
                        return nullptr;
                    } 
                
  
            }
        #endif

        if(protocolsMap.count(protocol)) {
            Handle* handle = protocolsMap[protocol]->connect(s.substr(s.find(":") + 1, s.length()));
            if(handle) {
                return handle;
            }
        }

        return nullptr;
    }


    /* Broadcast:
          App1(root) ---->  | App2
                            | App3

        Fan-out
            App1(root) --> | App2 o App3

        Fan-in
            App2 ----> | --> App1(root)
            App3 ----> |


        AllGather
            App1 --> |App2 e App3   ==  App2 & App3 --> | App1 (gather) --> | App2 & App3 (Broadcast result)
            App2 --> |App1 e App3
            App3 --> |App1 e App2
    */
    static HandleGroup createTeam(std::string participants, std::string root, CollectiveType type) {


#ifndef ENABLE_CONFIGFILE
        MTCL_ERROR("[Manager]:\t", "Team creation is only available with a configuration file\n");
        return HandleGroup(nullptr);
#else

        // Retrieve team size
        int size = 0;
        std::istringstream is(participants);
        std::string line;
        int rank = 0;
        bool mpi_impl = true, ucc_impl = true;
        while(std::getline(is, line, ':')) {
            if(Manager::appName == line) {
                rank=size;
            }

            bool mpi = false;
            bool ucc = false;
            auto protocols = std::get<1>(components[line]);
            for (auto &prot : protocols) {
                mpi |= prot == "MPI";
                ucc |= prot == "UCX";
            }

            mpi_impl &= mpi;
            ucc_impl &= ucc;

            size++;
        }
        printf("Initializing collective with size: %d - AppName: %s - rank: %d - mpi: %d - ucc: %d\n", size, Manager::appName.c_str(), rank, mpi_impl, ucc_impl);

        std::string teamID{participants + root + std::to_string(type)};

        std::vector<Handle*> coll_handles;

        ImplementationType impl;
        if(mpi_impl) impl = MPI;
        else if(ucc_impl) impl = UCC;
        else impl = GENERIC;

        auto ctx = createContext(type, size, Manager::appName == root, rank);
        if(Manager::appName == root) {
            if(ctx == nullptr) {
                MTCL_ERROR("[Manager]:\t", "Operation type not supported\n");
                return HandleGroup(nullptr);
            }

            // #define SINGLE_IO_THREAD
            #if defined(SINGLE_IO_THREAD)
                if ((groupsReady.count(teamID) != 0) && ctx->update(groupsReady.at(teamID).size())) {
                    coll_handles = groupsReady.at(teamID);
                    groupsReady.erase(teamID);
                }
                else {
                    //NOTE: Active and indefinite wait for group creation
                    do { 
                        for(auto& [prot, conn] : protocolsMap) {
                            conn->update();
                        }
                        if ((groupsReady.count(teamID) != 0) && ctx->update(groupsReady.at(teamID).size())) {
                            coll_handles = groupsReady.at(teamID);
                            groupsReady.erase(teamID);
                            break;
                        }
                    } while(true);
                }
            #else
                // Accetta tante connessioni quanti sono i partecipanti
                // Qui mi serve recuperare esattamente gli handle che hanno fatto
                // connect per partecipazione ad una collettiva specifica
                std::unique_lock lk(group_mutex);
                group_cond.wait(lk, [&]{
                    return (groupsReady.count(teamID) != 0) && ctx->update(groupsReady.at(teamID).size());
                });
                coll_handles = groupsReady.at(teamID);
                groupsReady.erase(teamID);
                size--;
            #endif
        }
        else {
            if(components.count(root) == 0) {
                MTCL_ERROR("[Manager]:\t", "Requested root node is not in configuration file\n");
                return HandleGroup(nullptr);
            }

            // Retrieve root listening addresses and connect to one of them
            auto root_addrs = std::get<2>(components.at(root));

            Handle* handle;
            for(auto& addr : root_addrs) {
                //TODO: bisogna fare il detect del protocollo a cui fare la connect
                //      se i booleani mpi_impl/ucc_impl sono settati, bisogna connettersi
                //      esattamente a quel protocollo
                handle = connectHandle(addr);
                if(handle != nullptr) {
                    MTCL_PRINT(100, "[Manager]:\t", "Connection ok to %s\n", addr.c_str());
                    break; 
                }
                MTCL_PRINT(100, "[Manager]:\t", "Connection failed to %s\n", addr.c_str());
            }

            if(handle == nullptr) {
                MTCL_ERROR("[Manager]:\t", "Could not establish a connection with root node \"%s\"\n", root.c_str());
                return HandleGroup(nullptr);
            }

            int collective = 1;
            handle->send(&collective, sizeof(int));
            handle->send(teamID.c_str(), teamID.length());

            coll_handles.push_back(handle);
        }
        ctx->setImplementation(impl, coll_handles);
        return HandleGroup(ctx);

#endif

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
        Handle* handle = connectHandle(s);

        // if handle is connected, we perform the handshake
        if(handle) {
            int collective = 0;
            handle->send(&collective, sizeof(int));
        }

        return HandleUser(handle, true, true);
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
