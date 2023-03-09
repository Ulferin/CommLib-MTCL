/*
 *
 * Iterative benchmark testing MTCL collectives implementation. This test is
 * focused on the Broadcast and Gather collectives. The former allows the Emitter
 * to send data to each of the workers, the latter allows the Collector to retrieve
 * partial results. The final result is then shipped via a P2P feedback channel
 * by the Collector to the Emitter. Each communication channel is tied to the
 * enabled protocol at compilation time.
 * 
 * The configuration file, if not provided, is automatically generated
 * by the application itself upon execution of the Emitter node to run on a single
 * machine.
 * 
 * The application structure is as follows:
 * 
 *       ______________________P2P handle________________________________
 *      |                                                                |
 *      v             | --> Worker1 (App2) --> |                         |
 * Emitter --BCast--> |        ...             | --Gather--> Collector --
 *                    | --> WorkerN (App3) --> |                         
 *      
 *
 * Compile with:
 *  $> TPROTOCOL=TCP||UCX||MPI [UCX_HOME="<ucx_path>" UCC_HOME="<ucc_path>"] RAPIDJSON_HOME="<rapidjson_path>" make clean iterative_benchmark
 * 
 * Execute with:
 *  $> ./iterative_benchmark <id> <num_workers> <num_elements> <iterations> [configuration_file_path]
 * 
 * Parameters are:
 *   - id: specify which part of the application to run.
 *          (Emitter id: 0, Collector id: 1, Worker id: [2,num_workers+1))
 *   - num_workers: specify how many workers are to be launched
 *   - num_elements: total amount of elements the Emitter will generate
 * 
 * 
 * Execution example with 2 workers, 10 elements and 5 iterations
 *  $> ./iterative_benchmark 0 2 10 5
 *  $> ./iterative_benchmark 1 2 10 5
 *  $> ./iterative_benchmark 2 2 10 5
 *  $> ./iterative_benchmark 3 2 10 5
 * 
 *  $> mpirun -x MTCL_VERBOSE=all
 *       -n 1 ./iterative_benchmark 0 2 10 5 iterative_bench.json : \
 *       -n 1 ./iterative_benchmark 1 2 10 5 iterative_bench.json : \ 
 *       -n 1 ./iterative_benchmark 2 2 10 5 iterative_bench.json : \ 
 *       -n 1 ./iterative_benchmark 3 2 10 5 iterative_bench.json
 * 
 * 
 */

#include <fstream>
#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/writer.h>

#include <iostream>
#include "mtcl.hpp"

inline static int EMITTER_RANK{0};
inline static int WORKER_RANK{2};
inline static int COLLECTOR_RANK{1};

inline static std::map<int, std::string> participants {
    {0,"Emitter"},
    {1,"Collector"},
    {2,"Worker"}
};

/**
 * @brief Generates configuration file required for this application with the
 * specified number of workers \b num_workers. Based on the enabled protocol,
 * the correct endpoint and protocol strings are generated automatically.
 * 
 * @param num_workers total number of workers
 */
void generate_configuration(int num_workers) {
    std::string PROTOCOL{};
    std::string EMITTER_ENDPOINT{};
    std::string COLLECTOR_ENDPOINT{};

//NOTE: questi indirizzi potrebbero diventare dei parametri come per p2p-perf
#ifdef ENABLE_TCP
    PROTOCOL = {"TCP"};
    EMITTER_ENDPOINT = {"TCP:0.0.0.0:42000"};
    COLLECTOR_ENDPOINT = {"TCP:0.0.0.0:42001"};
#endif

#ifdef ENABLE_MPI
    PROTOCOL = {"MPI"};
    EMITTER_ENDPOINT = {"MPI:0:10"};
    COLLECTOR_ENDPOINT = {"MPI:1:10"};
#endif

#ifdef ENABLE_UCX
    PROTOCOL = {"UCX"};
    EMITTER_ENDPOINT = {"UCX:0.0.0.0:42000"};
    COLLECTOR_ENDPOINT = {"UCX:0.0.0.0:42001"};
#endif

    rapidjson::Value s;
    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Value components;
    components.SetArray();
    rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
    
    rapidjson::Value protocols;
    protocols.SetArray();
    s.SetString(PROTOCOL.c_str(), PROTOCOL.length(), allocator); 
    protocols.PushBack(s, allocator);

    rapidjson::Value listen_endp_em;
    listen_endp_em.SetArray();
    s.SetString(EMITTER_ENDPOINT.c_str(), EMITTER_ENDPOINT.length(), allocator); 
    listen_endp_em.PushBack(s, allocator);
    rapidjson::Value listen_endp_coll;
    listen_endp_coll.SetArray();
    s.SetString(COLLECTOR_ENDPOINT.c_str(), COLLECTOR_ENDPOINT.length(), allocator); 
    listen_endp_coll.PushBack(s, allocator);
    
    
    rapidjson::Value emitter;
    emitter.SetObject();
    emitter.AddMember("name", "Emitter", allocator);
    emitter.AddMember("host", "localhost", allocator);
    emitter.AddMember("protocols", protocols, allocator);
    emitter.AddMember("listen-endpoints", listen_endp_em, allocator);

    rapidjson::Value collector;
    collector.SetObject();
    collector.AddMember("name", "Collector", allocator);
    collector.AddMember("host", "localhost", allocator);
    protocols.SetArray();
    s.SetString(PROTOCOL.c_str(), PROTOCOL.length(), allocator); 
    protocols.PushBack(s, allocator);
    collector.AddMember("protocols", protocols, allocator);
    collector.AddMember("listen-endpoints", listen_endp_coll, allocator);


    components.PushBack(emitter, allocator);

    for(int i=1; i<=num_workers; i++) {
        std::string worker_i{participants.at(WORKER_RANK) + std::to_string(i)};
        
        rapidjson::Value worker;
        worker.SetObject();
        worker.AddMember("host", "localhost", allocator);
        protocols.SetArray();
        s.SetString(PROTOCOL.c_str(), PROTOCOL.length(), allocator); 
        protocols.PushBack(s, allocator);
        worker.AddMember("protocols", protocols, allocator);
        s.SetString(worker_i.c_str(), worker_i.length(), allocator); 
        worker.AddMember("name", s, allocator);
        
        components.PushBack(worker, allocator);
    }
    components.PushBack(collector, allocator);

    doc.AddMember("components", components, allocator);
    std::ofstream ofs("iterative_bench_auto.json");
    rapidjson::OStreamWrapper osw(ofs);

    rapidjson::Writer<rapidjson::OStreamWrapper> writer(osw);
    doc.Accept(writer);
}

