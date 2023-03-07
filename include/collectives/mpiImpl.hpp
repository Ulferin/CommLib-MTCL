#ifndef MPICOLLIMPL_HPP
#define MPICOLLIMPL_HPP

#include "collectiveImpl.hpp"
#include <mpi.h>

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
    MPI_Group group;
    
    MPI_Request request_header = MPI_REQUEST_NULL;
    bool closing = false;
    ssize_t last_probe = -1;
    int* ranks;

public:
    MPICollective(std::vector<Handle*> participants, bool root) : CollectiveImpl(participants), root(root) {

        //TODO: add endianess conversion
        MPI_Comm_rank(MPI_COMM_WORLD, &local_rank);
        int coll_size;
        if(root) {
            coll_size = participants.size() + 1;
            root_rank = local_rank;
            ranks = new int[participants.size()+1];
            ranks[0] = root_rank;

            for(size_t i = 0; i < participants.size(); i++) {
                participants.at(i)->send(&root_rank, sizeof(int));
                int remote_rank;
                receiveFromHandle(participants.at(i), &remote_rank, sizeof(int));
                ranks[i+1] = remote_rank;
            }

            for(auto& p : participants) {
                p->send(ranks, sizeof(int)*(participants.size()+1));
            }

        }
        else {
            int remote_rank;
            receiveFromHandle(participants.at(0), &remote_rank, sizeof(int));
            root_rank = remote_rank;

            participants.at(0)->send(&local_rank, sizeof(int));
            size_t sz;
            probeHandle(participants.at(0), sz, true);
            coll_size = sz/sizeof(int);
            ranks = new int[sz];
            receiveFromHandle(participants.at(0), ranks, sz);
        }

        MPI_Group group_world;
        MPI_Comm_group(MPI_COMM_WORLD, &group_world);
        MPI_Group_incl(group_world, coll_size, ranks, &group);
        MPI_Comm_create_group(MPI_COMM_WORLD, group, 0, &comm);

        delete[] ranks;
        //TODO: closing connections???
    }

    // MPI needs to override basic peek in order to correctly catch messages
    // using MPI collectives
    //NOTE: if yield is disabled, this function will never be called
    bool peek() override {
        size_t sz;
        ssize_t res = this->probe(sz, false);

        return res > 0;
    }

};


class BroadcastMPI : public MPICollective {
private:

public:
    BroadcastMPI(std::vector<Handle*> participants, bool root) : MPICollective(participants, root) {}


    ssize_t probe(size_t& size, const bool blocking=true) {
        if(last_probe != -1) {
            size = last_probe;
            return sizeof(size_t);
        }

        if(request_header == MPI_REQUEST_NULL) {
            if(MPI_Ibcast(&last_probe, 1, MPI_UNSIGNED_LONG, root_rank, comm, &request_header) != MPI_SUCCESS) {
                MTCL_ERROR("[internal]:\t", "BroadcastMPI::probe Ibcast failed\n");
                errno=EBADF;
                return -1;
            }   
        }
        MPI_Status status;
        int flag{0};
        if(blocking) {
            if(MPI_Wait(&request_header, &status) != MPI_SUCCESS) {
                MTCL_ERROR("[internal]:\t", "BroadcastMPI::probe wait failed\n");
                errno=EBADF;
                return -1;
            }
        }
        else {
            if(MPI_Test(&request_header, &flag, &status) != MPI_SUCCESS) {
                MTCL_ERROR("[internal]:\t", "BroadcastMPI::probe test failed\n");
                return -1;
            }

            if(!flag) {
                errno = EWOULDBLOCK;
                return -1;
            }
        }

        // EOS
        if(last_probe == 0) closing = true;
        request_header = MPI_REQUEST_NULL;
        size = last_probe;

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
        ssize_t res;
        if((res = this->probe(sz, true)) <= 0) return res;

        if(last_probe == 0) return 0;
        if(MPI_Bcast((void*)buff, size, MPI_BYTE, root_rank, comm) != MPI_SUCCESS) {
            errno = ECOMM;
            return -1;
        }

        last_probe = -1;
        
        return size;
    }

