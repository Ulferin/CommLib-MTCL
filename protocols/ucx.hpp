#ifndef UCX_HPP
#define UCX_HPP

#include <iostream>
#include <map>
#include <string.h>
#include <shared_mutex>

#include <unistd.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <ucp/api/ucp.h>

#include "../handle.hpp"
#include "../protocolInterface.hpp"
#include "../utils.hpp"     //TODO:<----- rimuovi
#include "../config.hpp"    //TODO:<----- rimuovi


class HandleUCX : public Handle {

    typedef struct test_req {
        std::atomic<int> complete;
    } test_req_t;


    static void common_cb(void *user_data, const char *type_str) {
        test_req_t *ctx;
        if (user_data == NULL) {
            MTCL_UCX_ERROR("HandleUCX::common_cb user_data passed to %s mustn't be NULL\n", type_str);
            return;
        }

        ctx           = (test_req_t*)user_data;
        ctx->complete = 1;
    }

    /**
     * The callback on the sending side, which is invoked after finishing sending
     * the message.
     */
    static void send_cb(void *request, ucs_status_t status, void *user_data) {
        common_cb(user_data, "send_cb");
    }

    /**
     * The callback on the receiving side, which is invoked upon receiving the
     * stream message.
     */
    static void stream_recv_cb(void *request, ucs_status_t status, size_t length,
                            void *user_data) {
        common_cb(user_data, "stream_recv_cb");
    }


protected:

    void fill_request_param(test_req_t* ctx, ucp_request_param_t* param, bool is_send) {
        ctx->complete = 0;
        param->op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK |
                              UCP_OP_ATTR_FIELD_DATATYPE |
                              UCP_OP_ATTR_FIELD_USER_DATA;
        param->datatype     = is_send ? UCP_DATATYPE_IOV : ucp_dt_make_contig(1);
        param->user_data    = ctx;
    }

    int request_wait(void* request, test_req_t* ctx, char* operation) {
        ucs_status_t status = UCS_OK;

        // Operation completed immediately, callback is not called!
        if(request == NULL) {
            return 0;
        }

        if(UCS_PTR_IS_ERR(request)) {
            MTCL_UCX_PRINT(100, "HandleUCX::request_wait UCX_%s request error (%s)\n",
                operation, ucs_status_string(status));
            errno = ECOMM;
            ucp_request_free(request);
            return -1;
        }

        while(ctx->complete == 0) {
            ucp_worker_progress(ucp_worker);
        }
        status = ucp_request_check_status(request);
        ucp_request_free(request);
    
        if(status != UCS_OK) {
            MTCL_UCX_PRINT(100, "HandleUCX::request_wait UCX_%s status error (%s)\n",
                operation, ucs_status_string(status));
            errno = ECOMM;
            return -1;
        }

        return 0;
    }

    ssize_t receive_internal(void* buff, size_t size, bool blocking) {
        ucp_request_param_t param;
        test_req_t* request;
        test_req_t ctx;
        size_t res;

        fill_request_param(&ctx, &param, false);
        param.op_attr_mask |= UCP_OP_ATTR_FIELD_FLAGS;
        param.flags = blocking ? UCP_STREAM_RECV_FLAG_WAITALL : 0;
        param.cb.recv_stream = stream_recv_cb;
        request              = (test_req_t*)ucp_stream_recv_nbx(endpoint, buff, size,
                                                   &res, &param);

        /*NOTE: we do not check how many bytes we actually wrote in the buffer*/
        if(!blocking) {
            if(request == NULL && res == 0) {
                errno = EWOULDBLOCK;
                return -1;
            }
        }

        if(request_wait(request, &ctx, (char*)"receive_internal") != 0)
            return -1;

        return size;
    }


public:
    std::atomic<bool> already_closed {false};
    std::atomic<bool> eos_received {false};
    ucp_ep_h endpoint;
    ucp_worker_h ucp_worker;

    std::atomic<bool> closed_wr{false};
    std::atomic<bool> closed_rd{false};

    ssize_t last_probe = -1;

    HandleUCX(ConnType* parent, ucp_ep_h endpoint, ucp_worker_h worker) : Handle(parent), endpoint(endpoint), ucp_worker(worker) {}

    ssize_t sendEOS() {
        size_t sz = 0;

        ucp_dt_iov_t iov[1];
        iov[0].buffer = &sz;
        iov[0].length = sizeof(size_t);

        ucp_request_param_t param;
        test_req_t* request;
        test_req_t ctx;

        fill_request_param(&ctx, &param, true);
        param.cb.send = send_cb;
        request       = (test_req_t*)ucp_stream_send_nbx(endpoint, iov, 1, &param);

        if(request_wait(request, &ctx, (char*)"sendEOS") != 0)
            return -1;

        return sz;
    }

