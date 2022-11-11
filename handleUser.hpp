#ifndef HANDLEUSER_HPP
#define HANDLEUSER_HPP

#include "handle.hpp"

class HandleUser {
    friend class ConnType;
    Handle * realHandle;
    bool isReadable = false;
    HandleUser(Handle* h, bool r) : realHandle(h), isReadable(r) {}
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
        realHandle->send(buff, size);
    }

    void read(char* buff, size_t size){
        if (!isReadable) throw;
        realHandle->receive(buff, size);
    }

};

#endif