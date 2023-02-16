#ifndef COLLECTIVEIMPL_HPP
#define COLLECTIVEIMPL_HPP

#include <iostream>
#include <map>
#include <vector>

#include "../handle.hpp"
#include "../utils.hpp"


enum ImplementationType {
    GENERIC,
    MPI,
    UCC
};


/**
 * @brief Interface for transport-specific network functionalities for collective
 * operations. Subclasses specify different behaviors depending on the specific
 * transport used to implement the collective operations and on the specific type
 * of collective.
 * 
 */
class CollectiveImpl {
protected:
    std::vector<Handle*> participants;
    //TODO: implementare qui can_send/can_receive
    // virtual bool canSend() = 0;
    // virtual bool canReceive() = 0;

    protected:
    ssize_t probeHandle(Handle* realHandle, size_t& size, const bool blocking=true) {
		if (realHandle->probed.first) { // previously probed, return 0 if EOS received
			size=realHandle->probed.second;
			return (size ? sizeof(size_t) : 0);
		}
        if (!realHandle) {
			MTCL_PRINT(100, "[internal]:\t", "HandleGroup::probe EBADF\n");
            errno = EBADF; // the "communicator" is not valid or closed
            return -1;
        }
		if (realHandle->closed_rd) return 0;

		// reading the header to get the size of the message
		ssize_t r;
		if ((r=realHandle->probe(size, blocking))<=0) {
			switch(r) {
			case 0: {
				realHandle->close(true, true);
				return 0;
			}
			case -1: {				
				if (errno==ECONNRESET) {
					realHandle->close(true, true);
					return 0;
				}
				if (errno==EWOULDBLOCK || errno==EAGAIN) {
					errno = EWOULDBLOCK;
					return -1;
				}
			}}
			return r;
		}
		realHandle->probed={true,size};
		if (size==0) { // EOS received
			realHandle->close(false, true);
			return 0;
		}
		return r;		
	}

    ssize_t receiveFromHandle(Handle* realHandle, void* buff, size_t size) {
		size_t sz;
		if (!realHandle->probed.first) {
			// reading the header to get the size of the message
			ssize_t r;
			if ((r=probeHandle(realHandle, sz, true))<=0) {
				return r;
			}
		} else {
			if (!realHandle) {
				MTCL_PRINT(100, "[internal]:\t", "HandleUser::probe EBADF\n");
				errno = EBADF; // the "communicator" is not valid or closed
				return -1;
			}
			if (realHandle->closed_rd) return 0;
		}
		if ((sz=realHandle->probed.second)>size) {
			MTCL_ERROR("[internal]:\t", "HandleUser::receive ENOMEM, receiving less data\n");
			errno=ENOMEM;
			return -1;
		}	   
		realHandle->probed={false,0};
		return realHandle->receive(buff, std::min(sz,size));
    }

public:
    CollectiveImpl(std::vector<Handle*> participants) : participants(participants) {}

    virtual ssize_t probe(size_t& size, const bool blocking=true) = 0;
    virtual ssize_t send(const void* buff, size_t size) = 0;
    virtual ssize_t receive(void* buff, size_t size) = 0;

    void close() {
        for(auto& h : participants)
            h->close(true, true);
    }

    virtual ~CollectiveImpl() {}
};

/**
 * @brief Generic implementation of Broadcast collective using low-level handles.
 * This implementation is intended to be used by those transports that do not have
 * an optimized implementation of the Broadcast collective. This implementation
 * can be selected using the \b BROADCAST type and the \b GENERIC implementation,
 * provided, respectively, by @see CollectiveType and @see ImplementationType. 
 * 
 */
class BroadcastGeneric : public CollectiveImpl {
    ssize_t probe(size_t& size, const bool blocking=true) {
        // Broadcast for non-root should always have 1 handle
        ssize_t res = -1;
        if(participants.size() == 1) {
            auto h = participants.at(0);
            res = h->probe(size, blocking);
            if(res == 0 && size == 0) {
                participants.pop_back();
            }
        }
        else {
            MTCL_ERROR("[internal]:\t", "HandleGroup::broadcast expected size 1 in non root process - size: %ld\n", participants.size());
            return -1;
        }

        return res;

    }

    ssize_t send(const void* buff, size_t size) {
        for(auto& h : participants) {
            if(h->send(buff, size) < 0)
                return -1;
        }

        return size;
    }

    ssize_t receive(void* buff, size_t size) {
        // Broadcast for non-root should always have 1 handle
        ssize_t res = -1;
        if(participants.size() == 1) {
            auto h = participants.at(0);
            res = h->receive(buff, size);
            if(res == 0) {
                participants.pop_back();
            }
        }
        else {
            MTCL_ERROR("[internal]:\t", "HandleGroup::broadcast expected size 1 in non root process - size: %ld\n", participants.size());
            return -1;
        }

        return res;
    }

public:
    BroadcastGeneric(std::vector<Handle*> participants) : CollectiveImpl(participants) {}

};


class FanInGeneric : public CollectiveImpl {
private:
    ssize_t probed_idx = -1;

public:
    ssize_t probe(size_t& size, const bool blocking=true) {
        ssize_t res = -1;
        auto iter = participants.begin();
        while(res == -1) {
            auto h = *iter;
            res = h->probe(size, false);
            if(res > 0 && size == 0) {
                iter = participants.erase(iter);
                if(iter == participants.end()) iter = participants.begin();
                res = -1;
                h->close(true, true);
                continue;
            }
            if(res > 0) {
                probed_idx = iter - participants.begin();
            }
            iter++;
            if(iter == participants.end()) {
                if(blocking)
                    iter = participants.begin();
                else break;
            }
        }

        return res;
    }

    ssize_t send(const void* buff, size_t size) {
        ssize_t r;
        printf("Sending: %s\n", (char*)buff);
        for(auto& h : participants) {
            if((r = h->send(buff, size)) < 0) return r;
        }

        return 0;
    }

    ssize_t receive(void* buff, size_t size) {
        // I already probed one of the handles, I must receive from the same one
        ssize_t r;
        auto h = participants.at(probed_idx);

        if((r = h->receive(buff, size)) <= 0)
            return -1;

        probed_idx = -1;

        return r;
    }

public:
    FanInGeneric(std::vector<Handle*> participants) : CollectiveImpl(participants) {}

};


class FanOutGeneric : public CollectiveImpl {
private:
    size_t current = 0;

public:
    ssize_t probe(size_t& size, const bool blocking=true) {
        auto h = participants.at(0);
        ssize_t res = h->probe(size, blocking);

        return res;
    }

    ssize_t send(const void* buff, size_t size) {
        size_t count = participants.size();
        auto h = participants.at(current);
        
        int res = h->send(buff, size);

        ++current %= count;

        return res;
    }

    ssize_t receive(void* buff, size_t size) {
        auto h = participants.at(0);
        ssize_t res = h->receive(buff, size);

        return res;
    }

public:
    FanOutGeneric(std::vector<Handle*> participants) : CollectiveImpl(participants) {}

};

#endif //COLLECTIVEIMPL_HPP