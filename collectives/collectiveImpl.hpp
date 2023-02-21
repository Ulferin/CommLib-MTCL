#ifndef COLLECTIVEIMPL_HPP
#define COLLECTIVEIMPL_HPP

/*
 *
 * [ ] flush buffer lettura fino a ricezione EOS
 * [ ] Implementazione Gather
 * [ ] Implementazione ottimizzazioni Gather
 * [ ] Aggiunta metodo execute per tutte le collettive
 * [ ] Distruzione handle interni
 * 
 * 
 * */


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
			MTCL_PRINT(100, "[internal]:\t", "CollectiveImpl::probeHandle EBADF\n");
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
				MTCL_PRINT(100, "[internal]:\t", "CollectiveImpl::receiveFromHandle EBADF\n");
				errno = EBADF; // the "communicator" is not valid or closed
				return -1;
			}
			if (realHandle->closed_rd) return 0;
		}
		if ((sz=realHandle->probed.second)>size) {
			MTCL_ERROR("[internal]:\t", "CollectiveImpl::receiveFromHandle ENOMEM, receiving less data\n");
			errno=ENOMEM;
			return -1;
		}	   
		realHandle->probed={false,0};
		return realHandle->receive(buff, std::min(sz,size));
    }

    void flushHandle(Handle* h) {
        // Send EOS if not already sent
        if(!h->closed_wr)
            h->close(true, false);

        // If EOS is still not received we wait for it, discarding pending messages
        if(!h->closed_rd) {
            size_t sz = 1;
            while(true) {
                if(h->probe(sz) == -1) {
                    MTCL_PRINT(100, "[internal]:\t", "CollectiveImpl::flushHandle probe error\n");
                    return;
                }
                if(sz == 0) break;
                char* buff = new char[sz];
                if(h->receive(buff, sz) == -1) {
                    MTCL_PRINT(100, "[internal]:\t", "CollectiveImpl::flushHandle receive error\n");
                    return;
                }
                delete[] buff;
            }
        }

        // Finally closing the handle
        h->close(false, true);
    }

