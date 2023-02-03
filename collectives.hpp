#ifndef COLLECTIVES_HPP
#define COLLECTIVES_HPP

#include <iostream>
#include <map>
#include <vector>

#include "handle.hpp"

class CollectiveContext {
protected:
    int size;
    bool root;
    int rank;
    bool completed = false;

    ssize_t probeHandle(Handle* realHandle, size_t& size, const bool blocking=true) {
		if (realHandle->probed.first) { // previously probed, return 0 if EOS received
			size=realHandle->probed.second;
			return (size?sizeof(size_t):0);
		}
        if (!realHandle) {
			MTCL_PRINT(100, "[internal]:\t", "HandleUser::probe EBADF\n");
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
    CollectiveContext(int size, bool root, int rank) : size(size), root(root), rank(rank) {}

    size_t getSize() {
        return size;
    }

    /**
     * @brief Updates the status of the collective during the creation and
     * checks if the team is ready to be used.
     * 
     * @param[in] count number of received connections
     * @return true if the collective group is ready, false otherwise
     */
    virtual bool update(int count) = 0;

    /**
     * @brief Checks if the current state of the collective allows to perform
     * send operations.
     * 
     * @return true if the caller can send, false otherwise
     */
    virtual bool canSend() = 0;

    /**
     * @brief Checks if the current state of the collective allows to perform
     * receive operations.
     * 
     * @return true if the caller can receive, false otherwise
     */
    virtual bool canReceive() = 0;

    /**
     * @brief Receives at most \b size data into \b buff from the \b participants,
     * based on the semantics of the collective.
     * 
     * @param[in] participants vector of participants to the collective operation
     * @param[out] buff buffer used to write data
     * @param[in] size maximum amount of data to be written in the buffer
     * @return ssize_t if successful, returns the amount of data written in the
     * buffer. Otherwise, -1 is return and \b errno is set.
     */
    virtual ssize_t receive(std::vector<Handle*>& participants, void* buff, size_t size) = 0;
    
    /**
     * @brief Sends \b size bytes of \b buff to the \b participants, following
     * the semantics of the collective.
     * 
     * @param[in] participants vector of participatns to the collective operation
     * @param[in] buff buffer of data to be sent
     * @param[in] size amount of data to be sent
     * @return ssize_t if successful, returns \b size. Otherwise, -1 is returned
     * and \b errno is set.
     */
    virtual ssize_t send(std::vector<Handle*>& participants, const void* buff, size_t size) = 0;


    /**
     * @brief Check for incoming message and write in \b size the amount of data
     * present in the message.
     * 
     * @param[in] participants vector of participatns to the collective operation
     * @param[out] size total size in byte of incoming message
     * @param[in] blocking if true, the probe call blocks until a message
     * is ready to be received. If false, the call returns immediately and sets
     * \b errno to \b EWOULDBLOCK if no message is present on this handle.
     * @return ssize_t \c sizeof(size_t) upon success. If \c -1 is returned,
     * the error can be checked via \b errno.
     */
	virtual ssize_t probe(std::vector<Handle*>& participants, size_t& size, const bool blocking=true)=0;

    virtual ssize_t execute(std::vector<Handle*>& participants, const void* sendbuff, size_t sendsize, void* recvbuff, size_t recvsize) {
        return 0;
    }

    virtual ~CollectiveContext() {};
};



class Broadcast : public CollectiveContext {
public:
    Broadcast(int size, bool root, int rank) : CollectiveContext(size, root, rank) {}
    // Solo il root ha eventi in ricezione, non abbiamo bisogno di fare alcun
    // controllo per gli altri
    bool update(int count) {
        completed = count == size - 1; 

        return completed;
    }

    bool canSend() {
        return root;
    }

    bool canReceive() {
        return !root;
    }

    ssize_t probe(std::vector<Handle*>& participants, size_t& size, const bool blocking=true) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            errno = EINVAL;
            return -1;
        }

        // Broadcast for non-root should always have 1 handle
        ssize_t res = -1;
        if(participants.size() == 1) {
            auto h = participants.at(0);
            res = probeHandle(h, size, blocking);
            if(res == 0 && size == 0) {
                participants.pop_back();
            }
        }
        else {
            MTCL_ERROR("[internal]:\t", "HandleGroup::broadcast expected size 1 in non root process\n");
            return -1;
        }

        return res;

    }

    ssize_t send(std::vector<Handle*>& participants, const void* buff, size_t size) {
        if(!canSend()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        for(auto& h : participants) {
            if(h->send(buff, size) < 0)
                return -1;
        }

        return size;
    }

    ssize_t receive(std::vector<Handle*>& participants, void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        // Broadcast for non-root should always have 1 handle
        ssize_t res = -1;
        if(participants.size() == 1) {
            auto h = participants.at(0);
            res = receiveFromHandle(h, buff, size);
            if(res == 0) {
                participants.pop_back();
            }
        }
        else {
            MTCL_ERROR("[internal]:\t", "HandleGroup::broadcast expected size 1 in non root process\n");
            return -1;
        }

        return res;
    }

    ~Broadcast() {}

};


class FanIn : public CollectiveContext {
private:
    ssize_t probed_idx = -1;

public:
    FanIn(int size, bool root, int rank) : CollectiveContext(size, root, rank) {}

    bool update(int count) {
        completed = count == size - 1; 

        return completed;
    }

    bool canSend() {
        return !root;
    }

    bool canReceive() {
        return root;
    }

    ssize_t probe(std::vector<Handle*>& participants, size_t& size, const bool blocking=true) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            errno = EINVAL;
            return -1;
        }

