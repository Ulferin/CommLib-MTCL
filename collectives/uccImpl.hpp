#ifndef UCCCOLLIMPL_HPP
#define UCCCOLLIMPL_HPP

#include "collectiveImpl.hpp"
#include <ucc/api/ucc.h>

#define STR(x) #x
#define UCC_CHECK(_call)                                                \
    if (UCC_OK != (_call)) {                                            \
        MTCL_PRINT(100, "[internal]: \t", "UCC fail %s\n", STR(_call)); \
        exit(1);                                  \
    }

class UCCCollective : public CollectiveImpl {

typedef struct UCC_coll_info {
    std::vector<Handle*>* handles;      // Vector of handles of participants
    int rank;                           // Local rank
    int size;                           // Team size
    bool root;
    UCCCollective* coll_obj;
} UCC_coll_info_t;

protected:
    int rank, size;
    bool root;
    ucc_lib_config_h     lib_config;
    ucc_context_config_h ctx_config;
    ucc_team_h           team;
    ucc_context_h        ctx;
    ucc_lib_h            lib;
    ucc_coll_req_h req = nullptr;
    ssize_t last_probe = -1;
    bool closing = false;

    static ucc_status_t oob_allgather(void *sbuf, void *rbuf, size_t msglen,
                                  void *coll_info, void **req) {
        UCC_coll_info_t* info = (UCC_coll_info_t*)coll_info;

        auto handles = info->handles;

        if(info->root) {
            for(int i = 0; i < info->size-1; i++) {
                handles->at(i)->send(&info->rank, sizeof(int));
                int remote_rank;
                info->coll_obj->receiveFromHandle(handles->at(i), &remote_rank, sizeof(int));
                info->coll_obj->receiveFromHandle(handles->at(i), (char*)rbuf+(remote_rank*msglen), msglen);
            }

            memcpy((char*)rbuf+(info->rank*msglen), sbuf, msglen);
            for(auto& p : *handles) {
                p->send(rbuf, msglen*(info->size));
            }

        }
        else {
            int root_rank;
            info->coll_obj->receiveFromHandle(handles->at(0), &root_rank, sizeof(int));

            info->coll_obj->root_rank = root_rank;
            handles->at(0)->send(&info->rank, sizeof(int));
            handles->at(0)->send(sbuf, msglen);
            size_t sz;
            info->coll_obj->probeHandle(handles->at(0), sz, true);
            info->coll_obj->receiveFromHandle(handles->at(0), rbuf, sz);
        }

        return UCC_OK;
    }

    static ucc_status_t oob_allgather_test(void *req) {
        return UCC_OK;
    }

    static ucc_status_t oob_allgather_free(void *req) {
        return UCC_OK;
    }


    /* Creates UCC team from a group of handles. */
    static ucc_team_h create_ucc_team(UCC_coll_info_t* info, ucc_context_h ctx) {
        int rank = info->rank;
        int size = info->size;
        ucc_team_h        team;
        ucc_team_params_t team_params;
        ucc_status_t      status;

        team_params.mask          = UCC_TEAM_PARAM_FIELD_OOB;
        team_params.oob.allgather = oob_allgather;
        team_params.oob.req_test  = oob_allgather_test;
        team_params.oob.req_free  = oob_allgather_free;
        team_params.oob.coll_info = (void*)info;
        team_params.oob.n_oob_eps = size;
        team_params.oob.oob_ep    = rank;

        UCC_CHECK(ucc_team_create_post(&ctx, 1, &team_params, &team));
        while (UCC_INPROGRESS == (status = ucc_team_create_test(team))) {
            UCC_CHECK(ucc_context_progress(ctx));
        };
        if (UCC_OK != status) {
            fprintf(stderr, "failed to create ucc team\n");
            //TODO: CHECK
            exit(1);
        }
        return team;
    }

public:
    int root_rank;

