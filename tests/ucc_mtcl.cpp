/*
 * Basic example using MTCL handles to perform allgather handshake between two
 * peers in order to allow UCC library to create a context/team
 * and perform collective operations.
 *
 *
 * Compile with:
 *  $> mpicxx --std=c++17 ucc_mtcl.cpp -g -o ucc_mtcl -I .. -I/home/federico/includes/ucc/src/ -L/home/federico/install/lib -lucc -Wl,-rpath="/home/federico/install/lib" -DENABLE_UCX -lucp -lucs -luct
 * 
 * Run with:
 *  $> mpirun -n 1 ./ucc_mtcl 0 server : -n 1 ./ucc_mtcl 0 client
 * 
 * 
 * */

#include <iostream>
#include <string>
#include <vector>

#include <mpi.h>
#include <ucc/api/ucc.h>
#include "mtcl.hpp"


typedef struct my_info {
    std::vector<HandleUser*>* handles;    // Vector of handles of participants
    int rank;               // Local rank
    int size;               // Team size
    int complete;           // Maybe unnecessary
} my_info_t;


#define STR(x) #x
#define UCC_CHECK(_call)                                            \
    if (UCC_OK != (_call)) {                                        \
        fprintf(stderr, "*** UCC TEST FAIL: %s\n", STR(_call));     \
        MPI_Abort(MPI_COMM_WORLD, -1);                              \
    }

static ucc_status_t oob_allgather(void *sbuf, void *rbuf, size_t msglen,
                                  void *coll_info, void **req)
{
    MPI_Request request;

    my_info_t* info = (my_info_t*)coll_info;
    printf("local rank: %d - team size: %d - msglen: %ld\n", info->rank, info->size, msglen);

    auto handle = info->handles;

    for(int i = 0; i < info->size-1; i++) {
        handle->at(i)->send(&info->rank, sizeof(int));
        handle->at(i)->send(sbuf, msglen);
        int remote_rank;
        handle->at(i)->receive(&remote_rank, sizeof(int));
        handle->at(i)->receive((char*)rbuf+(remote_rank*msglen), msglen);
    }

    memcpy((char*)rbuf+(info->rank*msglen), sbuf, msglen);

    return UCC_OK;
}

static ucc_status_t oob_allgather_test(void *req)
{
    return UCC_OK;
}

static ucc_status_t oob_allgather_free(void *req)
{
    return UCC_OK;
}

/* Creates UCC team for a group of processes represented by MPI
   communicator. UCC API provides different ways to create a team,
   one of them is to use out-of-band (OOB) allgather provided by
   the calling runtime. */
static ucc_team_h create_ucc_team(my_info_t* info, ucc_context_h ctx)
{
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
        MPI_Abort(MPI_COMM_WORLD, status);
    }
    return team;
}

int main (int argc, char **argv) {

    if(argc < 2) {
        printf("Usage: %s <appName>\n", argv[0]);
        return 1;
    }


    ucc_lib_config_h     lib_config;
    ucc_context_config_h ctx_config;
    int                  rank, size, i;
    ucc_team_h           team;
    ucc_context_h        ctx;
    ucc_lib_h            lib;
    size_t               msglen;
    size_t               count;
    int                 *sbuf, *rbuf;
    ucc_coll_req_h       req;
    ucc_coll_args_t      args;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    Manager::init(argv[0]);

    std::vector<HandleUser*> handles;

    my_info_t* info = new my_info_t();
    info->handles = &handles;
    info->rank = rank;
    info->size = size;
    info->complete = 0;

    printf("Info rank: %d - size: %d\n", info->rank, info->size);

    if(rank == 0) {
        Manager::listen("UCX:0.0.0.0:42000");
        MPI_Barrier(MPI_COMM_WORLD);

        int count = 0;
        while(count < size-1) {
            auto h = new HandleUser(std::move(Manager::getNext()));
            size_t sz;
            if(h->probe(sz) <= 0) {
                MTCL_PRINT(100, "[Server]:\t", "Unexpected probe status\n");
                return 1;
            }
            char* buff = new char[sz+1];
            if(h->receive(buff, sz) <= 0) {
                MTCL_PRINT(100, "[Server]:\t", "Unexpected receive status\n");
                return 1;
            }
            buff[sz] = '\0';
            std::cout << "Client sent hello message: " << buff << std::endl;
            delete[] buff;

            handles.push_back(h);
            count++;
        }
    }
    else if(rank == 1) {
        MPI_Barrier(MPI_COMM_WORLD);
        auto h = new HandleUser(std::move(Manager::connect("UCX:0.0.0.0:42000")));

        std::string hello{"Hello server!"};
        h->send(hello.c_str(), hello.length());

        handles.push_back(h);
    }

    /* Init ucc library */
    ucc_lib_params_t lib_params = {
        .mask        = UCC_LIB_PARAM_FIELD_THREAD_MODE,
        .thread_mode = UCC_THREAD_SINGLE
    };
    UCC_CHECK(ucc_lib_config_read(NULL, NULL, &lib_config));
    UCC_CHECK(ucc_init(&lib_params, lib_config, &lib));
    ucc_lib_config_release(lib_config);

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

    count = 2;
    msglen = count * sizeof(int);

    sbuf = (int*)malloc(msglen);
    rbuf = (int*)malloc(msglen);    
    for (i = 0; i < count; i++) {
        sbuf[i] = rank + 1;
        rbuf[i] = 0;
    }
    
    args.mask              = 0;
    args.coll_type         = UCC_COLL_TYPE_ALLREDUCE;
    args.src.info.buffer   = sbuf;
    args.src.info.count    = count;
    args.src.info.datatype = UCC_DT_INT32;
    args.src.info.mem_type = UCC_MEMORY_TYPE_HOST;
    args.dst.info.buffer   = rbuf;
    args.dst.info.count    = count;
    args.dst.info.datatype = UCC_DT_INT32;
    args.dst.info.mem_type = UCC_MEMORY_TYPE_HOST;
    args.op                = UCC_OP_SUM;

    UCC_CHECK(ucc_collective_init(&args, &req, team));
    UCC_CHECK(ucc_collective_post(req));    
    while (UCC_INPROGRESS == ucc_collective_test(req)) {
        UCC_CHECK(ucc_context_progress(ctx));
    }
    ucc_collective_finalize(req);

    /* Check result */
    int sum = ((size + 1) * size) / 2;
    for (i = 0; i < count; i++) {
        printf("rbuf[%d]: %d\n", i, rbuf[i]);
        if (rbuf[i] != sum) {
            printf("ERROR at rank %d, pos %d, value %d, expected %d\n", rank, i, rbuf[i], sum);
            break;
        }
    }

    /* Cleanup UCC */
    UCC_CHECK(ucc_team_destroy(team));
    UCC_CHECK(ucc_context_destroy(ctx));
    UCC_CHECK(ucc_finalize(lib));

    MPI_Finalize();
    Manager::finalize();

    free(sbuf);
    free(rbuf);
    return 0;
}
