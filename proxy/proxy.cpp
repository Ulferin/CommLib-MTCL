#include <iostream>
#include <string>
#include <optional>
#include <thread>

#include "bimap.hpp"

#include "../manager.hpp"
#include "../protocols/tcp.hpp"

#define MAX_DEST_STRING 60
#define CHUNK_SIZE 1200

enum cmd_t : char {FWD = 0, CONN = 1, PRX = 2};

/**
 * PROXY <--> PROXY PROTOCOL
 *  | char CMD |  size_t IDENTIFIER |  size_t SIZE   |  ....... PAYLOAD ........ |
*/
char headerBuffer[sizeof(char) + sizeof(size_t) + sizeof(size_t)];
char chunkBuffer[CHUNK_SIZE];

using connID_t = size_t;
using handleID_t = size_t;

std::map<handleID_t, HandleUser> id2handle; // dato un handleID ritorna l'handle
std::map<connID_t, HandleUser*> connid2proxy; // dato una connID (meaningful tra proxy) ritorna l'handle del proxy su cui scrivere
bimap<handleID_t, connID_t> loc2connID;  // associazione bidirezionale handleID <-> conneID
std::map<handleID_t, handleID_t> proc2proc; // associazioni handleID <-> handleID per connessioni tra processi mediante singolo proxy


// invia un messaggio ad un proxy formattato secondo il protocollo definito
void sendWithHeader(HandleUser& h, cmd_t cmd, size_t identifier, const char* payload, size_t size){
    headerBuffer[0] = cmd;
    memcpy(headerBuffer+sizeof(char), &identifier, sizeof(size_t));
    memcpy(headerBuffer+sizeof(char)+sizeof(size_t), &size, sizeof(size_t));
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

int main(int argc, char** argv){
    Manager::registerType<ConnTcp>("TCP");
    Manager::registerType<ConnTcp>("P");
    Manager::init(argc, argv);

    

    /*
    Connect hai proxies dopo del mio nella lista!
    */

    while(true){
        auto h = Manager::getNext();
        
        if (Manager::getTypeOfHandle(h) == "P"){
            if (h.isNewConnection()){
                proxies.emplace(h.getID(), std::move(h));
                continue;
            }
            h.read(headerBuffer, sizeof(headerBuffer));
            cmd_t cmd = (cmd_t)headerBuffer[0];
            size_t identifier = *reinterpret_cast<size_t*>(headerBuffer+sizeof(char));
            size_t size = *reinterpret_cast<size_t*>(headerBuffer+sizeof(char)+sizeof(size_t));
            char* payload;
            if (size){
                payload = new char[size];
                h.read(payload, size);
            }

            if (chId2Proc.find(identifier) != chId2Proc.end())
                chId2Proc[identifier].send(payload, size);
            else
                std::cerr << "Received a forward message from a proxy but the identifier is unknown!\n";
        
            continue;
        } else {
            if (h.isNewConnection()){
                // read destination
                char destination[MAX_DEST_STRING];
                size_t read_ = h.read(destination, MAX_DEST_STRING);
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
                }

            }

            size_t sz = h.read(chunkBuffer, CHUNK_SIZE);

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

    Manager::endM();
    return 0;
}