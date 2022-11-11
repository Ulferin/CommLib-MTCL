#include <iostream>
#include <vector>
#include <string>
#include <assert.h>
#include <map>
#include <tuple>
#include <mpi.h>

#define CONNECTION_TAG 0


template<typename T>
class Handle {
    // Potrebbe avere un ID univoco incrementale che definiamo noi in modo da
    // accedere velocemente a quale connessione fa riferimento nel ConnType
    // Questo potrebbe sostituire l'oggetto "this" a tutti gli effetti quando
    // vogliamo fare send/receive/yield --> il ConnType di appartenenza ha info
    // interne per capire di chi si tratta
    
public:
    // TODO: Per ora public, poi dentro un getter per recuperarlo
    size_t ID;

public:
    Handle(size_t ID) : ID(ID) {}

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
    // nuova map è <hash_val> --> <free, <rank,tag>, handle object>
    // std::map<std::pair<int,int>, std::pair<bool, Handle<ConnMPI>*>> handles;
    std::map<size_t, std::tuple<bool, std::pair<int,int>, Handle<ConnMPI>*>> handles;
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
            // Genera ID univoco per la stringa "rank:tag"
            size_t hash_val = std::hash<std::string>{}(std::to_string(source) + ":" + std::to_string(source_tag));
            handles[hash_val] = std::make_tuple(true, std::make_pair(source, source_tag), new Handle<ConnMPI>(hash_val));
        }

        for (auto &[k, v] : handles) {
            if(std::get<0>(v)) {
                MPI_Iprobe(std::get<1>(v).first, std::get<1>(v).second, MPI_COMM_WORLD, &flag, &status);
                if(flag) {
                     std::get<0>(v) = false;

                    // Qui passare al manager l'handle da restituire all'utente dopo chiamata a getReady?
                    // Sincronizzato?
                    lastReady = std::get<2>(v);
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
        size_t key = std::hash<std::string>{}(connection);
        if(checkFree(key)){
            printf("Handle is busy\n");
            return;
        }

        // Questa ha bisogno di sincronizzazione? Magari sì, visto che la removeConnection
        // potrebbe essere chiamata anche da utente nel caso volesse chiudere la connessione a mano
        // (magari questa per MPI ha poco senso)
        handles.erase(key);
    }

    
    void removeConnection(Handle<ConnType>* handle) {
        size_t key = handle->ID;
        if(checkFree(key)){
            printf("Handle is busy\n");
            return;
        }

        handles.erase(key);
    }


    // Anche qui, come gestiamo gli Handle se non hanno più informazioni protocol-specific?
    // Qui vorrei recuperarmi rank-tag di quell'handle, accedere alla map e aggiornare il bool interno
    virtual void manage(Handle<ConnType>*);
    virtual void unmanage(Handle<ConnType>*);


    void send(Handle<ConnType>* handle, char* buf, size_t size) {
        size_t key = handle->ID;

        int rank = std::get<1>(handles[key]).first;
        int tag = std::get<1>(handles[key]).second;

        MPI_Send(buf, size, MPI_BYTE, rank, tag, MPI_COMM_WORLD);
    }

    void receive(Handle<ConnType>* handle, char* buf, size_t size) {
        size_t key = handle->ID;

        int rank = std::get<1>(handles[key]).first;
        int tag = std::get<1>(handles[key]).second;

        MPI_Recv(buf, size, MPI_BYTE, rank, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    void yield(Handle<ConnType>* handle) {
        size_t key = handle->ID;

        // Per il momento impostiamo a true, ma bisogna vedere come rimuovere il
        // controllo dall'utente che ha fatto yield. Magari con un puntatore offuscato
        // come abbiamo discusso in precedenza
        std::get<0>(handles[key]) = true;
    }


    // Funzione di utilità per vedere se handle è in uso oppure no
    bool checkFree(size_t key) {
        return std::get<0>(handles[key]);
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
    ConnType* createConnType() {
        static_assert(std::is_base_of<ConnType,T>::value, "Not a ConnType subclass");
        return new T;
    }

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