    ssize_t send(const void* buff, size_t size) {
        size_t sz = htobe64(size);
        
        ucp_dt_iov_t iov[2];
        iov[0].buffer = &sz;
        iov[0].length = sizeof(sz);
        iov[1].buffer = const_cast<void*>(buff);
        iov[1].length = size;

        ucp_request_param_t param;
        test_req_t* request;
        test_req_t ctx;

        fill_request_param(&ctx, &param, true);
        param.cb.send = send_cb;
        request       = (test_req_t*)ucp_stream_send_nbx(endpoint, iov, 2, &param);

        if(request_wait(request, &ctx, (char*)"send") != 0)
            return -1;

        return size;
    }

    ssize_t receive(void* buff, size_t size) {
        ssize_t res = receive_internal(buff, size, true);
        // Last recorded probe was consumed, reset probe size
        last_probe = -1;
        return res;
    }

    ssize_t probe(size_t& size, const bool blocking=true) {
        if(last_probe != -1) {
            size = last_probe;
            return sizeof(size_t);
        }

        size_t sz;

        if(receive_internal(&sz, sizeof(size_t), blocking) <= 0)
            return -1;

        size = be64toh(sz);
        if(size == 0) {
            MTCL_UCX_PRINT(100, "EOS received by internal probe\n");
            eos_received = true;
        }

        last_probe = size;
        return sizeof(size_t);
    }

    ~HandleUCX() {
        
    }

};

class ConnUCX : public ConnType {

protected:

    /* OOB-related */
    std::string address;
    int         port;
    fd_set      set, tmpset;
    int         listen_sck;

#if defined(SINGLE_IO_THREAD)
        int fdmax;
#else	
    std::atomic<int> fdmax;
    std::shared_mutex shm;
#endif

    /* UCX-related */
    ucp_address_t*  local_addr;
    size_t          local_addr_len;
    ucp_worker_h    ucp_worker;
    ucp_context_h   ucp_context;

    // UCX endpoint object --> <handle, to_manage>
    std::map<ucp_ep_h, std::pair<HandleUCX*, bool>> connections;

private:

