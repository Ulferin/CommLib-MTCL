#ifndef HANDLE_HPP
#define HANDLE_HPP

#include <iostream>
#include <atomic>

#include "protocolInterface.hpp"
// class ConnType;

class Handle {
    friend class HandleUser;
    friend class Manager;
    ConnType* parent;
    std::atomic<bool> busy;

    std::atomic<int> counter = 0;
    bool closed = false;

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
        parent->notify_yield(this);
    }

    void close(){
        closed = true;
        parent->notify_close(this);
    }




    void setBusy(bool b) {
        busy = b;
    }

public:
    Handle(ConnType* parent, bool busy=false) : parent(parent), busy(busy) {}
    virtual size_t send(const char* buff, size_t size) = 0;
    virtual size_t receive(char* buff, size_t size) = 0;
    
    bool isBusy() {
        return this->busy;
    }

    virtual ~Handle() {};

};

#endif
