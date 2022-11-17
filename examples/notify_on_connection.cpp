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

//client X --> server: server --> client X(ID_1)
//client Y --> server: server --> client Y(ID_2)

#include <iostream>
#include <string>
#include <optional>
#include <thread>


#include "../manager.hpp"
#include "../protocols/tcp.hpp"


int main(int argc, char** argv){
    if(argc != 3) {
        printf("Usage: %s <id> <address>\n", argv[0]);
        return -1;
    }
    
    int id = atoi(argv[1]);     // logical rank
    char* addr = argv[2];       // listening address
    // Manager m;
    Manager::registerType<ConnTcp>("TCP");
    Manager::init(argc, argv);
    Manager::listen(addr); // TCP:host:port

    // m.registerType<ConnTcp>("TCP");      // TCP
    // m.registerType<ConnMPI>(addr);   // MPI
    // m.init();

    // Listening for new connections
    if(id == 0) {

        while(true) {
            auto handle = Manager::getNext();
            // auto handle = m.getReady();
            if(handle.isValid()) {
                if(handle.isNewConnection()) {
                    handle.yield();
                    printf("Got new connection\n");
                    char buff[4]{'c','i','a','o'};
                    size_t count = 0;
                    size_t size = 4;
                    while(count < size)
                        count += handle.send(buff+count, size-count);
                    
                    // Manager::endM();
                    // return 0;
                }
                else handle.yield();
            }
            else {
                printf("No value in handle\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }

            // bool blocking;
            // HandleUser handle = m.getNext(blocking);
            // if(handle.new())
            //     handle.yield();
            // else....
        }
    }
    // Trying to connect
    else {
        {
            auto handle = Manager::connect("TCP:127.0.0.1:42000");
            if(handle.isValid()) {
                size_t size = 4;
                char buff[4];
                handle.read(buff, size);
                // handle.yield();
                handle.close();
                // auto handle = m.getReady();

                std::string res{buff};
                printf("%s\n", res.c_str());
            }
        }
        Manager::endM();
    }

    return 0;
}