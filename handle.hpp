#ifndef HANDLE_HPP
#define HANDLE_HPP

#include <iostream>
#include <atomic>

#include "protocolInterface.hpp"
// class ConnType;

class Handle {
    friend class HandleUser;
    friend class Manager;
    friend class ConnType;

    ConnType* parent;
    std::atomic<bool> busy;

    std::atomic<int> counter = 0;

    void incrementReferenceCounter(){
        counter++;
    }

    void decrementReferenceCounter(){
        counter--;
        if (counter == 0 && closed){
            delete this;
        }
    }
protected:
        std::atomic<bool> closed = false;
private:
    void yield() {
        setBusy(false);
        parent->notify_yield(this);
    }

    void close(){
        closed = true;
        parent->notify_close(this);
        if (counter == 0) 
            delete this;
    }

    void setBusy(bool b) {
        busy = b;
    }

public:
    Handle(ConnType* parent, bool busy=false) : parent(parent), busy(busy) {}
    virtual ssize_t send(const char* buff, size_t size) = 0; // ritorno (INT) 0 ok, -1 errore 
    virtual ssize_t receive(char* buff, size_t size) = 0; //
    
    bool isBusy() {
        return this->busy;
    }

    virtual ~Handle() {};

};


void ConnType::setAsClosed(Handle* h){
    h->close();
}

#endif