        ssize_t res = -1;
        auto iter = participants.begin();
        while(res == -1) {
            auto h = *iter;
            res = probeHandle(h, size, false);
            if(res == 0 && size == 0) {
                iter = participants.erase(iter);
                if(iter == participants.end()) iter = participants.begin();
                continue;
            }
            if(res > 0) {
                probed_idx = iter - participants.begin();
                printf("Probed message with size: %ld - handle in pos: %ld\n", size, probed_idx);
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

    ssize_t receive(std::vector<Handle*>& participants, void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        // I already probed one of the handles, I must receive from the same one
        if(probed_idx != -1) {
            size_t s = 0;
            auto h = participants.at(probed_idx);

            ssize_t res = probeHandle(h, s, false);
            if(res == 0 && s == 0) {
                participants.erase(participants.begin()+probed_idx);
            }
            if(res > 0) {
                printf("Probed message with size: %ld\n", s);
                if(receiveFromHandle(h, buff, s) <= 0)
                    return -1;
            }

            probed_idx = -1;
            return res;

        }


        ssize_t res = -1;
        auto iter = participants.begin();
        while(res == -1) {
            size_t s = 0;
            auto h = *iter;
            res = probeHandle(h, s, false);
            if(res == 0 && s == 0) {
                iter = participants.erase(iter);
                if(iter == participants.end()) iter = participants.begin();
                res = -1;
                continue;
            }
            if(res > 0) {
                printf("Probed message with size: %ld\n", s);
                if(receiveFromHandle(h, buff, s) <= 0)
                    return -1;
            }
            iter++;
            if(iter == participants.end()) iter = participants.begin();
        }

        return res;
    }

    ssize_t send(std::vector<Handle*>& participants, const void* buff, size_t size) {
        if(!canSend()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }
        
        for(auto& h : participants) {
            h->send(buff, size);
        }

        return 0;
    }

    ~FanIn() {}
};


class FanOut : public CollectiveContext {
private:
    size_t current = 0;

public:
    FanOut(int size, bool root, int rank) : CollectiveContext(size, root, rank) {}

    bool update(int count) {
        completed = count == size - 1; 

        return completed;
    }

    bool canSend() {
        return root;
    }

    bool canReceive() {
        return !root;
    }

    ssize_t probe(std::vector<Handle*>& participants, size_t& size, const bool blocking=true) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            errno = EINVAL;
            return -1;
        }

        size_t s;
        ssize_t res = -1;
        for(auto& h : participants) {
            res = probeHandle(h, s, blocking);
            if(res == 0 && s == 0) {
                participants.pop_back();
            }
        }

        return res;

    }

    ssize_t receive(std::vector<Handle*>& participants, void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        ssize_t res = -1;
        for(auto& h : participants) {
            res = receiveFromHandle(h, buff, size);
            if(res == 0) {
                participants.pop_back();
            }
        }

        return res;
    }

    ssize_t send(std::vector<Handle*>& participants, const void* buff, size_t size) {
        if(!canSend()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        size_t count = participants.size();
        auto h = participants.at(current);
        
        int res = h->send(buff, size);

        printf("Sent message to %ld\n", current);
        
        ++current %= count;

        return res;
    }

    ~FanOut () {}
};


class Gather : public CollectiveContext {
private:
    size_t current = 0;
    bool allReady;

public:
    Gather(int size, bool root, int rank) : CollectiveContext(size, root, rank) {}

    bool update(int count) {
        completed = count == size - 1; 

        return completed;
    }

    bool canSend() {
        return !root;
    }

    bool canReceive() {
        return root;
    }

    ssize_t probe(std::vector<Handle*>& participants, size_t& size, const bool blocking=true) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            errno = EINVAL;
            return -1;
        }

        allReady = true;
        size_t s;
        ssize_t res = -1;
        for(auto& h : participants) {
            res = probeHandle(h, s, blocking);
            if(res == 0 && s == 0) {
                participants.pop_back();
            }
            allReady = allReady && (res > 0);
        }

        return allReady ? sizeof(size_t) : -1;

    }


    // Qui il buffer deve essere grande quanto (participants.size()+1)*size
    ssize_t receive(std::vector<Handle*>& participants, void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

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

    ssize_t send(std::vector<Handle*>& participants, const void* buff, size_t size) {
        if(!canSend()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }
        
        for(auto& h : participants) {
            h->send(&rank, sizeof(int));
            h->send(buff, size);
        }

        return 0;
    }

    ssize_t execute(std::vector<Handle*>& participants, const void* sendbuff, size_t sendsize, void* recvbuff, size_t recvsize) {
        if(root) {
            memcpy((char*)recvbuff+(rank*sendsize), sendbuff, sendsize);
            return this->receive(participants, recvbuff, recvsize);
        }
        else {
            return this->send(participants, sendbuff, sendsize);
        }
    }

    ~Gather () {}
};


CollectiveContext *createContext(std::string type, int size, bool root, int rank)
{
    static const std::map<std::string, std::function<CollectiveContext*()>> contexts = {
        {"broadcast",  [&]{return new Broadcast(size, root, rank);}},
        {"fan-in",  [&]{return new FanIn(size, root, rank);}},
        {"fan-out",  [&]{return new FanOut(size, root, rank);}},
        {"gather",  [&]{return new Gather(size, root, rank);}}

    };

    if (auto found = contexts.find(type); found != contexts.end()) {
        return found->second();
    } else {
        return nullptr;
    }
}



#endif //COLLECTIVES_HPP