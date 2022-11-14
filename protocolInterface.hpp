#ifndef PROTOCOLINTERFACE_HPP
#define PROTOCOLINTERFACE_HPP

#include "manager.hpp"
#include "handle.hpp"
#include "handleUser.hpp"

class ConnType {

public:
    // template<typename T>
    // static T* getInstance() {
    //     static T ct;
    //     return &ct;
    // }


    virtual int init() = 0;
    virtual Handle* connect(std::string); 
    // virtual void removeConnection(std::string);
    virtual void removeConnection(Handle*);
    virtual void update(std::queue<Handle*>&, std::queue<Handle*>&); // chiama il thread del manager
    virtual void notify_yield(Handle*);
    virtual void notify_request(Handle*);
    virtual void end();



};

#endif