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

    virtual bool update(int count) = 0;
    virtual bool canSend() = 0;
    virtual bool canReceive() = 0;
    virtual ssize_t receive(std::vector<Handle*> participants, void* buff, size_t size) = 0;
    virtual ssize_t send(std::vector<Handle*> participants, const void* buff, size_t size) = 0;
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

    ssize_t send(std::vector<Handle*> participants, const void* buff, size_t size) {
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

    ssize_t receive(std::vector<Handle*> participants, void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        // Broadcast for non-root should always have 1 handle
        size_t s;
        if(participants.size() == 1) {
            participants.at(0)->probe(s, true);
            if(participants.at(0)->receive(buff, s) <= 0)
                return -1;
        }
        else {
            MTCL_ERROR("[internal]:\t", "HandleGroup::broadcast expected size 1 in non root process\n");
            return -1;
        }

        return s;
    }

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

    ssize_t receive(std::vector<Handle*> participants, void* buff, size_t size) {
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
            // Cheating
            if(res > 0 && s == 0) {
                h->close();
                res = -1;
            }
            printf("Probed message with size: %ld\n", s);
            if(res > 0) {
                if(h->receive(buff, s) <= 0)
                    return -1;
            }
            if(iter == participants.end()) iter = participants.begin();
            else iter++;
        }

        return 0;
    }

    ssize_t send(std::vector<Handle*> participants, const void* buff, size_t size) {
        if(!canSend()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }
        
        for(auto& h : participants) {
            h->send(buff, size);
        }

        return 0;
    }
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

    ssize_t receive(std::vector<Handle*> participants, void* buff, size_t size) {
        if(!canReceive()) {
            MTCL_PRINT(100, "[internal]:\t", "Invalid operation for the collective\n");
            return -1;
        }

        int res = 0;
        for(auto& h : participants) {
            size_t s;
            res = h->probe(s, true);
            res = h->receive(buff, s);
        }

        return res;
    }

    ssize_t send(std::vector<Handle*> participants, const void* buff, size_t size) {
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