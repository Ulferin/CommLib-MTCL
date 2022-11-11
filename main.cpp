#include <iostream>
#include <vector>
#include <string>
#include <assert.h>
#include <map>
#include <tuple>
#include <mpi.h>
#include <optional>

#define CONNECTION_TAG 0
class ConnType;
class HandleUser;

class Handle {
    // Potrebbe avere un ID univoco incrementale che definiamo noi in modo da
    // accedere velocemente a quale connessione fa riferimento nel ConnType
    // Questo potrebbe sostituire l'oggetto "this" a tutti gli effetti quando
    // vogliamo fare send/receive/yield --> il ConnType di appartenenza ha info
    // interne per capire di chi si tratta
    friend class HandleUser;
    ConnType* parent;

public:
    Handle(ConnType* parent) : parent(parent) {}
    virtual void send(char* buff, size_t size);
    virtual void receive(char* buff, size_t size);
};


class HandleUser {
    friend class ConnType;
    Handle * realHandle;
    bool isWritable = false;
    bool isReadable = false;
    HandleUser(Handle* h, bool w, bool r) : realHandle(h), isWritable(w), isReadable(r) {}
public:
    HandleUser(const HandleUser&) = delete;
    HandleUser& operator=(HandleUser const&) = delete;
    
    void yield(){
        // notifico il manager che l'handle lo gestisce lui
        isReadable = false;
        
    }

    bool acquireRead(){
        // check se sono l'unico a ricevere
        // se si setta readable a true e torna true, altrimenti torna false
    }

    void send(char* buff, size_t size){
        if (!isWritable) throw ;
        realHandle->send(buff, size);
    }

    void read(char* buff, size_t size){
        if (!isReadable) throw;
        realHandle->receive(buff, size);
    }

};

class HandleMPI : public Handle {
    int rank;
    int tag;

    HandleMPI(ConnType* parent, int rank, int tag): Handle(parent), rank(rank), tag(tag){}

    void send(char* buff, size_t size){
        MPI_Send(buff, size, MPI_BYTE, rank, tag, MPI_COMM_WORLD);
    }

    void receive(char* buff, size_t size){
        MPI_Recv(buff, size, MPI_BYTE, rank, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
};




struct ConnType {
    template<typename T>
    static T* getInstance() {
        static T ct;
        return &ct;
    }


    virtual void init() = 0;
    virtual HandleUser connect(std::string); 
    virtual void removeConnection(std::string);
    virtual void removeConnection(HandleUser*);
    virtual void update(); // chiama il thread del manager
    virtual HandleUser getReady(); // ritorna readable
    virtual void end();


    //virtual void yield(Handle*); 

protected:
    HandleUser createHandleUser(Handle* h, bool w, bool r){
        return HandleUser(h, w, r);
    }

};

class ConnTcp : public ConnType {
    void init() {

    }
};




class ConnMPI : public ConnType {
public:
    // Supponiamo tutte le comunicazioni avvengano sul comm_world
    // map è <rank, tag> ---> <free, handle object>
    // nuova map è <hash_val> --> <free, <rank,tag>, handle object>
    // std::map<std::pair<int,int>, std::pair<bool, Handle<ConnMPI>*>> handles;

    Handle* lastReady;
    std::map<HandleMPI*, bool> connections;

    void init(){
        MPI_Init(NULL, NULL);
    }


    HandleUser connect(std::string dest) {
        // in pratica questo specifica il tag utilizzato per le comunicazioni successive
        // per ora solo tag, poi si vede

        //parse della stringa

        int header[1];
        header[0] = tag;
        int dest_rank = stoi(dest);
        MPI_Send(header, 1, MPI_INT, dest_rank, CONNECTION_TAG, MPI_COMM_WORLD);

        // creo l'handle
        auto* handle = new HandleMPI(rank. tag);
        connections[handle] = false;    

        return createHandleUser(handle);
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

    
    void removeConnection(HandleMPI* handle) {
        if(connections[handle]){
            printf("Handle is busy\n");
            return;
        }
        connections.erase(handle);
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
    void* nextReady; // coda thread safe 1 produttore (thread che fa polling) n consumatori che sono quelli che chiamano getReady

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
    manager.registerProtocol<ConnTcp>("TCP");

     Manager::connect("TCP://hostname:port").yield();
     Manager::connect("TCP://hostname:port").yield();
     Manager::connect("TCP://hostname:port").yield();
     Manager::connect("TCP://hostname:port", "master").yield();
    Manger::connect("MPI://0:10");
    

    while(true){
        auto handle = Manger.getReady();

        // send/receive
        auto* handle2 = &handle;


        handle.yield();

        handle2->receive(); // errore handle2 è invalidato
    }

    

    Manager.getHandleFromLabel("master");

    auto handle = Manager::connect("TCP://hostname:port");// TCP : hostname:porta // MPI COMM_WORL:rank:tag

    handle->send();

    handle.yield();

    
    handle = manger.getReady();
    handle->receive(buff, size);
    handle.yield();




    return 0;
}