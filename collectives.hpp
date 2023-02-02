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
    bool completed = false;

public:
    CollectiveContext(int size, bool root) : size(size), root(root) {}

    /**
     * @brief Updates the status of the collective during the creation and
     * checks if the team is ready to be used.
     * 
     * @param count number of received connections
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
     * @param participants vector of participants to the collective operation
     * @param buff buffer used to write data
     * @param size maximum amount of data to be written in the buffer
     * @return ssize_t if successful, returns the amount of data written in the
     * buffer. Otherwise, -1 is return and \b errno is set.
     */
    virtual ssize_t receive(std::vector<Handle*>& participants, void* buff, size_t size) = 0;
    
    /**
     * @brief Sends \b size bytes of \b buff to the \b participants, following
     * the semantics of the collective.
     * 
     * @param participants vector of participatns to the collective operation
     * @param buff buffer of data to be sent
     * @param size amount of data to be sent
     * @return ssize_t if successful, returns \b size. Otherwise, -1 is returned
     * and \b errno is set.
     */
    virtual ssize_t send(std::vector<Handle*>& participants, const void* buff, size_t size) = 0;

    virtual ~CollectiveContext() {};
};



class Broadcast : public CollectiveContext {
public:
    Broadcast(int size, bool root) : CollectiveContext(size, root) {}
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
        size_t s;
        if(participants.size() == 1) {
            auto h = participants.at(0);
            size_t res = h->probe(s, true);
            if(res > 0 && s == 0) {
                h->close();
                participants.pop_back();
                return 0;
            }
            
            if(h->receive(buff, s) <= 0)
                return -1;
        }
        else {
            MTCL_ERROR("[internal]:\t", "HandleGroup::broadcast expected size 1 in non root process\n");
            return -1;
        }

        return s;
    }

    ~Broadcast() {}

};

class FanIn : public CollectiveContext {
public:
    FanIn(int size, bool root) : CollectiveContext(size, root) {}

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

    ssize_t receive(std::vector<Handle*>& participants, void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        ssize_t res = -1;
        auto iter = participants.begin();
        while(res == -1) {
            size_t s = 0;
            auto h = *iter;
            res = h->probe(s, false);
            if(res > 0 && s == 0) {
                h->close();
                iter = participants.erase(iter);
                if(iter == participants.end()) iter = participants.begin();
                res = -1;
                continue;
            }
            printf("Probed message with size: %ld\n", s);
            if(res > 0) {
                if(h->receive(buff, s) <= 0)
                    return -1;
            }
            iter++;
            if(iter == participants.end()) iter = participants.begin();
        }

        return 0;
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
    FanOut(int size, bool root) : CollectiveContext(size, root) {}

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

    ssize_t receive(std::vector<Handle*>& participants, void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        int res = 0;
        for(auto& h : participants) {
            size_t s = -1;
            res = h->probe(s, true);
            if(s == 0) {
                h->close();
                return -1;
            }
            res = h->receive(buff, s);
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


CollectiveContext *createContext(std::string type, int size, bool root)
{
    static const std::map<std::string, std::function<CollectiveContext*()>> contexts = {
        {"broadcast",  [&]{return new Broadcast(size, root);}},
        {"fan-in",  [&]{return new FanIn(size, root);}},
        {"fan-out",  [&]{return new FanOut(size, root);}}

    };

    if (auto found = contexts.find(type); found != contexts.end()) {
        return found->second();
    } else {
        return nullptr;
    }
}



#endif //COLLECTIVES_HPP