public:
    CollectiveImpl(std::vector<Handle*> participants) : participants(participants) {
        for(auto& h : participants) h->incrementReferenceCounter();
    }

    virtual ssize_t probe(size_t& size, const bool blocking=true) = 0;
    virtual ssize_t send(const void* buff, size_t size) = 0;
    virtual ssize_t receive(void* buff, size_t size) = 0;
    virtual void close(bool close_wr=true, bool close_rd=true) = 0;

    virtual ssize_t execute(const void* sendbuff, size_t sendsize, void* recvbuff, size_t recvsize) {
        return -1;
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
                h->close(true, true);
                participants.pop_back();
            }
        }
        else {
            MTCL_ERROR("[internal]:\t", "BroadcastGeneric::probe expected size 1 in non root process - size: %ld\n", participants.size());
            return -1;
        }

        return res;

    }

    ssize_t send(const void* buff, size_t size) {
        // TODO: aggiungere detect EOS per chiudere tutto se uno dei partecipanti fa close
        for(auto& h : participants) {
            if(h->send(buff, size) < 0) {
                errno = ECONNRESET;
                return -1;
            }
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
            MTCL_ERROR("[internal]:\t", "BroadcastGeneric::receive expected size 1 in non root process - size: %ld\n", participants.size());
            return -1;
        }

        return res;
    }

    void close(bool close_wr=true, bool close_rd=true) {
        for(auto& h : participants) {
            flushHandle(h);
        }
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
        while(res == -1 && !participants.empty()) {
            auto h = *iter;
            res = h->probe(size, false);
            // The handle sent EOS, we remove it from participants and go on
            // looking for a "real" message
            if(res > 0 && size == 0) {
                iter = participants.erase(iter);
                res = -1;
                h->close(true, true);
                if(iter == participants.end()) {
                    if(blocking) {
                        iter = participants.begin();
                        continue;
                    }
                    else break;
                }
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

        // All participants have closed their connection, we "notify" the HandleUser
        // that an EOS has been received for the entire group
        if(participants.empty()) {
            size = 0;
            res = sizeof(size_t);
        }

        return res;
    }

    ssize_t send(const void* buff, size_t size) {
        ssize_t r;
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

    void close(bool close_wr=true, bool close_rd=true) {
        // If non-root is closing, we flush by sending EOS and waiting EOS from
        // root. Root's EOS will arrive when its next probe catch our EOS.
        // If root is closing, we don't care anymore about messages, but we have
        // to flush them anyway. We send EOS and receive EOS to each participant.
        for(auto& h : participants) flushHandle(h);
    }

public:
    FanInGeneric(std::vector<Handle*> participants) : CollectiveImpl(participants) {}

};


class FanOutGeneric : public CollectiveImpl {
private:
    size_t current = 0;

public:
    ssize_t probe(size_t& size, const bool blocking=true) {
        if(participants.empty()) {
            errno = ECONNRESET;
            return -1;
        }

        auto h = participants.at(0);
        ssize_t res = h->probe(size, blocking);
        // EOS
        if(res > 0 && size == 0) {
            participants.pop_back();
            h->close(true, true);
        }

        return res;
    }

    ssize_t send(const void* buff, size_t size) {
        size_t count = participants.size();
        auto h = participants.at(current);
        
        int res = h->send(buff, size);
        // if(res < 0) participants.erase(h);

        ++current %= count;

        return res;
    }

    ssize_t receive(void* buff, size_t size) {
        auto h = participants.at(0);
        ssize_t res = h->receive(buff, size);

        return res;
    }

    void close(bool close_wr=true, bool close_rd=true) {
        if(participants.empty()) return;
        
        for(auto& h : participants) flushHandle(h);
    }

public:
    FanOutGeneric(std::vector<Handle*> participants) : CollectiveImpl(participants) {}

};


class GatherGeneric : public CollectiveImpl {
private:
    size_t current = 0;
    bool root;
    int rank;
    bool allReady;
public:
    GatherGeneric(std::vector<Handle*> participants, bool root, int rank) :
        CollectiveImpl(participants), root(root), rank(rank) {}

    ssize_t probe(size_t& size, const bool blocking=true) {
        allReady = true;
        size_t s;
        ssize_t res = -1;
        for(auto& h : participants) {
            if((res = probeHandle(h, s, blocking)) < 0)
                return res;
            
            if(res == 0 && s == 0) {
                participants.pop_back();
            }
            allReady = allReady && (res > 0);
        }
        return allReady ? sizeof(size_t) : -1;
    }

    // Qui il buffer deve essere grande quanto (participants.size()+1)*size
    ssize_t receive(void* buff, size_t size) {
        for(auto& h : participants) {
            size_t sz;
            int remote_rank;
            probeHandle(h, sz, true);
            receiveFromHandle(h, &remote_rank, sz);
            probeHandle(h, sz, true);
            receiveFromHandle(h, (char*)buff+(remote_rank*size), size);
        }
        return 0;
    }

    ssize_t send(const void* buff, size_t size) {
        for(auto& h : participants) {
            h->send(&rank, sizeof(int));
            h->send(buff, size);
        }
        
        return 0;
    }

    ssize_t execute(const void* sendbuff, size_t sendsize, void* recvbuff, size_t recvsize) {
        if(root) {
            memcpy((char*)recvbuff+(rank*sendsize), sendbuff, sendsize);
            return this->receive(recvbuff, recvsize);
        }
        else {
            return this->send(sendbuff, sendsize);
        }
    }

    void close(bool close_wr=true, bool close_rd=true) {
        return;
    }
    
    ~GatherGeneric () {}
};

#endif //COLLECTIVEIMPL_HPP