    ssize_t sendrecv(const void* sendbuff, size_t sendsize, void* recvbuff, size_t recvsize) {
        if(root) {
            return this->send(sendbuff, sendsize);
        }
        else {
            return this->receive(recvbuff, recvsize);
        }
    }

    void close(bool close_wr=true, bool close_rd=true) {
        // Root process can issue the close to all its non-root processes.
        if(root) {
            closing = true;
            size_t EOS = 0;
            MPI_Ibcast(&EOS, 1, MPI_UNSIGNED_LONG, root_rank, comm, &request_header);
        }
        
        return;
    }

    void finalize(bool, std::string name="") {

        if(root) {
            // The user didn't call the close explicitly
            if(!closing) {
                this->close(true, true);
            }
            MPI_Wait(&request_header, MPI_STATUS_IGNORE);
        }
        // non-root process didn't receive EOS
        else if(!closing) {
            while(true) {
                size_t sz;
                this->probe(sz, true);
                
                if(sz == 0) break;
				MTCL_ERROR("[internal]:\t", "Spurious message received of size %ld on handle with name %s!\n", sz, name.c_str());
                
                char* data = new char[sz];
                this->receive(data, sz);
                delete[] data;
                
            }
        }

        MPI_Group_free(&group);
        MPI_Comm_free(&comm);
    }
};


class GatherMPI : public MPICollective {
    size_t* probe_data;
    size_t  EOS = 0;

public:
    GatherMPI(std::vector<Handle*> participants, bool root) : MPICollective(participants, root) {
        probe_data = new size_t[participants.size()+1];
    }


    ssize_t probe(size_t& size, const bool blocking=true) {
        if(last_probe != -1) {
            size = last_probe;
            return sizeof(size_t);
        }

        if(request_header == MPI_REQUEST_NULL) {
            if(MPI_Igather(&last_probe, 1, MPI_UNSIGNED_LONG, probe_data, 1, MPI_UNSIGNED_LONG, root_rank, comm, &request_header) != MPI_SUCCESS) {
                MTCL_ERROR("[internal]:\t", "GatherMPI::probe Ibcast error\n");
                errno = ECOMM;
                return -1;
            }
        }
        MPI_Status status;
        int flag{0};
        if(blocking) {
            if(MPI_Wait(&request_header, &status) != MPI_SUCCESS) {
                MTCL_ERROR("[internal]:\t", "BroadcastMPI::probe wait failed\n");
                errno=EBADF;
                return -1;
            }
        }
        else {
            if(MPI_Test(&request_header, &flag, &status) != MPI_SUCCESS) {
                MTCL_ERROR("[internal]:\t", "BroadcastMPI::probe test failed\n");
                return -1;
            }

            if(!flag) {
                errno = EWOULDBLOCK;
                return -1;
            }
        }

        last_probe = probe_data[(local_rank + 1) % (participants.size() + 1)];
        if(last_probe == 0) closing = true;
        request_header = MPI_REQUEST_NULL;
        size = last_probe;

        return sizeof(size_t);
    }

    ssize_t send(const void* buff, size_t size) {
        MPI_Status status;
        if(MPI_Igather(&size, 1, MPI_UNSIGNED_LONG, nullptr, 0, MPI_UNSIGNED_LONG, root_rank, comm, &request_header) != MPI_SUCCESS) {
            errno = ECOMM;
            return -1;
        }
        MPI_Wait(&request_header, &status);

        return sizeof(size_t);
    }

    ssize_t receive(void* buff, size_t size) {
        return -1;
    }
    
