#ifndef HANDLE_HPP
#define HANDLE_HPP

#include <iostream>
#include <atomic>

#include "protocolInterface.hpp"

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

    /**
     * @brief Send \b size byte of \b buff to the remote end connected to this
     * Handle. Wait until all data has been sent or until the peer close the
     * connection.
     * 
     * @param buff data to be sent
     * @param size amount of bytes to send
     * @return number of bytes sent to the remote end or \c -1 if an error occurred.
     * If \c -1 is returned, the error can be checked via \b errno.
     */
    virtual ssize_t send(const void* buff, size_t size) = 0; 


    /**
     * @brief Read at most \b size byte into \b buff from the remote end connected
     * to this Handle. Wait until all \b size data has been received or until the
     * connection is closed by the remote peer.
     * 
     * @param buff 
     * @param size 
     * @return the amount of bytes read upon success, \c 0 in case the connection
     * has been closed, \c -1 if an error occurred. If \c -1 is returned, the error
     * can be checked via \b errno.
     */
    virtual ssize_t receive(void* buff, size_t size) = 0; 
    
    bool isBusy() {
        return this->busy;
    }

    virtual ~Handle() {};

};


void ConnType::setAsClosed(Handle* h){
    h->close();
}

#endif
