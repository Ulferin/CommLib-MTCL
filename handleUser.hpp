#ifndef HANDLEUSER_HPP
#define HANDLEUSER_HPP

#include "handle.hpp"
#include "errno.h"

class HandleUser {
    friend class ConnType;
    friend class Manager;
    Handle * realHandle;
    bool isReadable    = false;
	bool isWritable    = true;
    bool newConnection = true;
public:
    HandleUser() : HandleUser(nullptr, false, false) {}
    HandleUser(Handle* h, bool r, bool n):
		realHandle(h), isReadable(r), newConnection(n) {
		if (h) h->incrementReferenceCounter();
    }

    HandleUser(const HandleUser&) = delete;
    HandleUser& operator=(HandleUser const&) = delete;
    HandleUser& operator=(HandleUser && o) {
		if (this != &o) {
			realHandle    = o.realHandle;
			isReadable    = o.isReadable;
			isWritable    = o.isWritable;
			newConnection = o.newConnection;
			o.realHandle  = nullptr;
			o.isReadable  = false;
			o.isWritable  = false;
			o.newConnection=false;
		}
		return *this;
	}
	
    HandleUser(HandleUser&& h) :
		realHandle(h.realHandle), isReadable(h.isReadable), isWritable(h.isWritable), newConnection(h.newConnection) {
        h.realHandle = nullptr;
		h.isReadable = h.isWritable = h.newConnection = false;
    }
    
    // releases the handle to the manager
    void yield() {
        isReadable = false;
        newConnection = false;
        if (realHandle) realHandle->yield();
    }

    bool isValid() {
        return realHandle;
    }

    bool isNewConnection() {
        return newConnection;
    }

    size_t getID(){
        return (size_t)realHandle;
    }

	const std::string& getName() { return realHandle->getName(); }
	void setName(const std::string& name) { realHandle->setName(name);}
	
    ssize_t send(const void* buff, size_t size){
		if (!isWritable) {
			MTCL_PRINT(100, "[internal]:\t", "HandleUser::send EBADF (1)\n");
            errno = EBADF; // the "communicator" is not valid or closed
			return -1;
		}
        newConnection = false;
        if (!realHandle || realHandle->closed) {
			MTCL_PRINT(100, "[internal]:\t", "HandleUser::send EBADF (2)\n");
            errno = EBADF; // the "communicator" is not valid or closed
            return -1;
        }
        return realHandle->send(buff, size);
    }

	ssize_t probe(size_t& size, const bool blocking=true) {
        newConnection = false;
		if (realHandle->probed.first) {
			size=realHandle->probed.second;
			return sizeof(size_t);
		}
        if (!isReadable){
			MTCL_PRINT(100, "[internal]:\t", "HandleUser::probe handle not readable\n");
			return 0;
        }
        if (!realHandle) {
			MTCL_PRINT(100, "[internal]:\t", "HandleUser::probe EBADF\n");
            errno = EBADF; // the "communicator" is not valid or closed
            return -1;
        }
		if (realHandle->closed) return 0;

		// reading the header to get the size of the message
		ssize_t r;
		if ((r=realHandle->probe(size, true))<=0) {
			if (r==0) {
				isReadable=false;
				realHandle->close(!isWritable, true);
				return 0;
			}
			if (r==-1 && errno==ECONNRESET) {
				isReadable=isWritable=false;
				realHandle->close(true, true);
				return 0;
			}
			return r;
		}
		if (size==0) { // EOS received
			realHandle->close(!isWritable, true);
			isReadable=false;
			return 0;
		}
		realHandle->probed={true,size};
		return r;		
	}

    ssize_t receive(void* buff, size_t size) {
		size_t sz;
		if (!realHandle->probed.first) {
			// reading the header to get the size of the message
			ssize_t r;
			if ((r=this->probe(sz, true))<=0) {
				return r;
			}
			if (sz>size) {
				MTCL_PRINT(100, "[internal]:\t", "HandleUser::receive ENOMEM\n");
				errno=ENOMEM;
				return -1;
			}
		} else {
			newConnection = false;
			if (!isReadable){
				MTCL_PRINT(100, "[internal]:\t", "HandleUser::probe handle not readable\n");
				return 0;
			}
			if (!realHandle) {
				MTCL_PRINT(100, "[internal]:\t", "HandleUser::probe EBADF\n");
				errno = EBADF; // the "communicator" is not valid or closed
				return -1;
			}
			if (realHandle->closed) return 0;
			if ((sz=realHandle->probed.second)>size) {
				MTCL_PRINT(100, "[internal]:\t", "HandleUser::receive ENOMEM\n");
				errno=ENOMEM;
				return -1;
			}
		}
		realHandle->probed={false,0};
		return realHandle->receive(buff, sz);
    }

    void close(){
        if (realHandle) realHandle->close(true, !isReadable);
		isWritable=false;
    }

    ~HandleUser(){
        // if this handle is readable and it is not closed, when i destruct this handle implicitly i'm giving the control to the runtime.
        if (isReadable && realHandle && !realHandle->closed) this->yield();

        // decrement the reference counter of the wrapped handle to manage its destruction.
        if (realHandle) {
			realHandle->decrementReferenceCounter();
		}
    }


};

#endif