    UCCCollective(std::vector<Handle*> participants, int rank, int size, bool root) : CollectiveImpl(participants), rank(rank), size(size), root(root) {
        /* === UCC collective operation === */
        /* Init ucc library */
        ucc_lib_params_t lib_params = {
            .mask        = UCC_LIB_PARAM_FIELD_THREAD_MODE,
            .thread_mode = UCC_THREAD_SINGLE
        };
        UCC_CHECK(ucc_lib_config_read(NULL, NULL, &lib_config));
        UCC_CHECK(ucc_init(&lib_params, lib_config, &lib));
        ucc_lib_config_release(lib_config);

        if(root) root_rank = rank;

        UCC_coll_info_t* info = new UCC_coll_info_t();
        info->handles = &participants;
        info->rank = rank;
        info->size = size;
        info->root = root;
        info->coll_obj = this;

        /* Init ucc context for a specified UCC_TEST_TLS */
        ucc_context_oob_coll_t oob = {
            .allgather    = oob_allgather,
            .req_test     = oob_allgather_test,
            .req_free     = oob_allgather_free,
            .coll_info    = (void*)info,
            .n_oob_eps    = (uint32_t)size, 
            .oob_ep       = (uint32_t)rank 
        };


        ucc_context_params_t ctx_params = {
            .mask             = UCC_CONTEXT_PARAM_FIELD_OOB,
            .oob              = oob
        };

        UCC_CHECK(ucc_context_config_read(lib, NULL, &ctx_config));
        UCC_CHECK(ucc_context_create(lib, &ctx_params, ctx_config, &ctx));
        ucc_context_config_release(ctx_config);

        team = create_ucc_team(info, ctx);
    }

    // UCX needs to override basic peek in order to correctly catch messages
    // using UCX collectives
    bool peek() override {
        size_t sz;
        ssize_t res = this->probe(sz, false);

        return res > 0;
    }

};


class BroadcastUCC : public UCCCollective {

public:
    BroadcastUCC(std::vector<Handle*> participants, int rank, int size, bool root) : UCCCollective(participants, rank, size, root) {}


    ssize_t probe(size_t& size, const bool blocking=true) {
        if(last_probe != -1) {
            size = last_probe;
            return sizeof(size_t);
        }

        ucc_coll_args_t      args;
        if(req == nullptr) {

            /* BROADCAST HEADER */
            args.mask              = 0;
            args.coll_type         = UCC_COLL_TYPE_BCAST;
            args.src.info.buffer   = &last_probe;
            args.src.info.count    = 1;
            args.src.info.datatype = UCC_DT_UINT64;
            args.src.info.mem_type = UCC_MEMORY_TYPE_HOST;
            args.root              = root_rank;

            UCC_CHECK(ucc_collective_init(&args, &req, team)); 
            UCC_CHECK(ucc_collective_post(req));
        }

        if(blocking) {
            while (UCC_INPROGRESS == ucc_collective_test(req)) { 
                UCC_CHECK(ucc_context_progress(ctx));
            }
        }
        else {
            UCC_CHECK(ucc_context_progress(ctx));
            if(UCC_INPROGRESS == ucc_collective_test(req)) {
                errno = EWOULDBLOCK;
                return -1;
            }
        }

        if(last_probe == 0) closing = true;

        ucc_collective_finalize(req);
        req = nullptr;
        size = last_probe;
        return sizeof(size_t);
    }

    ssize_t send(const void* buff, size_t size) {
        ucc_coll_args_t      args;
        ucc_coll_req_h       request;

        /* BROADCAST HEADER */
        args.mask              = 0;
        args.coll_type         = UCC_COLL_TYPE_BCAST;
        args.src.info.buffer   = &size;
        args.src.info.count    = 1;
        args.src.info.datatype = UCC_DT_UINT64;
        args.src.info.mem_type = UCC_MEMORY_TYPE_HOST;
        args.root              = root_rank;

        UCC_CHECK(ucc_collective_init(&args, &request, team)); 
        UCC_CHECK(ucc_collective_post(request));    
        while (UCC_INPROGRESS == ucc_collective_test(request)) { 
            UCC_CHECK(ucc_context_progress(ctx));
        }
        ucc_collective_finalize(request);
        
        /* BROADCAST DATA */
        args.src.info.buffer = (void*)buff;
        args.src.info.count = size;
        args.src.info.datatype = UCC_DT_UINT8;

        UCC_CHECK(ucc_collective_init(&args, &request, team)); 
        UCC_CHECK(ucc_collective_post(request));    
        while (UCC_INPROGRESS == ucc_collective_test(request)) { 
            UCC_CHECK(ucc_context_progress(ctx));
        }
        ucc_collective_finalize(request);

        return size;
    }


