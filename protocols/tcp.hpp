#ifndef TCP_HPP
#define TCP_HPP

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <vector>
#include <queue>
#include <map>

#include "../protocolInterface.hpp"

#define IOVMAXCOUNT 2
#define MAXRETRY 32
#define SELECTTIMEOUT 100000    // usec

class HandleTCP : public Handle {

// protected:

public:
    int fd; // File descriptor of the connection represented by this Handle
    HandleTCP(ConnType* parent, int fd) : Handle(parent), fd(fd) {}

    // NOTE: qui vogliamo restituire il numero di byte inviati oppure vogliamo garantire che tutti i byte siano stati mandati?
    int send(char* buff, size_t size) {
        size_t* sz = new size_t(htobe64(size));
        
        struct iovec iov[IOVMAXCOUNT];
        iov[0].iov_base = sz;
        iov[0].iov_len = sizeof(size_t);
        iov[1].iov_base = buff;
        iov[1].iov_len = size;

        ssize_t written;
        int count = IOVMAXCOUNT;
        for (int cur = 0;;) {
            written = writev(fd, iov+cur, count-cur);
            if (written < 0) return -1;
            while (cur < count && written >= (ssize_t)iov[cur].iov_len)
                written -= iov[cur++].iov_len;
            if (cur == count) {
                delete sz;
                return 1; // success!!
            }
            iov[cur].iov_base = (char *)iov[cur].iov_base + written;
            iov[cur].iov_len -= written;
        }

        return -1;
    }


    int receive(char* buff, size_t size) {
        size_t sz;
        struct iovec iov[1];
        iov[0].iov_base = &sz;
        iov[0].iov_len = sizeof(sz);

        ssize_t rread;
        while(rread)
        for (int cur = 0;;) {
            rread = readv(fd, iov+cur, 1-cur);
            if (rread <= 0) return rread; // error or closed connection
            while (cur < 1 && rread >= (ssize_t)iov[cur].iov_len)
                rread -= iov[cur++].iov_len;
            if (cur == 1) return 1; // success!!
            iov[cur].iov_base = (char *)iov[cur].iov_base + rread;
            iov[cur].iov_len -= rread;
        }

        sz = be64toh(sz);

        if (sz > 0){
            size_t   nleft = sz;
            ssize_t  nread;

            while (nleft > 0) {
                if((nread = read(fd, buff, nleft)) < 0) {
                    if (nleft == sz) return -1; /* error, return -1 */
                    else break; /* error, return amount read so far */
                } else if (nread == 0) break; /* EOF */
                nleft -= nread;
                buff += nread;
            }
            return(sz - nleft); /* return >= 0 */
        }
    }

};


class ConnTcp : public ConnType {
protected:
    int port;
    
    std::queue<Handle*> ready;             // Ready connections
    std::map<size_t, Handle*> connections;  // Active connections for this Connector

    fd_set set, tmpset;
    int listen_sck;
    int fdmax;


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

        if (listen(listen_sck, MAXRETRY) < 0){
            printf("Error listening\n");
            return -1;
        }

        return 0;
    }


public:    
    /*TODO: Qui abbiamo bisogno di un oggetto che permetta di identificare un
            endpoint di ascolto. Utile per TCP ma probabilmente inutile per MPI.
            Per ora solo port dovrebbe andare bene, dato che ascoltiamo da tutte
            le interfacce di rete
    */
    ConnTcp(int port) : port(port) {}

    int init() {
        if(this->_init())
            return -1;

        // intialize both sets (master, temp)
        FD_ZERO(&set);
        FD_ZERO(&tmpset);

        // add the listen socket to the master set
        FD_SET(this->listen_sck, &set);

        // hold the greater descriptor
        fdmax = this->listen_sck;
    }

    void update(std::queue<Handle*>& q, std::queue<Handle*>& qnew) {
        // copy the master set to the temporary
        tmpset = set;
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
                    } else {
                        FD_SET(connfd, &set);
                        if(connfd > fdmax) fdmax = connfd;
                        connections[connfd] = new HandleTCP(this, connfd);
                    }
                    qnew.push(connections[connfd]);
                }
                else {
                    // Updates ready connections and removes from listening
                    FD_CLR(idx, &set);

                    // update the maximum file descriptor
                    if (idx == fdmax)
                        for(int ii=(fdmax-1);ii>=0;--ii)
                            if (FD_ISSET(ii, &set)){
                                fdmax = ii;
                                break;
                            }

                    // ready.push(connections[idx]);
                    q.push(connections[idx]);
                }
                
            }
        }

        return;        
    }

    // URL: host:prot || label: stringa utente
    Handle* connect(const std::string& address, const std::string& label=std::string()) {
        size_t hash_val;
        if(!label.empty())
            hash_val = std::hash<std::string>{}(label);
        else
            hash_val = std::hash<std::string>{}(address);

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

            // NOTE: naming clash with global namespace connect, hence ::connect
            if (::connect(fd, rp->ai_addr, rp->ai_addrlen) != -1)
                break;                  /* Success */

            close(fd);
        }
        free(result);
            
        if (rp == NULL)            /* No address succeeded */
            return nullptr;

        /*NOTE: qui sto assumendo che sia praticamente impossibile avere un clash
                in hashtable tra i descrittori e i valori generati dalla hash
                function sulle stringhe label/URL
        */
        HandleTCP *handle = new HandleTCP(this, fd);
        connections[hash_val] = handle;

        return handle;
    }


    // HandleUser getReady(bool readMode) {
    //     HandleUser emptyhusr;

    //     /*NOTE: qui sto restituendo il controllo all'utente nel caso il primo
    //             nella coda fosse vuoto */
    //     if(ready.empty())
    //         return emptyhusr;
    //     else {
    //         HandleTCP* handle = nullptr;
    //         while(handle == nullptr || ready.empty()) {
    //             bool busy = ready.front()->isBusy();
    //             if(!busy) {
    //                 handle = (HandleTCP*)ready.front();
    //             }
    //             ready.pop();
    //         }
    //     }
    //     // if(readMode && ready.front()->isBusy()) {
    //     //     ready.pop();
    //     //     return emptyhusr;
    //     // }
        

    //     HandleTCP *handle = (HandleTCP*)ready.front();
    //     ready.pop();
    //     HandleUser husr(handle, readMode);
    // }


    virtual void removeConnection(Handle*);


    void notify_yield(Handle* h) {
        int fd = reinterpret_cast<HandleTCP*>(h)->fd;
        FD_SET(fd, &set);
        if(fd > fdmax) {
            fdmax = fd;
        }
    }


    void notify_request(Handle* h) {
        int fd = reinterpret_cast<HandleTCP*>(h)->fd;
        FD_CLR(fd, &set);

        // update the maximum file descriptor
        if (fd == fdmax)
            for(int ii=(fdmax-1);ii>=0;--ii)
                if (FD_ISSET(ii, &set)){
                    fdmax = ii;
                    break;
                }
    }

};

#endif