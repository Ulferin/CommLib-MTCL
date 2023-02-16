#ifndef COLLECTIVECONTEXT_HPP
#define COLLECTIVECONTEXT_HPP

#include <iostream>
#include <map>
#include <vector>
#include "../utils.hpp"
#include "collectiveImpl.hpp"

#ifdef ENABLE_MPI
#include "mpiImpl.hpp"
#endif

#ifdef ENABLE_UCX
#include "uccImpl.hpp"
#endif

enum CollectiveType {
    BROADCAST,
    FANIN,
    FANOUT,
    GATHER
};


class CollectiveContext {
protected:
    int size;
    bool root;
    int rank;
    CollectiveImpl* coll;
    CollectiveType type;
    bool canSend, canReceive;
    bool completed = false;

    std::pair<bool, size_t> probed{false, 0};
    std::atomic<bool> closed{false};

public:
    CollectiveContext(int size, bool root, int rank, CollectiveType type,
            bool canSend=false, bool canReceive=false) : size(size), root(root),
                rank(rank), type(type), canSend(canSend), canReceive(canReceive) {}

    void setImplementation(ImplementationType impl, std::vector<Handle*> participants) {
        const std::map<CollectiveType, std::function<CollectiveImpl*()>> contexts = {
            {BROADCAST,  [&]{
                    CollectiveImpl* coll = nullptr;
                    switch (impl) {
                        case GENERIC:
                            coll = new BroadcastGeneric(participants);
                            break;
                        case MPI:
                            #ifdef ENABLE_MPI
                            coll = new BroadcastMPI(participants, root);
                            #endif
                            break;
                        case UCC:
                            #ifdef ENABLE_UCX
                            coll = new BroadcastUCC(participants, rank, size, root);
                            #endif
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
            MTCL_PRINT(100, "[internal]: \t", "CollectiveContext::setImplementation error\n");
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
    bool update(int count) {
        completed = count == (size - 1); 

        return completed;
    }

    /**
     * @brief Receives at most \b size data into \b buff based on the
     * semantics of the collective.
     * 
     * @param[out] buff buffer used to write data
     * @param[in] size maximum amount of data to be written in the buffer
     * @return ssize_t if successful, returns the amount of data written in the
     * buffer. Otherwise, -1 is return and \b errno is set.
     */
    ssize_t receive(void* buff, size_t size) {
        if(!canReceive) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        size_t sz;
        if(!probed.first) {
            ssize_t r;
            if((r = probe(sz, true)) <= 0) return r;
        }
        else if(closed) return 0;

        if((sz = probed.second) > size) {
            MTCL_ERROR("[internal]:\t", "HandleGroup::receive ENOMEM, receiving less data\n");
			errno=ENOMEM;
			return -1;
        }

        probed = {false, 0};
        return coll->receive(buff, std::min(sz,size));
    }
    
    /**
     * @brief Sends \b size bytes of \b buff, following the semantics of the collective.
     * 
     * @param[in] buff buffer of data to be sent
     * @param[in] size amount of data to be sent
     * @return ssize_t if successful, returns \b size. Otherwise, -1 is returned
     * and \b errno is set.
     */
    ssize_t send(const void* buff, size_t size) {
        if(!canSend) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        return coll->send(buff, size);
    }


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
	ssize_t probe(size_t& size, const bool blocking=true) {
        if(!canReceive) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            errno = EINVAL;
            return -1;
        }

        // Previously probed, we retrieve the last result
        if(probed.first) {
            size = probed.second;
            return (size ? sizeof(size_t) : 0);
        }

        if(closed) return 0;

        ssize_t r;

        // Probe failure
        if((r = coll->probe(size, blocking)) <= 0) {
            switch (r) {
            case 0: {
                coll->close();
                return 0;
            }
            case -1: {
                if(errno == ECONNRESET) {
                    coll->close();
                    return 0;
                }
                if(errno == EWOULDBLOCK || errno == EAGAIN) {
                    errno = EWOULDBLOCK;
                    return -1;
                }
            }}
        }

        // Success
        probed = {true, size};
        if(size == 0) { // EOS
            // NOTE: chiudiamo tutta la collettiva???
            // coll->close();
            return 0;
        }

        return r;
    }

    virtual ssize_t execute(const void* sendbuff, size_t sendsize, void* recvbuff, size_t recvsize) {
        return 0;
    }

    virtual void close() {
        coll->close();
    }

    virtual ~CollectiveContext() {delete coll;};
};


CollectiveContext *createContext(CollectiveType type, int size, bool root, int rank)
{
    const std::map<CollectiveType, std::function<CollectiveContext*()>> contexts = {
        {BROADCAST,  [&]{return new CollectiveContext(size, root, rank, type, root, !root);}},
        {FANIN,  [&]{return new CollectiveContext(size, root, rank, type, !root, root);}},
        {FANOUT,  [&]{return new CollectiveContext(size, root, rank, type, root, !root);}},
        {GATHER,  [&]{return nullptr;}}

    };

    if (auto found = contexts.find(type); found != contexts.end()) {
        return found->second();
    } else {
        return nullptr;
    }
}

#endif //COLLECTIVECONTEXT_HPP