    ssize_t receive(void* buff, size_t size) {
        size_t sz;
        ssize_t res;
        if((res = this->probe(sz, true)) <= 0) return res;

        ucc_coll_args_t      args;
        ucc_coll_req_h       req;

        /* BROADCAST DATA */
        args.mask              = 0;
        args.coll_type         = UCC_COLL_TYPE_BCAST;
        args.src.info.buffer   = (void*)buff;
        args.src.info.count    = size;
        args.src.info.datatype = UCC_DT_UINT8;
        args.src.info.mem_type = UCC_MEMORY_TYPE_HOST;
        args.root              = root_rank;

        UCC_CHECK(ucc_collective_init(&args, &req, team)); 
        UCC_CHECK(ucc_collective_post(req));    
        while (UCC_INPROGRESS == ucc_collective_test(req)) { 
            UCC_CHECK(ucc_context_progress(ctx));
        }
        ucc_collective_finalize(req);
        
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
            ucc_coll_args_t      args;

            /* BROADCAST EOS */
            args.mask              = 0;
            args.coll_type         = UCC_COLL_TYPE_BCAST;
            args.src.info.buffer   = &EOS;
            args.src.info.count    = 1;
            args.src.info.datatype = UCC_DT_UINT64;
            args.src.info.mem_type = UCC_MEMORY_TYPE_HOST;
            args.root              = root_rank;

            UCC_CHECK(ucc_collective_init(&args, &req, team)); 
            UCC_CHECK(ucc_collective_post(req));
        }

        return;
    }

    void finalize(bool, std::string name="") {
        if(root) {
            // The user didn't call the close explicitly
            if(!closing) {
                this->close(true, true);
            }
            while (UCC_INPROGRESS == ucc_collective_test(req)) { 
                UCC_CHECK(ucc_context_progress(ctx));
            }
            ucc_collective_finalize(req);
        }
        else {
            while(!closing) {
                size_t sz = 1;
                this->probe(sz, true);
                if(sz == 0) break;
				MTCL_ERROR("[internal]:\t", "Spurious message received of size %ld on handle with name %s!\n", sz, name.c_str());

                char* data = new char[sz];
                this->receive(data, sz);
                delete[] data;
            }
        }

        // ucc_context_destroy(ctx);
    }


};

class GatherUCC : public UCCCollective {
    ucc_coll_args_t      close_args;
    size_t* probe_data;
    size_t EOS = 0;

public:
    GatherUCC(std::vector<Handle*> participants, int rank, int size, bool root) : UCCCollective(participants, rank, size, root) {
                probe_data = new size_t[participants.size()+1];
    }

    ssize_t probe(size_t& size, const bool blocking=true) {
        if(last_probe != -1) {
            size = last_probe;
            return sizeof(size_t);
        }

        ucc_coll_args_t      args;
        if(req == nullptr) {
            /* BROADCAST HEADER */
            args.mask              = 0;
            args.coll_type         = UCC_COLL_TYPE_GATHER;
            args.src.info.buffer   = &last_probe;
            args.src.info.count    = 1;
            args.src.info.datatype = UCC_DT_UINT64;
            args.src.info.mem_type = UCC_MEMORY_TYPE_HOST;
            if(root) {
                args.dst.info.buffer   = probe_data;
                args.dst.info.count    = 1;
                args.dst.info.datatype = UCC_DT_UINT64;
                args.dst.info.mem_type = UCC_MEMORY_TYPE_HOST;
            }
            args.root              = root_rank;

            UCC_CHECK(ucc_collective_init(&args, &req, team)); 
            UCC_CHECK(ucc_collective_post(req));
        }

        if(blocking) {
            while (UCC_INPROGRESS == ucc_collective_test(req)) { 
                UCC_CHECK(ucc_context_progress(ctx));
            }
        }
        else {
            UCC_CHECK(ucc_context_progress(ctx));
            if(UCC_INPROGRESS == ucc_collective_test(req)) {
                errno = EWOULDBLOCK;
                return -1;
            }
        }
        ucc_collective_finalize(req);

        last_probe = probe_data[(rank + 1) % this->size];
        if(last_probe == 0) closing=true;
        size = last_probe;
        req = nullptr;

        return sizeof(size_t);
    }

