#ifndef HANDLE_HPP
#define HANDLE_HPP

#include <iostream>
#include <atomic>

#include "protocolInterface.hpp"
// class ConnType;

class Handle {
    // Potrebbe avere un ID univoco incrementale che definiamo noi in modo da
    // accedere velocemente a quale connessione fa riferimento nel ConnType
    // Questo potrebbe sostituire l'oggetto "this" a tutti gli effetti quando
    // vogliamo fare send/receive/yield --> il ConnType di appartenenza ha info
    // interne per capire di chi si tratta
    friend class HandleUser;
    friend class Manager;
    ConnType* parent;
    std::atomic<bool> busy;

    std::atomic<int> counter = 0;
    bool closed;

    void incrementReferenceCounter(){
        counter++;
    }

    void decrementReferenceCounter(){
        counter--;
        if (counter == 0 && closed){
            delete this;
        }
    }

private:
    void yield() {
        setBusy(false);
        std::lock_guard lk(parent->m);
        parent->notify_yield(this);
    }

    void close(){
        closed = true;
        std::lock_guard lk(parent->m);
        parent->notify_close(this);
    }




    void setBusy(bool b) {
        busy = b;
    }

public:
    Handle(ConnType* parent, bool busy=false) : parent(parent), busy(busy) {}
    virtual size_t send(char* buff, size_t size) = 0;
    virtual size_t receive(char* buff, size_t size) = 0;
    
    bool isBusy() {
        return this->busy;
    }

    virtual ~Handle() = 0;

};

#endif
