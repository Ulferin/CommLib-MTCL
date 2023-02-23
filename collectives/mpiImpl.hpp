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

    MPI_Request request_header = MPI_REQUEST_NULL;
    bool closing = false;
    ssize_t last_probe = -1;
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


class BroadcastMPI : public MPICollective {
private:

public:
    BroadcastMPI(std::vector<Handle*> participants, bool root) : MPICollective(participants, root) {}


    ssize_t probe(size_t& size, const bool blocking=true) {
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
        if(MPI_Bcast((void*)buff, size, MPI_BYTE, root_rank, comm) != MPI_SUCCESS) {
            errno = ECOMM;
            return -1;
        }
        
        return size;
    }

    void close(bool close_wr=true, bool close_rd=true) {
        // Non-root process must wait for the root process to terminate before
        // it can issue a close operation.
        if(!root && !closing) {
            MTCL_ERROR("[internal]:\t", "Non-root process trying to close with active root process. Aborting.\n");
            errno = EINVAL;
            return;
        }

        // Root process can issue the close to all its non-root processes. At
        // finalize it will flush the EOS messages coming from non-root proc.
        if(root) {
            size_t EOS = 0;
            MPI_Ibcast(&EOS, 1, MPI_UNSIGNED_LONG, root_rank, comm, &request_header);
        }
        
        return;
    }

    void finalize() {
        if(closing) return;

        if(root) {
            // The user didn't call the close explicitly
            if(request_header == nullptr) {
                this->close(true, true);
            }
            MPI_Status status;
            MPI_Wait(&request_header, &status);
        }
        else {
            size_t sz = 1;
            do {
                this->probe(sz, true);
            }while(sz != 0);
        }
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

        last_probe = probe_data[(root_rank + 1) % (participants.size() + 1)];
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

    ssize_t execute(const void* sendbuff, size_t sendsize, void* recvbuff, size_t recvsize) {
        if(root && closing)
            return 0;
        if(root) {
            size_t sz;
            if(last_probe == -1) this->probe(sz, true);
            if(closing) return 0;
            last_probe = -1;
        }
        else {
            this->send(nullptr, sendsize);
        }

        if(MPI_Gather(sendbuff, sendsize, MPI_BYTE, recvbuff, recvsize, MPI_BYTE, root_rank, comm) != MPI_SUCCESS) {
            errno = ECOMM;
            return -1;
        }
        return sizeof(size_t);
    }

    void close(bool close_wr=true, bool close_rd=true) {
        if(root && !closing) {
            MTCL_ERROR("[internal]:\t", "GatherMPI root process trying to close with active non-root process. Aborting.\n");
            errno = EINVAL;
            return;
        }

        if(!root) {
            size_t EOS = 0;
            MPI_Igather(&EOS, 1, MPI_UNSIGNED_LONG, nullptr, 0, MPI_UNSIGNED_LONG, root_rank, comm, &request_header);
        }
    }

    void finalize() {
        if(closing) return;

        if(!root) {
            // The user didn't call the close explicitly
            if(request_header == nullptr) {
                this->close(true, true);
            }
            MPI_Status status;
            MPI_Wait(&request_header, &status);
        }
        else {
            size_t sz = 1;
            do {
                this->probe(sz, true);
            }while(sz != 0);
        }
    }
};

#endif //MPICOLLIMPL_HPP