    ssize_t send(const void* buff, size_t size) {
        ucc_coll_args_t args;
        ucc_coll_req_h  request;
        ucc_status_t    status;

        /* BROADCAST HEADER */
        args.mask              = 0;
        args.coll_type         = UCC_COLL_TYPE_GATHER;
        args.src.info.buffer   = &size;
        args.src.info.count    = 1;
        args.src.info.datatype = UCC_DT_UINT64;
        args.src.info.mem_type = UCC_MEMORY_TYPE_HOST;
        args.root              = root_rank;

        UCC_CHECK(ucc_collective_init(&args, &request, team)); 
        UCC_CHECK(ucc_collective_post(request));    
        while (UCC_INPROGRESS == (status = ucc_collective_test(request))) { 
            UCC_CHECK(ucc_context_progress(ctx));
        }
        ucc_collective_finalize(request);

        return size;
    }


    ssize_t receive(void* buff, size_t size) {        
        return -1;
    }

    ssize_t sendrecv(const void* sendbuff, size_t sendsize, void* recvbuff, size_t recvsize) {        
        ucc_coll_args_t args;
        ucc_coll_req_h  request;

        if(root) {
            size_t sz;
            this->probe(sz, true);
            if(closing) return 0;
            last_probe = -1;
        }
        else {
            this->send(nullptr, sendsize);
        }

        args.mask              = 0;
        args.coll_type         = UCC_COLL_TYPE_GATHER;
        args.src.info.buffer   = (void*)sendbuff;
        args.src.info.count    = sendsize;
        args.src.info.datatype = UCC_DT_UINT8;
        args.src.info.mem_type = UCC_MEMORY_TYPE_HOST;
        if(root) {
            args.dst.info.buffer   = (void*)recvbuff;
            args.dst.info.count    = recvsize;
            args.dst.info.datatype = UCC_DT_UINT8;
            args.dst.info.mem_type = UCC_MEMORY_TYPE_HOST;
        }
        args.root              = root_rank;

        UCC_CHECK(ucc_collective_init(&args, &request, team)); 
        UCC_CHECK(ucc_collective_post(request));    
        while (UCC_INPROGRESS == ucc_collective_test(request)) { 
            UCC_CHECK(ucc_context_progress(ctx));
        }
        ucc_collective_finalize(request);

        return sizeof(size_t);
    }

    void close(bool close_wr=true, bool close_rd=true) {
        // Non-root process can issue the explicit close to the root process and
        // go on. At finalize it will flush the pending operations.
        if(!root) {

            /* BROADCAST EOS */
            close_args.mask              = 0;
            close_args.coll_type         = UCC_COLL_TYPE_GATHER;
            close_args.src.info.buffer   = &EOS;
            close_args.src.info.count    = 1;
            close_args.src.info.datatype = UCC_DT_UINT64;
            close_args.src.info.mem_type = UCC_MEMORY_TYPE_HOST;
            close_args.root              = root_rank;

            UCC_CHECK(ucc_collective_init(&close_args, &req, team)); 
            UCC_CHECK(ucc_collective_post(req));  

            closing = true;
            return;
        }
    }

    void finalize(bool, std::string name="") {
        if(!root) {
            // The user didn't call the close explicitly
            if(!closing) {
                this->close(true, true);
            }
            while (UCC_INPROGRESS == ucc_collective_test(req)) { 
                UCC_CHECK(ucc_context_progress(ctx));
            }
            ucc_collective_finalize(req);
        }
        else {
            while(!closing) {
                size_t sz = 1;
                this->probe(sz, true);
                if(sz == 0) break;
				MTCL_ERROR("[internal]:\t", "Spurious message received of size %ld on handle with name %s!\n", sz, name.c_str());

                char* data = new char[sz];
                this->receive(data, sz);
                delete[] data;
            }
        }

        // ucc_context_destroy(ctx);
        delete[] probe_data;
    }

};

#endif //UCCCOLLIMPL_HPP
