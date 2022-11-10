#include <iostream>
#include <vector>
#include <string>
#include <assert.h>
#include <map>
#include <mpi.h>

#define CONNECTION_TAG 0


template<typename T>
class Handle {
    // typedef TT T;
    void send(char* buff, size_t size){
        T::getInstance()->send(this, buff, size);
    }

    void receive(char* buff, size_t size){
        T::getInstance()->receive(this, buff, size);
    }

    void yield(){
        T::getInstance()->yield(this);
    }
};

struct ConnType {
    
    template<typename T>
    static T* getInstance() {
        static T ct;
        return &ct;
    }

    virtual void init() = 0;
    virtual void connect(std::string);
    virtual void removeConnection(std::string);
    virtual void removeConnection(Handle<ConnType>*);
    virtual void update(); // chiama il thread del manager
    virtual Handle<ConnType>* getReady();
    virtual void manage(Handle<ConnType>*);
    virtual void unmanage(Handle<ConnType>*);
    virtual void end();

    virtual void send(Handle<ConnType>*, char*, size_t);
    virtual void receive(Handle<ConnType>*, char*, size_t);
    virtual void yield(Handle<ConnType>*);

};

class ConnTcp : public ConnType {
    void init() {

    }
};


class ConnMPI : public ConnType {
    // Supponiamo tutte le comunicazioni avvengano sul comm_world
    // map è <rank, tag> ---> <free, handle object>
    std::map<std::pair<int,int>, std::pair<bool, Handle<ConnMPI>*>> handles;
    Handle<ConnMPI>* lastReady;

    void init(){
        MPI_Init(NULL, NULL);
    }


    void connect(std::string dest, int tag) {
        // in pratica questo specifica il tag utilizzato per le comunicazioni successive
        // per ora solo tag, poi si vede
        int header[1];
        header[0] = tag;
        int dest_rank = stoi(dest);
        MPI_Send(header, 1, MPI_INT, dest_rank, CONNECTION_TAG, MPI_COMM_WORLD);
    }


    void update() {
        int flag;
        MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, CONNECTION_TAG, MPI_COMM_WORLD, &flag, &status);
        if(flag) {
            // Qui dobbiamo gestire la nuova connessione e aggiungerla a quelle libere
            
            int headersLen;
            MPI_Get_count(&status, MPI_LONG, &headersLen);
            int header[headersLen];
            int rank = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            
            if (MPI_Recv(header, headersLen, MPI_INT, status.MPI_SOURCE, CONNECTION_TAG, MPI_COMM_WORLD, &status) != MPI_SUCCESS) {
                printf("Error on Recv Receiver primo in alto\n");
                //NOTE: assert(false) !!!!
                assert(false);
            }
            
            int source = status.MPI_SOURCE;
            int source_tag = header[0];
            handles[std::make_pair(source, source_tag)] = std::make_pair(true, new Handle<ConnMPI>);
        }

        for (auto &[k, v] : handles) {
            if(v.first) {
                MPI_Iprobe(k.first, k.second, MPI_COMM_WORLD, &flag, &status);
                if(flag) {
                    v.first = false;

                    // Qui passare al manager l'handle da restituire all'utente dopo chiamata a getReady?
                    // Sincronizzato?
                    lastReady = v.second;
                }
            }
        }
        
    }


    Handle<ConnType>* getReady() {
        // Esplode con un return di questo tipo?
        return (Handle<ConnType>*)lastReady;
    }


    void removeConnection(std::string connection) {
        // Assumendo che la stringa per specificare le connessioni sia rank:tag
        std::string delimiter = ":";
        std::string rank = connection.substr(0, connection.find(delimiter));
        std::string tag = connection.substr(connection.find(delimiter) + 1, connection.length() - 1);

        if(checkFree(stoi(rank), stoi(tag))){
            printf("Handle is busy\n");
            return;
        }

        // Questa ha bisogno di sincronizzazione? Magari sì, visto che la removeConnection
        // potrebbe essere chiamata anche da utente nel caso volesse chiudere la connessione a mano
        // (magari questa per MPI ha poco senso)
        handles.erase(std::make_pair(stoi(rank), stoi(tag)));
    }

    
    // Ha ancora senso una remove con Handle? Adesso in teoria non abbiamo più
    // informazioni protocol-specific negli handle
    // virtual void removeConnection(Handle<ConnType>*) {}


    // Anche qui, come gestiamo gli Handle se non hanno più informazioni protocol-specific?
    // Qui vorrei recuperarmi rank-tag di quell'handle, accedere alla map e aggiornare il bool interno
    virtual void manage(Handle<ConnType>*);
    virtual void unmanage(Handle<ConnType>*);


    // Funzione di utilità per vedere se handle è in uso oppure no
    bool checkFree(int rank, int tag) {
        return handles[{rank, tag}].first;
    }
};


class Manager {
    std::map<std::string, ConnType*> protocolsMap;
    std::vector<ConnType*> protocols;
    void* nextReady;

    void* getReady(){
        void* tmp = nextReady;
        nextReady = nullptr;
        // setta closed to false
        return tmp;
    }

    void getReadyBackend(){
        while(true){
            for(auto& c : protocols){
                auto* ready = c.getReady();
                if (ready) nextReady = ready;
            }
            nanosleep(2000);
        }
        // dormi per n ns
        // riprova da capo
    }

    void yield(Handle<ConnType>* n){
        // come faccio a sapere il tipo di connessioni in N??

    }

    // Qualcosa così per creare gli oggetti associati ad un sottotipo?
    template<typename T>
    ConnType* createConnType() {return new T;}

    template<typename T>
    void registerType(std::string s){
        protocolsMap[s] = createConnType<T>();
    }

    static void connect(std::string s){

    };
};



int main(int argc, char** argv){
    Manager m(argc,argv);
    //manager.registerProtocol<ConnTcp>("TCP");

    auto handle = Manager::connect<ConnTCP>("TCP://hostname:port");// TCP : hostname:porta // MPI COMM_WORL:rank:tag

    handle.send()
    handle.yield();





    
    handle = getReady();
    if (handle){

    }


    handle.yield();

    return 0;
}