    ssize_t sendrecv(const void* sendbuff, size_t sendsize, void* recvbuff, size_t recvsize) {
        if(root) {
            if(MPI_Igather(&recvsize, 1, MPI_UNSIGNED_LONG, probe_data, 1, MPI_UNSIGNED_LONG, 0, comm, &request_header) != MPI_SUCCESS) {
                MTCL_ERROR("[internal]:\t", "GatherMPI::probe Ibcast error\n");
                errno = ECOMM;
                return -1;
            }
            if(MPI_Wait(&request_header, MPI_STATUS_IGNORE) != MPI_SUCCESS) {
                MTCL_ERROR("[internal]:\t", "BroadcastMPI::probe wait failed\n");
                errno=EBADF;
                return -1;
            }

            // If at least one of the participants sent EOS header, we signal a
            // close operation in order to "invalidate" the call
            for(size_t i = 0; i < participants.size() + 1; i++) {
                if(probe_data[i] == 0) {
                    MTCL_PRINT(100, "[internal]:\t", "GatherMPI::sendrecv at least one of the participants closed the collective\n");
                    closing = true;
                    return 0;
                }

                if(probe_data[i] > recvsize) {
                    MTCL_ERROR("[internal]:\t", "GatherMPI::sendrecv the provided recvsize is too small\n");
                    errno = ENOMEM;
                    return -1;
                }
            }
        }
        else {
            MPI_Status status;
            if(MPI_Igather(&sendsize, 1, MPI_UNSIGNED_LONG, nullptr, 0, MPI_UNSIGNED_LONG, 0, comm, &request_header) != MPI_SUCCESS) {
                errno = ECOMM;
                return -1;
            }
            MPI_Wait(&request_header, &status);
        }

        if(MPI_Gather(sendbuff, sendsize, MPI_BYTE, recvbuff, recvsize, MPI_BYTE, 0, comm) != MPI_SUCCESS) {
            errno = ECOMM;
            return -1;
        }
        return sizeof(size_t);
    }

    void close(bool close_wr=true, bool close_rd=true) {

        if(!root) {
            size_t EOS = 0;
            MPI_Igather(&EOS, 1, MPI_UNSIGNED_LONG, nullptr, 0, MPI_UNSIGNED_LONG, 0, comm, &request_header);
            closing = true;
        }
    }

    void finalize(bool, std::string name="") {
        if(!root) {
            // The user didn't call the close explicitly
            if(!closing) {
                this->close(true, true);
            }
            MPI_Wait(&request_header, MPI_STATUS_IGNORE);
        }
        // root process didn't get the EOS
        else if(!closing) {
            while(true) {
                size_t sz = 1;
                if(MPI_Igather(&sz, 1, MPI_UNSIGNED_LONG, probe_data, 1, MPI_UNSIGNED_LONG, 0, comm, &request_header) != MPI_SUCCESS) {
                    MTCL_ERROR("[internal]:\t", "GatherMPI::probe Ibcast error\n");
                    errno = ECOMM;
                    return;
                }
                if(MPI_Wait(&request_header, MPI_STATUS_IGNORE) != MPI_SUCCESS) {
                    MTCL_ERROR("[internal]:\t", "BroadcastMPI::probe wait failed\n");
                    errno=EBADF;
                    return;
                }

                // If at least one of the participants sent EOS header, we signal a
                // close operation in order to "invalidate" the call
                for(size_t i = 0; i < participants.size() + 1; i++) {
                    if(probe_data[i] == 0) {
                        closing = true;
                        break;
                    }

                    if(i != (size_t)local_rank) sz = probe_data[i];
                }
                if(closing) break;
				MTCL_ERROR("[internal]:\t", "Spurious message received of size %ld on handle with name %s!\n", sz, name.c_str());
                sz = probe_data[(local_rank + 1) % (participants.size() + 1)];
                char* data = new char[sz];
                if(MPI_Bcast(data, sz, MPI_BYTE, root_rank, comm) != MPI_SUCCESS) {
                    errno = ECOMM;
                    delete[] data;
                    break;
                }
                delete[] data;
                
            }
        }

        delete[] probe_data;
        MPI_Group_free(&group);
        MPI_Comm_free(&comm);
    }
};

#endif //MPICOLLIMPL_HPP