void Emitter(const std::string& bcast, const std::string& broot) {

	// Waiting for Collector
	auto fbk = Manager::getNext();
	fbk.yield();        
	
	auto hg = Manager::createTeam(bcast, broot, BROADCAST);
	if(hg.isValid() && fbk.isValid())
		printf("Emitter starting\n");
	else {
		abort();
	}

	const int msgsize = 100;
	char data[msgsize];
	
	
	for(int i=0; i< 10; ++i) {
		hg.sendrecv(data, msgsize, nullptr, 0);		   			
		// Receive result from feedback channel
		auto h = Manager::getNext();
		int res;
		if (h.receive(&res, sizeof(int)) <= 0) {
			assert(1==0);
		}
		if (res != 1) {
			assert(1==0);
		}
		fprintf(stderr, "EMITTER, RECEIVED FROM COLLECTOR\n");
	}
	hg.close();
	auto h = Manager::getNext();	
	h.close();
}

void Worker(const std::string& bcast, const std::string& gather,
			const std::string& broot, const std::string& groot, int rank) {
	const int msgsize = 100;
	char data[msgsize];

	fprintf(stderr, "bcast=%s, gather=%s, broot=%s, groot=%s, rank=%d\n",
			bcast.c_str(), 			gather.c_str(), 			broot.c_str(), 			groot.c_str(), rank);
	
	
	auto hg_bcast = Manager::createTeam(bcast, broot, BROADCAST);	
	auto hg_gather= Manager::createTeam(gather, groot,GATHER);
	
	
	if(hg_bcast.isValid() && hg_gather.isValid())
		printf("WORKER Correctly created teams\n");
	else {
		fprintf(stderr, "INVALID COLLECTIVES HANDLE\n");
		abort();
	}
	
	for(int i=0; i< 10; ++i) {
		hg_bcast.sendrecv(nullptr, 0, data, msgsize);		   			
		fprintf(stderr, "WORKER BCAST CROSSED\n");
		hg_gather.sendrecv(&rank, sizeof(int), nullptr, 0);
		fprintf(stderr, "WORKER GATHER CROSSED\n");
	}
	fprintf(stderr, "WORKER DONE\n");
	
	hg_bcast.close();        
	hg_gather.close();
	
}

void Collector(const std::string& gather, const std::string& groot, const int nworkers) {

	auto fbk = Manager::connect("Emitter", 100, 200);
	if(!fbk.isValid()) {
		abort();		
	}

	auto hg_gather = Manager::createTeam(gather, groot, GATHER);

	
	int ranks[nworkers+1];
	int rank = 1;
	for(int i=0; i< 10; ++i) {
		hg_gather.sendrecv(&rank, sizeof(int), ranks, sizeof(int));
		fprintf(stderr, "COLLECTOR, GATHER CROSSED\n");
		if (fbk.send(&rank, sizeof(int))<=0) {
			assert(1==0);
		}
	}
	hg_gather.close();
	fbk.close();
}


int main(int argc, char** argv){

    if(argc < 5) {
        printf("Usage: %s <0|1|2> <num workers> <stream len> <iterations> [configuration_file]\n", argv[0]);
        return 1;
    }

    int rank = atoi(argv[1]);
    int num_workers = atoi(argv[2]);
    int streamlen = atoi(argv[3]);
    int count = atoi(argv[4]);
    
    std::string configuration_file{"iterative_bench_auto.json"};
    // A user provided configuration file was specified
    if(argc == 6) {
        configuration_file = {argv[5]};
    }
    else {
        generate_configuration(num_workers);
    }


    // Generating appName based on AppID
    std::string appName{""};
    if(rank >= WORKER_RANK) {
        appName = participants.at(WORKER_RANK) + std::to_string(rank-1);
    }
    else appName = participants.at(rank);


    std::string broadcast_string{participants.at(EMITTER_RANK)};
    std::string worker_string{};
    std::string gather_string{participants.at(COLLECTOR_RANK)};

    for(int i=1; i<=num_workers; i++) {
        std::string worker_i{participants.at(WORKER_RANK) + std::to_string(i)};
        worker_string += (":" + worker_i);
    }
    broadcast_string += worker_string;
    gather_string += worker_string;

	Manager::init(appName, configuration_file);

    // Emitter
    if(rank == 0) {
		Emitter(broadcast_string, participants.at(EMITTER_RANK));
    } else {
		if (rank == 1) {
			Collector(gather_string, participants.at(COLLECTOR_RANK), participants.size()-2);
		} else {	   
			Worker(broadcast_string, gather_string, participants.at(EMITTER_RANK),participants.at(COLLECTOR_RANK), rank-1);
		}
	}
	
    Manager::finalize(true);

    return 0;
}
