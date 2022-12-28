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

class HandleTCP : public Handle {

    ssize_t readn(int fd, char *ptr, size_t n) {  
        size_t   nleft = n;
        ssize_t  nread;

        while (nleft > 0) {
            if((nread = read(fd, ptr, nleft)) < 0) {
                if (nleft == n) return -1; /* error, return -1 */
                else break; /* error, return amount read so far */
            } else if (nread == 0) break; /* EOF */
            nleft -= nread;
            ptr += nread;
        }
        return(n - nleft); /* return >= 0 */
    }

    ssize_t writen(int fd, const char *ptr, size_t n) {  
        size_t   nleft = n;
        ssize_t  nwritten;
        
        while (nleft > 0) {
            if((nwritten = write(fd, ptr, nleft)) < 0) {
                if (nleft == n) return -1; /* error, return -1 */
                else break; /* error, return amount written so far */
            } else if (nwritten == 0) break; 
            nleft -= nwritten;
            ptr   += nwritten;
        }
        return(n - nleft); /* return >= 0 */
    }

public:
    int fd; // File descriptor of the connection represented by this Handle
    HandleTCP(ConnType* parent, int fd, bool busy=true) : Handle(parent, busy), fd(fd) {}

    ssize_t send(const void* buff, size_t size) {
        return writen(fd, (const char*)buff, size); 
    }

    ssize_t receive(void* buff, size_t size) {
        return readn(fd, (char*)buff, size); 
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
#if defined(SINGLE_IO_THREAD)
	int fdmax;
#else	
    std::atomic<int> fdmax;
    std::shared_mutex shm;
#endif

private:
    /**
     * @brief Initializes the main listening socket for this Handle
     * 
     * @return int status code
     */
    int _init() {
        if ((listen_sck=socket(AF_INET, SOCK_STREAM, 0)) < 0){
			MTCL_TCP_ERROR("socket ERROR: errno=%d -- %s\n", errno, strerror(errno));
            return -1;
        }

        int enable = 1;
        // enable the reuse of the address
        if (setsockopt(listen_sck, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
			MTCL_TCP_ERROR("setsockopt ERROR: errno=%d -- %s\n", errno, strerror(errno));
            return -1;
        }

        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET; 
        serv_addr.sin_addr.s_addr = INADDR_ANY; // listening on any interface
        serv_addr.sin_port = htons(port);

        int bind_err;
        if ((bind_err = bind(listen_sck, (struct sockaddr*)&serv_addr,sizeof(serv_addr))) < 0){
			MTCL_TCP_ERROR("bind ERROR: errno=%d -- %s\n", errno, strerror(errno));
            return -1;
        }

        if (::listen(listen_sck, TCP_BACKLOG) < 0){
			MTCL_TCP_ERROR("listen ERROR: errno=%d -- %s\n", errno, strerror(errno));
            return -1;
        }

        return 0;
    }


public:

   ConnTcp(){};
   ~ConnTcp(){};

    int init(std::string) {
		// For clients who do just connect, the communication thread anyway calls
		// the update method, and we do not want to call the select function with
		// invalid fields.
        FD_ZERO(&set);
        FD_ZERO(&tmpset);
		fdmax = -1;
        return 0;
    }

    int listen(std::string s) {
        address = s.substr(0, s.find(":"));
        port = stoi(s.substr(address.length()+1));

		if (this->_init())
			return -1;
		
        MTCL_TCP_PRINT("listening on: %s:%d\n", address.c_str(),port);

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

        REMOVE_CODE_IF(std::unique_lock ulock(shm, std::defer_lock));
        //std::shared_lock shlock(shm, std::defer_lock);  <-----------????

        REMOVE_CODE_IF(ulock.lock());
        tmpset = set;
        REMOVE_CODE_IF(ulock.unlock());

        struct timeval wait_time = {.tv_sec=0, .tv_usec=TCP_POLL_TIMEOUT};
		int nready=0;
        switch(nready=select(fdmax+1, &tmpset, NULL, NULL, &wait_time)) {
		case -1: MTCL_TCP_ERROR("select ERROR: errno=%d -- %s\n", errno, strerror(errno));
		case  0: return;
        }

        for(int idx=0; idx <= fdmax && nready>0; idx++){
            if (FD_ISSET(idx, &tmpset)){
                if (idx == this->listen_sck) {
                    int connfd = accept(this->listen_sck, (struct sockaddr*)NULL ,NULL);
                    if (connfd == -1){
						MTCL_TCP_ERROR("accept ERROR: errno=%d -- %s\n", errno, strerror(errno));
                        return;
                    }

                    REMOVE_CODE_IF(ulock.lock());
                    FD_SET(connfd, &set);
                    if(connfd > fdmax) fdmax = connfd;
                    connections[connfd] = new HandleTCP(this, connfd, false);
                    addinQ({true, connections[connfd]});
                    REMOVE_CODE_IF(ulock.unlock());                    
                } else {
                    REMOVE_CODE_IF(ulock.lock());
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
                    REMOVE_CODE_IF(ulock.unlock());

                }
				--nready;
            }
        }
    }

    // URL: host:prot || label: stringa utente
    Handle* connect(const std::string& address/*, const std::string& label=std::string()*/) {
		const std::string host = address.substr(0, address.find(":"));
		const std::string svc  = address.substr(host.length()+1);
		
		MTCL_TCP_PRINT("connect to %s:%s\n", host.c_str(), svc.c_str());

        int fd;

        struct addrinfo hints;
        struct addrinfo *result, *rp;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;                /* Allow IPv4 or IPv6 */
        hints.ai_socktype = SOCK_STREAM;            /* Stream socket */
        hints.ai_flags = 0;
        hints.ai_protocol = IPPROTO_TCP;            /* Allow only TCP */

        // resolve the address (assumo stringa formattata come host:port)
        if (getaddrinfo(host.c_str(), svc.c_str(), &hints, &result) != 0)
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
		{
			REMOVE_CODE_IF(std::unique_lock lock(shm));
			connections[fd] = handle;
		}
        return handle;
    }

    void notify_close(Handle* h) {
        int fd = reinterpret_cast<HandleTCP*>(h)->fd;
        close(fd);
        REMOVE_CODE_IF(std::unique_lock lock(shm));
        connections.erase(fd);
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
		REMOVE_CODE_IF(std::unique_lock l(shm));
        FD_SET(fd, &set);
        if(fd > fdmax) {
            fdmax = fd;
        }
    }

    void end() {
        auto modified_connections = connections;
        for(auto& [fd, h] : modified_connections)
            if(isSet(fd))
                setAsClosed(h); 
    }

    bool isSet(int fd){
        REMOVE_CODE_IF(std::shared_lock s(shm));
        return FD_ISSET(fd, &set);
    }

};

#endif
