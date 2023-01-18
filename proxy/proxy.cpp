#include <iostream>
#include <fstream>
#include <string>
#include <optional>
#include <thread>

#include "rapidjson/rapidjson.h"
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/document.h>
#include "bimap.hpp"

#include "../mtcl.hpp"

#define PROXY_PORT 8002 // solo tra proxy
#define MAX_DEST_STRING 60
#define CHUNK_SIZE 1200

enum cmd_t : char {FWD = 0, CONN = 1, PRX = 2};

/**
 * PROXY <--> PROXY PROTOCOL
 *  | char CMD |  size_t IDENTIFIER |  ....... PAYLOAD ........ |
*/
char headerBuffer[sizeof(char) + sizeof(size_t)];
char chunkBuffer[CHUNK_SIZE];

using connID_t = size_t;
using handleID_t = size_t;

// config file
// pools: Name => [List(proxyIP), List(nodes)]
std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> pools;
// components: Name => [hostname, List(protocols), List(listen_endpoints)]
std::map<std::string, std::tuple<std::string, std::vector<std::string>, std::vector<std::string>>> components;


std::map<handleID_t, HandleUser> id2handle; // dato un handleID ritorna l'handle

std::map<connID_t, HandleUser*> connid2proxy; // dato una connID (meaningful tra proxy) ritorna l'handle del proxy su cui scrivere
bimap<handleID_t, connID_t> loc2connID;  // associazione bidirezionale handleID <-> conneID

std::map<handleID_t, handleID_t> proc2proc; // associazioni handleID <-> handleID per connessioni tra processi mediante singolo proxy


// invia un messaggio ad un proxy formattato secondo il protocollo definito
void sendHeaderProxy(HandleUser& h, cmd_t cmd, size_t identifier){
    headerBuffer[0] = cmd;
    memcpy(headerBuffer+sizeof(char), &identifier, sizeof(size_t)); 
    h.send(headerBuffer, sizeof(headerBuffer));
}

// ritorna in base alla stringa se una connessione è locale o remota (ovvero mediante altro proxy)
bool isLocal(const std::string& destination){
    return true;
}

// ritorna in base alla destinazione l'handle del proxy che gestisce (può raggiungere) quella destinazione
Handle&& getProxyFromDestination(const std::string& destination){

}

HandleUser* toHeap(HandleUser h){
    return new HandleUser(std::move(h));
}

    
std::vector<std::string> JSONArray2VectorString_(rapidjson::GenericArray& arr){
    std::vector<std::string> output;
    for(auto& e : arr)  
        output.push_back(e.GetString());
    
    return output;
}

void parseConfig(std::string& f){
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
/*
    Connect verso proxy ---> pool e ip
    Accept da proxy ---> ricevo pool -> 




*/

int main(int argc, char** argv){

    Manager::registerType<ConnTcp>("TCP");
    Manager::registerType<ConnTcp>("P");
    Manager::init("PROXY");

    // parse file config che prendo da argv[2]

    std::string pool(argv[1]);

    // check esistenza pool
    if (!pools.count(pool)){
        std::cerr << "Errore, pool non trovato!\n";
        abort();
    }

    // connect to other proxies
    std::map<std::string, HandleUser*> proxies;
    for(auto& [name, val] : pools)
        if (name > pool) {
            for(auto& addr : val.first){
                ///if (add == mioaddr) continue; ## TODO!!
                auto h = Manager::connect("P:" + addr + ":" + std::to_string(PROXY_PORT));
                if (h.isValid()) {

                    // send cmd: PRX - ID: 0 - Payload: {pool name}
                    char* buff = new char[sizeof(cmd_t) + sizeof(size_t) + pool.length()];
                    buff[0] = cmd_t::PRX;
                    memset(buff+1, 0, sizeof(size_t));
                    memcpy(buff+sizeof(char)+sizeof(size_t), pool.c_str(), pool.length());
                    h.send(buff, sizeof(cmd_t) + sizeof(size_t) + pool.length());

                    h.yield();
                    proxies[name] = toHeap(std::move(h));
                }
            }
        }

    while(true){
        auto h = Manager::getNext();
        
        if (Manager::getTypeOfHandle(h) == "P"){
            if (h.isNewConnection()){
                // nuovo proxy
                size_t sz;
                h.probe(sz, true);
                char* buff = new char[sz+1];
                h.receive(buff, sz);
                buff[sz] = '\0';
                std::string poolName;
                if (buff[0] == cmd_t::PRX)
                    poolName = std::string(buff+sizeof(size_t)+sizeof(char));
                
                proxies[poolName] = toHeap(std::move(h));
                delete [] buff;
                continue;
            }
            size_t sz;
            h.probe(sz);
            char* buff = new char[sz];
            h.receive(buff, sz);


            cmd_t cmd = (cmd_t)buff[0];
            connID_t identifier = *reinterpret_cast<connID_t*>(buff+sizeof(char));
            
            char* payload = buff + sizeof(char) + sizeof(size_t);
            size_t size = sz - sizeof(char) - sizeof(size_t);
           
            if (loc2connID.has_value(identifier))
                id2handle[loc2connID.get_key(identifier)].send(payload, size);
            else
                std::cerr << "Received a forward message from a proxy but the identifier is unknown!\n";
            
            delete [] buff;
        
            continue;
        } else {
            if (h.isNewConnection()){
                // read destination
                char destination[MAX_DEST_STRING];
                size_t read_ = h.receive(destination, MAX_DEST_STRING);
                std::string destination_str(destination, read_);
                
                // se la destinazione è locale fai connect
                if (isLocal(destination)){
                    auto peer = Manager::connect(destination_str);
                    size_t peer_id = peer.getID();
                    chId2Proc.emplace(h.getID(), std::move(peer));
                    chId2Proc.emplace(peer_id, std::move(h));
                    continue;
                } else {
                    getProxyFromDestination(destination)
                    // trovo qule proxy è la destinazione e setto qualcosa

                    //TODO
                }

            }

            size_t sz = h.receive(chunkBuffer, CHUNK_SIZE);

            const auto& dest = chId2Proxy.find(h.getID());
            if (dest != chId2Proxy.end()){
                dest->second.send(/**encaspsulated data*/);
                continue;
            }
            const auto& dest = chId2Proc.find(h.getID());
            if (dest != chId2Proc.end()){
                dest->second.send(chunkBuffer, sz);
                continue;
            }

            std::cerr << "Received something from a old connection that i cannot handle! :(\n";
        }
    }

    Manager::finalize();
    return 0;
}