    int _init() {
        if ((listen_sck=socket(AF_INET, SOCK_STREAM, 0)) < 0){
			MTCL_UCX_PRINT(100, "ConnUCX::_init socket errno=%d\n", errno);
            return -1;
        }

        int enable = 1;
        // enable the reuse of the address
        if (setsockopt(listen_sck, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
			MTCL_UCX_PRINT(100, "ConnUCX::_init setsockopt errno=%d\n", errno);
            return -1;
        }

		struct addrinfo hints;
		struct addrinfo *result, *rp;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family   = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
		hints.ai_socktype = SOCK_STREAM;  /* Stream socket */
		hints.ai_flags    = AI_PASSIVE;
		hints.ai_protocol = IPPROTO_TCP;  /* Allow only TCP */
		if (getaddrinfo(address.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
			MTCL_UCX_PRINT(100, "ConnUCX::_init getaddrinfo errno=%d\n", errno);		
			return -1;
		}

		bool ok = false;
		for (rp = result; rp != NULL; rp = rp->ai_next) {
			if (bind(listen_sck, rp->ai_addr, (int)rp->ai_addrlen) < 0){
				MTCL_UCX_PRINT(100, "ConnUCX::_init bind errno=%d, continue\n", errno);
				continue;
			}
			ok = true;
			break;
		}
		free(result);
		if (!ok) {
			MTCL_UCX_PRINT(100, "ConnUCX::_init bind loop exit with errno=%d\n", errno);
			return -1;
		}
        if (::listen(listen_sck, UCX_BACKLOG) < 0){
			MTCL_UCX_PRINT(100, "ConnUCX::_init listen errno=%d\n", errno);
            return -1;
        }

        return 0;
    }


    int exchange_address(int oob_sock, ucp_address_t* local_addr, size_t local_addr_len, ucp_address_t** peer_addr, size_t* peer_addr_len) {
        int ret;
        uint64_t addr_len = 0;

        ret = send(oob_sock, &local_addr_len, sizeof(local_addr_len), 0);
        if(ret != (int)sizeof(local_addr_len)) {
            MTCL_UCX_ERROR("ConnUCX::exchange_address OOB error sending address len\n");
            return 1;
        }

        ret = send(oob_sock, local_addr, local_addr_len, 0);
        if(ret != (int)local_addr_len) {
            MTCL_UCX_ERROR("ConnUCX::exchange_address OOB error sending address\n");
            return 1;
        }

        ret = recv(oob_sock, &addr_len, sizeof(addr_len), MSG_WAITALL);
        if(ret != (int)sizeof(addr_len)) {
            MTCL_UCX_ERROR("ConnUCX::exchange_address OOB error receiving address len\n");
            return 1;
        }

        *peer_addr_len = addr_len;
        *peer_addr = (ucp_address_t*)malloc(*peer_addr_len);
        if(!*peer_addr) {
            MTCL_UCX_ERROR("ConnUCX::exchange_address OOB error allocating address\n");
            return 1;
        }

        ret = recv(oob_sock, *peer_addr, *peer_addr_len, MSG_WAITALL);
        if(ret != (int)*peer_addr_len) {
            MTCL_UCX_ERROR("ConnUCX::exchange_address OOB error receiving address\n");
            return 1;
        }

        return 0;
    }


    void ep_close(ucp_ep_h ep) {
        // ucp_request_param_t param;
        ucs_status_t status;
        void *close_req;

        // param.op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS;
        // param.flags        = UCP_EP_CLOSE_MODE_FLUSH;
        close_req          = ucp_ep_close_nb(ep, UCP_EP_CLOSE_MODE_FLUSH);

        if (close_req == NULL) {
            return;
        } else if (UCS_PTR_IS_ERR(close_req)) {
            MTCL_UCX_ERROR("ConnUCX::ep_close failed with status [%s]\n", ucs_status_string(UCS_PTR_STATUS(close_req)));
            return;
        }

        if (UCS_PTR_IS_PTR(close_req)) {
            do {
                ucp_worker_progress(ucp_worker);
                status = ucp_request_check_status(close_req);
            } while (status == UCS_INPROGRESS);
            ucp_request_free(close_req);
        } else {
            status = UCS_PTR_STATUS(close_req);
        }

        if (status != UCS_OK) {
            MTCL_UCX_PRINT(100, "ConnUCX::ep_close failed to close ep %p. [%s]\n", (void*)ep, ucs_status_string(status));
        }
    }


public:

    int init(std::string) {
        /* OOB init */
        FD_ZERO(&set);
        FD_ZERO(&tmpset);
        listen_sck = -1;
        fdmax = -1;

        /* UCX init */
        ucs_status_t        ep_status = UCS_OK;
        ucp_config_t*       config;
        ucp_params_t        ucp_params;
        ucp_worker_params_t worker_params;

        memset(&ucp_params, 0, sizeof(ucp_params));
        memset(&worker_params, 0, sizeof(worker_params));


        // Reads environment configuration
        ep_status = ucp_config_read(NULL, NULL, &config);
        if(ep_status != UCS_OK) {
            MTCL_UCX_PRINT(100, "ConnUCX::init error reading config\n");
            errno = EINVAL;
            return -1;
        }

        /*NOTE: qui si possono richiedere più modalità per lo stesso contesto,
                in particolare si può richiedere la compatibilità sia con TAG
                che STREAM. Potrebbe essere interessante usarli entrambi in modo
                da ricevere gli header su TAG e i dati su stream*/
        /* UCX context initialization */
        ucp_params.field_mask   = UCP_PARAM_FIELD_FEATURES;
        ucp_params.features     = UCP_FEATURE_STREAM;

        // Initialize context with requested features and parameters
        ep_status = ucp_init(&ucp_params, config, &ucp_context);
        if(ep_status != UCS_OK) {
            MTCL_UCX_PRINT(100, "ConnUCX::init error initializing context\n");
            errno = EINVAL;
            return -1;
        }
#ifdef UCX_DEBUG
        MTCL_UCX_PRINT(1, "\n\n=== Environment configuration ===\n");
        ucp_config_print(config, stdout, NULL, UCS_CONFIG_PRINT_CONFIG);
        MTCL_UCX_PRINT(1, "\n\n=== Context info ===\n");
        ucp_context_print_info(ucp_context, stderr);
#endif
        ucp_config_release(config);


        /* UCX worker initialization */
        worker_params.field_mask    = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
        worker_params.thread_mode   = UCS_THREAD_MODE_MULTI;

        // Initialize worker
        ep_status = ucp_worker_create(ucp_context, &worker_params, &ucp_worker);
        if(ep_status != UCS_OK) {
            MTCL_UCX_PRINT(100, "ConnUCX::init error initializing worker\n");
            errno = EINVAL;
            return -1;
        }

        // Retrieve local address of the current worker, to be exchanged upon
        // handshake with the OOB connections
        ep_status = ucp_worker_get_address(ucp_worker, &local_addr, &local_addr_len);
        if(ep_status != UCS_OK) {
            MTCL_UCX_PRINT(100, "ConnUCX::init error retrieving worker address\n");
            errno = EINVAL;
            return -1;
        }

        return 0;
    }


    int listen(std::string s) {
        /*TODO: inserire controllo stringa malformata in listen*/
        address = s.substr(0, s.find(":"));
        port    = stoi(s.substr(address.length()+1));

        if(this->_init()) {
            MTCL_UCX_PRINT(100, "ConnUCX::listen error initializing OOB socket\n");
            errno = EINVAL;
            return -1;
        } 
        
        MTCL_UCX_PRINT(1, "ConnUCX::listen OOB socket listening on: %s:%d\n", address.c_str(), port);

        FD_SET(listen_sck, &set);
        fdmax = listen_sck;

        return 0;
    }


    Handle* connect(const std::string& address) {
        /*TODO: inserire controllo stringa connect malformata*/

        const std::string host = address.substr(0, address.find(":"));
		const std::string svc  = address.substr(host.length()+1);

        int fd;
        int res;

        struct addrinfo hints;
        struct addrinfo *result, *rp;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_UNSPEC;              /* Allow IPv4 or IPv6 */
        hints.ai_socktype = SOCK_STREAM;            /* Stream socket */
        hints.ai_flags    = 0;
        hints.ai_protocol = IPPROTO_TCP;            /* Allow only TCP */

        // resolve the address (assumo stringa formattata come host:port)
        if (getaddrinfo(host.c_str(), svc.c_str(), &hints, &result) != 0) {
			MTCL_UCX_PRINT(100, "ConnUCX::connect  getaddrinfo errno=%d\n", errno);
            return nullptr;
		}

        // try to connect to a possible one of the resolution results
        for (rp = result; rp != NULL; rp = rp->ai_next) {
            fd = socket(rp->ai_family, rp->ai_socktype,
                            rp->ai_protocol);
            if (fd == -1) {
				MTCL_UCX_PRINT(100, "ConnUCX::connect socket errno=%d\n", errno);
                continue;
			}

            if (::connect(fd, rp->ai_addr, rp->ai_addrlen) != -1)
                break;                  /* Success */

            close(fd);
        }
        free(result);
            
        if (rp == NULL)            /* No address succeeded */
            return nullptr;

        // To store address info of the remote peer
        ucp_address_t* peer_addr;
        size_t peer_addr_len;
        res = exchange_address(fd, local_addr, local_addr_len, &peer_addr, &peer_addr_len);
        if(res != 0) {
            MTCL_UCX_PRINT(100, "ConnUCX::connect error exchanging address, handle is invalid\n");
            return nullptr;
        }

        // Create endpoint with info received from OOB socket
        ucp_ep_h server_ep;
        ucp_ep_params_t ep_params;
        ep_params.field_mask    = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS; // |
                                // UCP_EP_PARAM_FIELD_USER_DATA;
        ep_params.address       = peer_addr;
        //ep_params.user_data = &ep_status;

        ucs_status_t ep_status = UCS_OK;
        ep_status = ucp_ep_create(ucp_worker, &ep_params, &server_ep);
        if(ep_status != UCS_OK) {
            MTCL_UCX_PRINT(100, "ConnUCX::connect error creating the endpoint\n");
            errno = EINVAL;
            return nullptr;
        }
        free(peer_addr);

#ifdef UCX_DEBUG
        MTCL_UCX_PRINT(1, "\n\n=== Endpoint info ===\n");
        ucp_ep_print_info(server_ep, stderr);
#endif


        HandleUCX *handle = new HandleUCX(this, server_ep, ucp_worker);
        {
			REMOVE_CODE_IF(std::unique_lock lock(shm));
			connections.insert({server_ep, {handle, false}});
		}

        return handle;
    }


    void update() {
        REMOVE_CODE_IF(std::unique_lock ulock(shm, std::defer_lock));

        REMOVE_CODE_IF(ulock.lock());
        tmpset = set;
        REMOVE_CODE_IF(ulock.unlock());

        struct timeval wait_time = {.tv_sec = 0, .tv_usec=UCX_POLL_TIMEOUT};
        int nready = 0;
        
        // Only if we are listening for new connections
        if(fdmax != -1) {
            switch (nready=select(fdmax+1, &tmpset, NULL, NULL, &wait_time)) {
            case -1: {
                if(errno == EBADF) {
                    MTCL_UCX_PRINT(100, "ConnUCX::update select ERROR: errno=EBADF\n");
                    return;
                }
                MTCL_UCX_ERROR("ConnUCX::update select ERROR: errno=%d -- %s\n", errno, strerror(errno));
            }
            case 0:
                break;
            }

            // Only for new connections on the OOB socket
            if(FD_ISSET(this->listen_sck, &tmpset)) {
                int connfd = accept(this->listen_sck, (struct sockaddr*)NULL ,NULL);
                if (connfd == -1){
                    MTCL_UCX_ERROR("ConnUCX::update accept ERROR: errno=%d -- %s\n", errno, strerror(errno));
                    return;
                }

                ucp_address_t* peer_addr;
                size_t peer_addr_len;
                int res = exchange_address(connfd, local_addr, local_addr_len, &peer_addr, &peer_addr_len);
                if(res != 0) {
                    MTCL_UCX_PRINT(100, "ConnUCX::update error exchanging address\n");
                    return;
                }

                // We only use connfd for the OOB handshake
                close(connfd);

                ucs_status_t ep_status   = UCS_OK;
                ucp_ep_h client_ep;
                ucp_ep_params_t ep_params;
                ep_params.field_mask      = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS |
                                            UCP_EP_PARAM_FIELD_USER_DATA;
                ep_params.address         = peer_addr;
                ep_params.user_data       = &ep_status;

                ep_status = ucp_ep_create(ucp_worker, &ep_params, &client_ep);
                if(ep_status != UCS_OK) {
                    MTCL_UCX_PRINT(100, "ConnUCX::update error creating the endpoint\n");
                    return;
                }
                free(peer_addr);

                HandleUCX* handle = new HandleUCX(this, client_ep, ucp_worker);

                REMOVE_CODE_IF(ulock.lock());
                connections.insert({client_ep, {handle, false}});
                addinQ(true, handle);
                REMOVE_CODE_IF(ulock.unlock());                    
            }
        }

        // Poll on managed endpoints to detect something is ready to be read
        // This will cause, at the same time, progress on the user handles
        size_t size = -1;
        size_t max_eps = connections.size();
        
        // One-shot progress
        int prog = -1;
        prog = ucp_worker_progress(ucp_worker);
        (void)prog;

        ucp_stream_poll_ep_t* ready_eps = new ucp_stream_poll_ep_t[max_eps];
        size = ucp_stream_worker_poll(ucp_worker, ready_eps, max_eps, 0);
        if(size < 0) {
            MTCL_UCX_PRINT(100, "ConnUCX::update error in ucp_stream_worker_poll\n");
            return;
        }

        REMOVE_CODE_IF(ulock.lock());
        // Some of the endpoints are ready, we need to check if we are managing that ep
        for(size_t i=0; i<size; i++) {
            ucp_stream_poll_ep_t ep = ready_eps[i];

            auto it = connections.find(ep.ep);
            if(it != connections.end()) {
                auto handlePair = it->second;
                if(handlePair.second) {
                    handlePair.second = false;
                    addinQ(false, handlePair.first);
                }
            }
        }
        REMOVE_CODE_IF(ulock.unlock());

        delete[] ready_eps;
        return;
    }
    

    void notify_yield(Handle* h) {

        HandleUCX* handle = reinterpret_cast<HandleUCX*>(h);
        if(handle->isClosed()) return;

        // Check if handle still has some data to receive, addinQ in case we
        // have data
        size_t size;
        if(handle->probe(size, false) > 0) {
            REMOVE_CODE_IF(std::unique_lock l(shm));
            addinQ(false, handle);
            return;
        }

        REMOVE_CODE_IF(std::unique_lock l(shm));
        auto it = connections.find(handle->endpoint);
        if(it == connections.end())
            MTCL_UCX_ERROR("Couldn't yield handle\n");
        connections[handle->endpoint].second = true;

        return;
    }

    void notify_close(Handle* h, bool close_wr=true, bool close_rd=true) {
        HandleUCX* handle = reinterpret_cast<HandleUCX*>(h);

        if(handle->already_closed) return;

        if(close_wr && close_rd) {
            connections.erase(handle->endpoint);
            handle->already_closed = true;

            if(!handle->eos_received) {
                size_t sz = 1;
                while(true) {
                    handle->probe(sz, sizeof(size_t));
                    if(sz == 0) break;
                    char* buff = new char[sz];
                    handle->receive(buff, sz);
                    delete[] buff;
                }
            }
            ep_close(handle->endpoint);
        }

        return;
    }

    void end() {
        auto modified_connections = connections;
        for(auto& [_, handlePair] : modified_connections)
                setAsClosed(handlePair.first);
        
        ucp_worker_release_address(ucp_worker, local_addr);
        ucp_worker_destroy(ucp_worker);
        ucp_cleanup(ucp_context);
    }

};

#endif //UCX_HPP