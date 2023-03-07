/*
 *
 * Iterative benchmark testing MTCL collectives implementation. This test is
 * focused on the Broadcast and Gather collectives. The former allows the Emitter
 * to send data to each of the workers, the latter allows the Collector to retrieve
 * partial results. The final result is then shipped via a P2P feedback channel
 * by the Collector to the Emitter. Each communication channel is tied to the
 * enabled protocol at compilation time.
 * 
 * The configuration file for each run of the application is automatically generated
 * by the application itself upon execution of the Emitter node.
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
 *  $> mpirun -n 1 ./iterative_benchmark 0 2 10 5 iterative_bench.json : \ 
 *  -n 1 ./iterative_benchmark 1 2 10 5 iterative_bench.json : \ 
 *  -n 1 sh -c 'sleep 1; ./iterative_benchmark 2 2 10 5 iterative_bench.json' : \ 
 *  -n 1 sh -c 'sleep 1; ./iterative_benchmark 3 2 10 5 iterative_bench.json'
 * 
 * */

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

    int expected = (streamlen+1)*streamlen/2;
    int* data = new int[streamlen];

    // Emitter
    if(rank == 0) {
        // Waiting for Collector
        auto fbk = Manager::getNext();
        fbk.yield();        

		auto hg = Manager::createTeam(broadcast_string, participants.at(EMITTER_RANK), BROADCAST);
        if(hg.isValid() && fbk.isValid())
            printf("Emitter starting\n");
        else
            printf("Aborting, connection error (group: %d - p2p: %d)\n", hg.isValid(), fbk.isValid());


        // Generating data
        for(int i = 0; i < streamlen; i++) {
            data[i] = i+1;
        }

        int current = 0;
        int res = 0;
        ssize_t r;
        while(current < count) {
            // Broadcast data
            if(hg.sendrecv(data, streamlen*sizeof(int), nullptr, 0) <= 0) {
                printf("Error sending message\n");
                return 1;
            }

            // Receive result from feedback channel
            auto h = Manager::getNext();
            auto name = h.getName();
            r = h.receive(&res, sizeof(int));
            if(r <= 0) {
                printf("Feedback error\n");
                break;
            }

            if(res == expected)
                printf("[Iteration: %d] Total is: %d, expected was: %d\n", current, res, expected);
            else {
                printf("Failed at iteration [%d]. res is: %d, expected was: %d\n", current, res, expected);
                break;
            }
            current++;
        }
        hg.close();
        fbk.close();
    }
    // Collector
    else if(rank==1) {
        // Feedback channel
        auto fbk = Manager::connect("Emitter");
        if(!fbk.isValid()) {
            printf("Connection failed\n");
            return 1;
        }

        auto hg_gather = Manager::createTeam(gather_string, participants.at(COLLECTOR_RANK), GATHER);
		
        int* gather_data = new int[hg_gather.size()];

        ssize_t r;
        do {
            int partial = 0;
            r = hg_gather.sendrecv(&rank, sizeof(int), gather_data, sizeof(int));
            if(r == 0) {
                printf("gather closed\n");
                break;
            }

            for(int i=1; i<hg_gather.size(); i++) {
                printf("Data from worker[%d]: %d\n", i, gather_data[i]);
                partial += gather_data[i];
            }

            printf("Collector computed %d\n", partial);

            fbk.send(&partial, sizeof(int));
        } while(r > 0);
        hg_gather.close();
        fbk.close();
        delete[] gather_data;
    }
    // Worker
    else {
        auto hg_bcast = Manager::createTeam(broadcast_string, participants.at(EMITTER_RANK), BROADCAST);

		auto hg_gather = Manager::createTeam(gather_string, participants.at(COLLECTOR_RANK), GATHER);

		
        if(hg_bcast.isValid() && hg_gather.isValid())
            printf("Correctly created teams\n");
        else {
            printf("bcast: %d - gather: %d\n", hg_bcast.isValid(), hg_gather.isValid());
            return 1;
        }

        rank--;

        ssize_t res;
        int subsize = streamlen/num_workers;
        int start = (rank-1)*subsize;
        int end = (rank == num_workers) ? streamlen : rank*subsize;
        int expected = ((end+1)*end/2) - ((start+1)*start/2);
        // printf("Rank [%d] managing elements in range [%d,%d)\n", rank, start, end);

        do {
            res = hg_bcast.sendrecv(nullptr, 0, data, streamlen*sizeof(int));
            if(res == 0) {
                printf("bcast closed\n");        
                break;
            }
        
            // Computing partial result
            int partial{0};
            for(int i = start; i < end; i++) {
                partial += data[i];
            }
        
            // Sending local result to Collector
            hg_gather.sendrecv(&partial, sizeof(int), nullptr, 0);

            printf("Worker%d computed: %d - expected: %d\n", rank, partial, expected);
        } while (res > 0);

        hg_bcast.close();        
        hg_gather.close();
    }

    delete[] data;
    Manager::finalize(true);

    return 0;
}
