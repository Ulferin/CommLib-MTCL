#ifndef COLLECTIVES_HPP
#define COLLECTIVES_HPP

#include <iostream>
#include <map>
#include <vector>

#ifdef ENABLE_MPI
#include <mpi.h>
#endif

#include "handle.hpp"

enum CollectiveType {
    BROADCAST,
    FANIN,
    FANOUT,
    GATHER
};

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

    protected:
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
    CollectiveImpl(std::vector<Handle*> participants) : participants(participants) {}

    virtual ssize_t probe(size_t& size, const bool blocking=true) = 0;
    virtual ssize_t send(const void* buff, size_t size) = 0;
    virtual ssize_t receive(void* buff, size_t size) = 0;

    void close() {
        for(auto& h : participants)
            h->close(true, true);
    }
};

#ifdef ENABLE_MPI
/**
 * @brief MPI implementation of collective operations. Abstract class, only provides
 * generic functionalities for collectives using the MPI transport. Subclasses must
 * implement collective-specific behavior.
 * 
 */
class MPICollective : public CollectiveImpl {
protected:
    bool root;
    int root_rank, local_rank;
    MPI_Comm comm;

    int* ranks;

public:
    MPICollective(std::vector<Handle*> participants, bool root) : CollectiveImpl(participants), root(root) {
        // Creazione comunicatore

        //TODO: aggiungere conversione endianess
        MPI_Comm_rank(MPI_COMM_WORLD, &local_rank);
        int coll_size;
        if(root) {
            coll_size = participants.size() + 1;
            root_rank = local_rank;
            ranks = new int[participants.size()+1];
            ranks[0] = root_rank;


            for(size_t i = 0; i < participants.size(); i++) {
                int remote_rank;
                receiveFromHandle(participants.at(i), &remote_rank, sizeof(int));
                ranks[i+1] = remote_rank;
            }

            for(auto& p : participants) {
                p->send(ranks, sizeof(int)*(participants.size()+1));
            }

        }
        else {
            participants.at(0)->send(&local_rank, sizeof(int));
            size_t sz;
            probeHandle(participants.at(0), sz, true);
            coll_size = sz/sizeof(int);
            ranks = new int[sz];
            receiveFromHandle(participants.at(0), ranks, sz);
            root_rank = ranks[0];
        }

        MPI_Group group, group_world;
        MPI_Comm_group(MPI_COMM_WORLD, &group_world);
        MPI_Group_incl(group_world, coll_size, ranks, &group);
        MPI_Comm_create_group(MPI_COMM_WORLD, group, 0, &comm);

        //TODO: close delle connessioni???
    }
};
#endif //ENABLE_MPI

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
            res = probeHandle(h, size, blocking);
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
            res = receiveFromHandle(h, buff, size);
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

#ifdef ENABLE_MPI
class BroadcastMPI : public MPICollective {
private:
    MPI_Request request_header = MPI_REQUEST_NULL;
    bool probed;

public:
    BroadcastMPI(std::vector<Handle*> participants, bool root) : MPICollective(participants, root) {}


    ssize_t probe(size_t& size, const bool blocking=true) {

        if(request_header == MPI_REQUEST_NULL) {
            MPI_Ibcast(&size, 1, MPI_UNSIGNED_LONG, root_rank, comm, &request_header);
            //TODO: Check errori
        }
        MPI_Status status;
        int flag{0}, count{0};
        if(blocking) {
            if(MPI_Wait(&request_header, &status) != MPI_SUCCESS) {
                MTCL_ERROR("[internal]:\t", "BroadcastMPI wait failed\n");
                errno=EBADF;
                return -1;
            }
        }
        else {
            if(MPI_Test(&request_header, &flag, &status) != MPI_SUCCESS) {
                MTCL_ERROR("[internal]:\t", "BroadcastMPI test failed\n");
                return -1;
            }

            if(!flag) {
                errno = EWOULDBLOCK;
                return -1;
            }
        }

        if(MPI_Get_count(&status, MPI_BYTE, &count) != MPI_SUCCESS) {
            MTCL_ERROR("[internal]:\t", "BroadcastMPI get_count failed\n");
            return -1;
        }
        size = (size_t)count;            

        return sizeof(size_t);
    }

    ssize_t send(const void* buff, size_t size) {
        MPI_Status status;
        MPI_Ibcast(&size, 1, MPI_UNSIGNED_LONG, root_rank, comm, &request_header);
        MPI_Wait(&request_header, &status);
        if(MPI_Bcast((void*)buff, size, MPI_BYTE, root_rank, comm) != MPI_SUCCESS) {
            errno = ECOMM;
            return -1;
        }

        return size;
    }

    ssize_t receive(void* buff, size_t size) {
        size_t sz;
        if(!probed) probe(sz, true);
        
        if(MPI_Bcast((void*)buff, size, MPI_BYTE, root_rank, comm) != MPI_SUCCESS) {
            errno = ECOMM;
            return -1;
        }
        

        return size;
    }
};
#endif //ENABLE_MPI


