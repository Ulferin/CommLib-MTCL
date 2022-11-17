#ifndef PROTOCOLINTERFACE_HPP
#define PROTOCOLINTERFACE_HPP
//#include "manager.hpp"
// #include "handle.hpp"
// #include "handleUser.hpp"
#include <queue>
#include <mutex>
#include <functional>

class Handle;
class ConnType {

    friend class Manager;
    friend class Handle;


private:
    std::mutex m;


protected:
    /*void addinQ(std::pair<bool, Handle*> el) {
        Manager::addinQ(el);
    }*/

    std::function<void(std::pair<bool, Handle*>)> addinQ;

public:
    ConnType() {};
    virtual ~ConnType() {};
    
    virtual int init() = 0;
    virtual int listen(std::string) = 0;
    virtual Handle* connect(const std::string&) = 0; 
    virtual void update() = 0; // chiama il thread del manager
    virtual void notify_yield(Handle*) = 0;
    virtual void notify_close(Handle*) = 0;
    virtual void end() = 0;



};

#endif