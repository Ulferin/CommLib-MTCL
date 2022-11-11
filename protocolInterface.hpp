#ifndef PROTOCOLINTERFACE_HPP
#define PROTOCOLINTERFACE_HPP

#include "handle.hpp"
#include "handleUser.hpp"

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


    virtual void yield(Handle*); 

protected:
    HandleUser createHandleUser(Handle* h, bool r){
        return HandleUser(h, r);
    }

};

#endif