#ifndef HANDLE_HPP
#define HANDLE_HPP

#include <iostream>
#include <atomic>

#include "protocolInterface.hpp"

class Handle {
    friend class HandleGroup;
    friend class HandleUser;
    friend class Manager;
    friend class ConnType;

    ConnType* parent;
    std::atomic<int> counter = 0;
	std::string handleName{"no-name-provided"};
	
    void incrementReferenceCounter(){
        counter++;
    }

    void decrementReferenceCounter(){
        counter--;
        if (counter == 0 && closed_wr && closed_rd){
            delete this;
        }
    }
protected:	
	// if first=true second is the size contained in the header
	std::pair<bool, size_t> probed{false,0};  
	std::atomic<bool> closed_rd = false, closed_wr = false;
    virtual ssize_t sendEOS() = 0;
private:
    void yield() {
        if (!closed_rd)
            parent->notify_yield(this);
    }


public:
    void close(bool close_wr=true, bool close_rd=true){
		if (close_wr && !closed_wr){
            this->sendEOS();
            closed_wr = true;
        }

        if (close_rd && !closed_rd){
            closed_rd = true;
        }
        
        parent->notify_close(this, closed_wr, closed_rd);

        if (counter == 0 && closed_rd && closed_wr)
            delete this;
        
        /*if (!closed) {
			parent->notify_close(this, close_wr, close_rd);
			if (close_wr && close_rd) closed=true;
		}
        if (counter == 0 && closed) {
            delete this;
		}*/
    }
    
    Handle(ConnType* parent) : parent(parent) {}

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
     * @brief Check for incoming message and write in \b size the amount of data
     * present in the message.
     * 
     * @param[out] size total size in byte of incoming message
     * @param[in] blocking if true, the probe call blocks until a message
     * is ready to be received. If false, the call returns immediately and sets
     * \b errno to \b EWOULDBLOCK if no message is present on this handle.
     * @return ssize_t \c sizeof(size_t) upon success. If \c -1 is returned,
     * the error can be checked via \b errno.
     */
	virtual ssize_t probe(size_t& size, const bool blocking=true)=0;
	

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


	void setName(const std::string &name) { handleName = name; }
	const std::string& getName() { return handleName; }
	const bool isClosed()   { return closed_rd && closed_wr; }
	
    virtual ~Handle() {};
};


void ConnType::setAsClosed(Handle* h){
    // Send EOS if not already sent
    if(!h->closed_wr)
        h->close(true, false);

    // If EOS is still not received we wait for it, discarding pending messages
    if(!h->closed_rd) {
        size_t sz = 1;
        while(true) {
            if(h->probe(sz) == -1) {
                MTCL_PRINT(100, "[internal]:\t", "ConnType::setAsClosed probe error\n");
                return;
            }
            if(sz == 0) break;
            char* buff = new char[sz];
            if(h->receive(buff, sz) == -1) {
                MTCL_PRINT(100, "[internal]:\t", "ConnType::setAsClosed receive error\n");
                return;
            }
            delete[] buff;
        }
    }

    // Finally closing the handle
    h->close(false, true);

}

#endif
