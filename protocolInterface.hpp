#ifndef PROTOCOLINTERFACE_HPP
#define PROTOCOLINTERFACE_HPP

// #include "manager.hpp"
// #include "handle.hpp"
// #include "handleUser.hpp"
#include <queue>

class Handle;

class ConnType {

public:
    ConnType() {}
    // template<typename T>
    // static T* getInstance() {
    //     static T ct;
    //     return &ct;
    // }


    virtual int init() = 0;
    virtual int listen(std::string) = 0;
    virtual Handle* connect(const std::string&) = 0; 
    // virtual void removeConnection(std::string);
    virtual void removeConnection(Handle*) = 0;
    // virtual void update(std::queue<Handle*>&, std::queue<Handle*>&) = 0; // chiama il thread del manager
    virtual void update(std::queue<std::pair<bool,Handle*>>&) = 0; // chiama il thread del manager
    virtual void notify_yield(Handle*) = 0;
    virtual void notify_request(Handle*) = 0;
    virtual void end() = 0;



};

#endif