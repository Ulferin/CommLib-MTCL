#ifndef HANDLEUSER_HPP
#define HANDLEUSER_HPP

#include "handle.hpp"
#include "errno.h"

class HandleUser {
    friend class ConnType;
    Handle * realHandle;
    bool isReadable = false;
    bool newConnection = true;
public:
    HandleUser() : HandleUser(nullptr, false, false) {}
    HandleUser(Handle* h, bool r, bool n) : realHandle(h), isReadable(r), newConnection(n) {
        if (h) h->incrementReferenceCounter();
    }

    HandleUser(const HandleUser&) = delete;
    HandleUser& operator=(HandleUser const&) = delete;

    HandleUser(HandleUser&& h) : realHandle(h.realHandle), isReadable(h.isReadable), newConnection(h.newConnection){
        h.realHandle = nullptr;
    }
    
    // notifico il manager che l'handle lo gestisce lui
    void yield() {
        isReadable = false;
        newConnection = false;
        if (realHandle) realHandle->yield();
    }

    bool isValid() {
        return realHandle;
    }

    bool isNewConnection() {
        return newConnection;
    }

    ssize_t send(const char* buff, size_t size) {
        newConnection = false;
        if (!realHandle || realHandle->closed) {
            errno = EBADF; // the "communicator" is not valid or closed
            return -1;
        }
        return realHandle->send(buff, size);
    }

    ssize_t read(char* buff, size_t size) {
        newConnection = false;
        if (!isReadable){
            errno = EINVAL; // unable to read from the "communicator"
            return -1;
        }
        if (!realHandle || realHandle->closed) {
            errno = EBADF; // the "communicator" is not valid or closed
            return -1;
        };
        return realHandle->receive(buff, size);
    }

    void close(){
        if (realHandle) realHandle->close();
    }

    ~HandleUser(){
        // if this handle is readable and it is not closed, when i destruct this handle implicitly i'm giving the control to the runtime.
        if (isReadable && realHandle && !realHandle->closed) this->yield();

        // decrement the reference counter of the wrapped handle to manage its destruction.
        if (realHandle) realHandle->decrementReferenceCounter();
    }


};

#endif