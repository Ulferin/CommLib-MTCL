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
    std::atomic<int> counter = 0;
	std::string handleName{"no-name-provided"};
	
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

	// DEVE ESSERE ATOMIC??????	
	std::atomic<bool> closed  = false;
private:
    void yield() {
        parent->notify_yield(this);
    }

    void close(bool close_wr=true, bool close_rd=true){
		if (!closed) {
			parent->notify_close(this, close_wr, close_rd);
			if (close_wr && close_rd) closed=true;
		}
        if (counter == 0 && closed) {
            delete this;
		}
    }

public:
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


	// COMMENTARE
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
	const bool isClosed()   { return closed; }
	
    virtual ~Handle() {};
};


void ConnType::setAsClosed(Handle* h){
    h->close();
}

#endif
