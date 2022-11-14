#ifndef HANDLE_HPP
#define HANDLE_HPP

#include <iostream>
#include <atomic>

class ConnType;

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

private:
    void yield() {
        setBusy(false);
        parent->notify_yield(this);
    }

    bool request() {
        if(isBusy())
            return false;

        parent->notify_request(this);
        setBusy(true);
        return true;
    }

    void setBusy(bool b) {
        busy = b;
    }

public:
    Handle(ConnType* parent, bool busy=false) : parent(parent), busy(busy) {}
    virtual int send(char* buff, size_t size);
    virtual int receive(char* buff, size_t size);
    
    bool isBusy() {
        return this->busy;
    }

};

#endif
