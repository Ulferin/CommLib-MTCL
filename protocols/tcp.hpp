#ifndef TCP_HPP
#define TCP_HPP

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <vector>
#include <queue>
#include <map>
#include <shared_mutex>

#include "../handle.hpp"
#include "../protocolInterface.hpp"

#define IOVMAXCOUNT 2
#define MAXRETRY 32
#define SELECTTIMEOUT 100000    // usec

class HandleTCP : public Handle {

public:
    int fd; // File descriptor of the connection represented by this Handle
    HandleTCP(ConnType* parent, int fd, bool busy=true) : Handle(parent, busy), fd(fd) {}

    size_t send(const char* buff, size_t size) {
        return write(fd, buff, size);
    }


    size_t receive(char* buff, size_t size) {
        return read(fd, buff, size);
    }


    ~HandleTCP() {}

};


class ConnTcp : public ConnType {
private:
    // enum class ConnEvent {close, yield};

protected:
    std::string address;
    int port;
    
    std::map<int, Handle*> connections;  // Active connections for this Connector

    fd_set set, tmpset;
    int listen_sck;
    std::atomic<int> fdmax;

    std::shared_mutex shm;


private:
    /**
     * @brief Initializes the main listening socket for this Handle
     * 
     * @return int status code
     */
    int _init() {
        if ((listen_sck=socket(AF_INET, SOCK_STREAM, 0)) < 0){
            printf("Error creating the socket\n");
            return -1;
        }

        int enable = 1;
        // enable the reuse of the address
        if (setsockopt(listen_sck, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
            printf("setsockopt(SO_REUSEADDR) failed\n");
            return -1;
        }

        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET; 
        serv_addr.sin_addr.s_addr = INADDR_ANY; // listening on any interface
        serv_addr.sin_port = htons(port);

        int bind_err;
        if ((bind_err = bind(listen_sck, (struct sockaddr*)&serv_addr,sizeof(serv_addr))) < 0){
            printf("Error binding: %d -- %s\n", bind_err, strerror(errno));
            return -1;
        }

        if (::listen(listen_sck, MAXRETRY) < 0){
            printf("Error listening\n");
            return -1;
        }

        return 0;
    }


public:

   ConnTcp(){};
   ~ConnTcp(){};

    int init() {
        return 0;
    }

    int listen(std::string s) {
        address = s.substr(s.find(":")+1, s.length());
        port = stoi(address.substr(address.find(":")+1, address.length()));

        if(this->_init())
            return -1;

        printf("Listening on: %s\n", address.c_str());

        // intialize both sets (master, temp)
        FD_ZERO(&set);
        FD_ZERO(&tmpset);

        // add the listen socket to the master set
        FD_SET(this->listen_sck, &set);

        // hold the greater descriptor
        fdmax = this->listen_sck;

        return 0;
    }

    void update() {
        // copy the master set to the temporary

        std::unique_lock ulock(shm, std::defer_lock);
        std::shared_lock shlock(shm, std::defer_lock);

        ulock.lock();
        tmpset = set;
        ulock.unlock();

        struct timeval wait_time = {.tv_sec=0, .tv_usec=SELECTTIMEOUT};

        switch(select(fdmax+1, &tmpset, NULL, NULL, &wait_time)){
            case -1: printf("Error on selecting socket\n");
            case  0: {return;}
        }

        for(int idx=0; idx <= fdmax; idx++){
            if (FD_ISSET(idx, &tmpset)){
                if (idx == this->listen_sck) {
                    int connfd = accept(this->listen_sck, (struct sockaddr*)NULL ,NULL);
                    if (connfd == -1){
                        printf("Error accepting client\n");
                        return;
                    }

                    ulock.lock();
                    FD_SET(connfd, &set);
                    if(connfd > fdmax) fdmax = connfd;
                    connections[connfd] = new HandleTCP(this, connfd, false);
                    addinQ({true, connections[connfd]});
                    ulock.unlock();
                    
                }
                else {
                    ulock.lock();
                    // Updates ready connections and removes from listening
                    FD_CLR(idx, &set);

                    // update the maximum file descriptor
                    if (idx == fdmax)
                        for(int ii=(fdmax-1);ii>=0;--ii) {
                            if (FD_ISSET(ii, &set)){
                                fdmax = ii;
                                break;
                            }
                        }
                    // ready.push(connections[idx]);
                    addinQ({false, connections[idx]});
                    ulock.unlock();

                }
                
            }
        }

        return;        
    }

    // URL: host:prot || label: stringa utente
    Handle* connect(const std::string& address/*, const std::string& label=std::string()*/) {
        printf("[TCP]Connecting to: %s\n", address.c_str());

        int fd;

        struct addrinfo hints;
        struct addrinfo *result, *rp;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;                /* Allow IPv4 or IPv6 */
        hints.ai_socktype = SOCK_STREAM;            /* Stream socket */
        hints.ai_flags = 0;
        hints.ai_protocol = IPPROTO_TCP;            /* Allow only TCP */

        // resolve the address (assumo stringa formattata come host:port)
        if (getaddrinfo(
                    (address.substr(0, address.find(":"))).c_str(),
                    (address.substr(address.find(":")+1, address.length())).c_str(),
                    &hints, &result)
                != 0)
            return nullptr;

        // try to connect to a possible one of the resolution results
        for (rp = result; rp != NULL; rp = rp->ai_next) {
            fd = socket(rp->ai_family, rp->ai_socktype,
                            rp->ai_protocol);
            if (fd == -1)
                continue;

            if (::connect(fd, rp->ai_addr, rp->ai_addrlen) != -1)
                break;                  /* Success */

            close(fd);
        }
        free(result);
            
        if (rp == NULL)            /* No address succeeded */
            return nullptr;

        HandleTCP *handle = new HandleTCP(this, fd);
        std::unique_lock lock(shm);
        connections[fd] = handle;
        lock.unlock();
        return handle;
    }

    void notify_close(Handle* h) {
        int fd = reinterpret_cast<HandleTCP*>(h)->fd;
        close(fd);
        std::unique_lock lock(shm);
        connections.erase(fd); // elimina un puntatore! è safe!
        FD_CLR(fd, &set);

        // update the maximum file descriptor
        if (fd == fdmax)
            for(int ii=(fdmax-1);ii>=0;--ii) {
                if (FD_ISSET(ii, &set)){
                    fdmax = ii;
                    break;
                }
            }
    }


    void notify_yield(Handle* h) override {
        int fd = reinterpret_cast<HandleTCP*>(h)->fd;
        std::unique_lock l(shm);
        FD_SET(fd, &set);
        if(fd > fdmax) {
            fdmax = fd;
        }
    }

    void end() {
        return;
    }

};

#endif