/**
 * @file notify_on_connection.cpp
 * @author Federico Finocchio
 *
 * @brief 
 * 
 * -- Descrizione --
 * Come gestisco le situazioni in cui voglio che il mio processo "server" mandi
 * una notifica a tutti i client che si connettono? In pratica vorrei costruire
 * un server che, usando la libreria, riceva connessioni su una porta prestabilita
 * e, per ogni nuova connessione, invii un ID al client appena connesso in modo
 * da informarlo del suo identificatore per tutti i prossimi messaggi.
 * 
 * Attualmente questo non possiamo farlo, visto che nella getReady() noi prendiamo
 * solo gli Handle che sono pronti per leggere qualche dato. Dovremmo necessariamente
 * aggiungere nella getReady() anche tutti quegli Handle che sono stati accettati
 * dal Manager, anche se non hanno niente da leggere al loro interno.
 * 
 * E.g. (TCP): tutti quegli fd che vengono restituiti dalla accept quando la select
 * restituisce il fd associato al socket del server.
 * 
 * Con questa logica la getReady() non dà più la garanzia che l'Handle restituito
 * sia "readable", ma semplicemente che c'è stato un qualche evento su quell'Handle.
 * 
 * @date 2022-11-14
 * 
 */

//client X --> server: server --> client X(1)
//client Y --> server: server --> client Y(2)

#include <iostream>
#include <vector>
#include <string>
#include <assert.h>
#include <map>
#include <tuple>
#include <mpi.h>
#include <optional>


#include "manager.hpp"


int main(int argc, char** argv){
    // Manager m(argc,argv);
    CommLib::init(argc,argv);
    m.registerProtocol<ConnTcp>("TCP://host:port");
    
    std::map<std::string, size_t> peers;

    while(true){
        auto handle = m.getReady();            // voglio farci business logic
        if(!handle.has_value()) {
            printf("no handle");
            return;
        }

        while(true) {
            auto handle = m.getNewConnection();       // voglio farci l'handshake
        }

        handle.receive(buff, sz);
        peers[std::string(buff)] = handle.getID();
        handle.yield();

        // faccio altro, magari anche scrivendo dati su HandleUser
        // ....
        // ....

        // Voglio recuperare peer di prima per interazioni domanda-risposta
        // quindi ho bisogno di recuperare di nuovo 
        auto handle = Manager.getHandle(peers["host:port"]);

    }

    return 0;
}