class FanInGeneric : public CollectiveImpl {
private:
    ssize_t probed_idx = -1;

public:
    ssize_t probe(size_t& size, const bool blocking=true) {
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

    ssize_t send(const void* buff, size_t size) {
        for(auto& h : participants) {
            h->send(buff, size);
        }

        return 0;
    }

    ssize_t receive(void* buff, size_t size) {
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

public:
    FanInGeneric(std::vector<Handle*> participants) : CollectiveImpl(participants) {}

};


class FanOutGeneric : public CollectiveImpl {
private:
    size_t current = 0;

public:
    ssize_t probe(size_t& size, const bool blocking=true) {
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

    ssize_t send(const void* buff, size_t size) {
        size_t count = participants.size();
        auto h = participants.at(current);
        
        int res = h->send(buff, size);

        printf("Sent message to %ld\n", current);
        
        ++current %= count;

        return res;
    }

    ssize_t receive(void* buff, size_t size) {
        ssize_t res = -1;
        for(auto& h : participants) {
            res = receiveFromHandle(h, buff, size);
            if(res == 0) {
                participants.pop_back();
            }
        }

        return res;
    }

public:
    FanOutGeneric(std::vector<Handle*> participants) : CollectiveImpl(participants) {}

};


class CollectiveContext {
protected:
    int size;
    bool root;
    int rank;
    CollectiveImpl* coll;
    CollectiveType type;
    bool completed = false;

public:
    CollectiveContext(int size, bool root, int rank, CollectiveType type) : size(size), root(root), rank(rank), type(type) {}

    void setImplementation(ImplementationType impl, std::vector<Handle*> participants) {
        static const std::map<CollectiveType, std::function<CollectiveImpl*()>> contexts = {
            {BROADCAST,  [&]{
                    CollectiveImpl* coll;
                    switch (impl) {
                        case GENERIC:
                            coll = new BroadcastGeneric(participants);
                            break;
                        case MPI:
                            #ifdef ENABLE_MPI
                            coll = new BroadcastMPI(participants, root);
                            #else
                            //TODO: check da qualche parte per questi nullptr
                            coll = nullptr;
                            #endif
                            break;
                        case UCC:
                            coll = nullptr;
                            break;
                        default:
                            coll = nullptr;
                            break;
                    }
                    return coll;
                }
            },
            {FANIN,  [&]{return new FanInGeneric(participants);}},
            {FANOUT, [&]{return new FanOutGeneric(participants);}},
            {GATHER,  [&]{return nullptr;}}

        };

        if (auto found = contexts.find(type); found != contexts.end()) {
            coll = found->second();
        } else {
            coll = nullptr;
        }
    }

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
     * @brief Receives at most \b size data into \b buff based on the
     * semantics of the collective.
     * 
     * @param[out] buff buffer used to write data
     * @param[in] size maximum amount of data to be written in the buffer
     * @return ssize_t if successful, returns the amount of data written in the
     * buffer. Otherwise, -1 is return and \b errno is set.
     */
    virtual ssize_t receive(void* buff, size_t size) = 0;
    
    /**
     * @brief Sends \b size bytes of \b buff, following the semantics of the collective.
     * 
     * @param[in] buff buffer of data to be sent
     * @param[in] size amount of data to be sent
     * @return ssize_t if successful, returns \b size. Otherwise, -1 is returned
     * and \b errno is set.
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

    virtual ssize_t execute(const void* sendbuff, size_t sendsize, void* recvbuff, size_t recvsize) {
        return 0;
    }

    virtual void close() {
        coll->close();
    }

    virtual ~CollectiveContext() {};
};



class Broadcast : public CollectiveContext {
public:
    Broadcast(int size, bool root, int rank) : CollectiveContext(size, root, rank, BROADCAST) {}
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

    ssize_t probe(size_t& size, const bool blocking=true) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            errno = EINVAL;
            return -1;
        }

        return coll->probe(size, blocking);

    }

    ssize_t send(const void* buff, size_t size) {
        if(!canSend()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        return coll->send(buff, size);
    }

    ssize_t receive(void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        return coll->receive(buff, size);
    }

    ~Broadcast() {}

};


class FanIn : public CollectiveContext {

public:
    FanIn(int size, bool root, int rank) : CollectiveContext(size, root, rank, FANIN) {}

    bool update(int count) {
        completed = count == (size - 1); 

        return completed;
    }

    bool canSend() {
        return !root;
    }

    bool canReceive() {
        return root;
    }

    ssize_t probe(size_t& size, const bool blocking=true) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            errno = EINVAL;
            return -1;
        }

        return coll->probe(size, blocking);

    }

    ssize_t receive(void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        return coll->receive(buff, size);
    }

    ssize_t send(const void* buff, size_t size) {
        if(!canSend()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }
        
        return coll->send(buff, size);
    }

    ~FanIn() {}
};



class FanOut : public CollectiveContext {

public:
    FanOut(int size, bool root, int rank) : CollectiveContext(size, root, rank, FANOUT) {}

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

    ssize_t probe(size_t& size, const bool blocking=true) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            errno = EINVAL;
            return -1;
        }

        return coll->probe(size, blocking);

    }

    ssize_t receive(void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        return coll->receive(buff, size);
    }

    ssize_t send(const void* buff, size_t size) {
        if(!canSend()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        return coll->send(buff, size);
    }

    ~FanOut () {}
};


/*
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
*/


CollectiveContext *createContext(CollectiveType type, int size, bool root, int rank)
{
    static const std::map<CollectiveType, std::function<CollectiveContext*()>> contexts = {
        {BROADCAST,  [&]{return new Broadcast(size, root, rank);}},
        {FANIN,  [&]{return new FanIn(size, root, rank);}},
        {FANOUT,  [&]{return new FanOut(size, root, rank);}},
        {GATHER,  [&]{return nullptr;}}

    };

    if (auto found = contexts.find(type); found != contexts.end()) {
        return found->second();
    } else {
        return nullptr;
    }
}



#endif //COLLECTIVES_HPP