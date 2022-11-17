#ifndef HANDLEUSER_HPP
#define HANDLEUSER_HPP

#include "handle.hpp"

class HandleUser {
    friend class ConnType;
    Handle * realHandle;
    bool isReadable = false;
    bool newConnection = true;
public:
    HandleUser() : HandleUser(nullptr, false, false) {}
    HandleUser(Handle* h, bool r, bool n) : realHandle(h), isReadable(r), newConnection(n) {
        h->counter++;
    }

    HandleUser(const HandleUser&) = delete;
    HandleUser& operator=(HandleUser const&) = delete;
    
    // notifico il manager che l'handle lo gestisce lui
    void yield() {
        isReadable = false;
        newConnection = false;
        realHandle->yield();        
        // realHandle = nullptr;
    }

    // bool acquireRead() {
    //     // check se sono l'unico a ricevere
    //     // se si setta readable a true e torna true, altrimenti torna false
    //     isReadable = realHandle->request();

    //     return isReadable;
    // }

    bool isValid() {
        return realHandle;
    }

    bool isNewConnection() {
        return newConnection;
    }

    size_t send(char* buff, size_t size){
        newConnection = false;
        return realHandle->send(buff, size);
    }

    size_t read(char* buff, size_t size){
        newConnection = false;
        if (!isReadable) throw;
        return realHandle->receive(buff, size);
    }

    void close(){
        realHandle->close();
    }

    ~HandleUser(){
        // if this handle is readable and it is not closed, when i destruct this handle implicitly i'm giving the control to the runtime.
        if (isReadable && !realHandle->closed) this->yield();

        // decrement the reference counter of the wrapped handle to manage its destruction.
        realHandle->decrementReferenceCounter();
    }

};

#endif