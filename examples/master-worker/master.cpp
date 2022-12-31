/*
 * One master and multiple slaves (workers) example using multiple 
 * transport protocols.
 *
 * HOW TO RUN: 
 * ^^^^^^^^^^^
 * First you should start all non-MPI receiving workers, then all 
 * MPI workers together with the master in a single MPMD mpirun. 
 * Example:
 *   workers.list contains 4 addresses:
 *    TCP:node1:8000
 *    TCP:node2:9000
 *    MPI:0:10
 *    MPI:1:10
 *
 *  start one worker on node1 and one worker on node2:
 *  $node1> MTCL_VERBOSE=1 ./worker 
 *  $node2> MTCL_VERBOSE=1 ./worker
 *  then execute
 *  mpirun -n 2 --host node3,node4 -x MTCL_VERBOSE=1 ./worker : \
 *         -n 1 --host node5 -x MTCL_VERBOSE=1 ./master
 *
 */

#include <cassert>
#include <iostream>
#include <string>
#include <fstream>
#include <algorithm>

#include <mtcl.hpp>

const int NMSGS      = 50;
const int headersize = 3;
const int maxpayload = 100; 

std::string random_string( size_t length ){
	assert(length<=maxpayload);
    auto randchar = []() -> char {
        const char charset[] =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[ rand() % max_index ];
    };
	
    std::string str(length+headersize,0);
	char lenstr[headersize+1];
	snprintf(lenstr, sizeof(lenstr), "%03ld", length);
	str.insert(0,lenstr);
    std::generate_n( str.begin()+headersize, length, randchar );
    return str;
}

int main(int argc, char** argv){
    Manager::init("master");

    std::vector<HandleUser> writeHandles;
	std::vector<std::string> conns;
	
    std::ifstream input("workers.list");
    for( std::string line; getline( input, line ); ) {
		if (line.empty()) continue;
		conns.push_back(line);
    }
	input.close();
    for( auto& c: conns) {
        MTCL_PRINT("[Server]:\t", "Connecting to: %s\n", c.c_str());
		for(int i=0;i<5;++i) {
			auto h = Manager::connect(c);
			if (!h.isValid()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
			}
			writeHandles.emplace_back(std::move(h));
			writeHandles.back().setName("worker" + std::to_string(writeHandles.size()));
			MTCL_PRINT("[Server]:\t", "connected to %s\n", writeHandles.back().getName().c_str());
			break;
		}
	}
	size_t workers = writeHandles.size();
	if (workers==0) {
        MTCL_ERROR("[Server]:\t", "No connected workers, exit!\n");
		Manager::finalize();	
		return -1;
	}
	
    std::thread t([&writeHandles, workers](){
					  for(int i = 0; i < NMSGS; i++) {
						  int sz = (rand() % maxpayload) + 1;						  
						  std::string str = random_string(sz);
						  MTCL_PRINT("[Server]:\t", "sending %s to worker%d (%s)\n", str.c_str()+headersize, i%workers, writeHandles[i%workers].getName().c_str());
						  
						  if (writeHandles[i % workers].send(str.c_str(), headersize) == -1) {
							  MTCL_ERROR("[Server]:\t", "ERROR sending the header message, errno=%d\n", errno);
						  }
						  if (writeHandles[i % workers].send(str.c_str()+headersize, sz) == -1) {
							  MTCL_ERROR("[Server]:\t", "ERROR sending the header message, errno=%d\n", errno);
						  }
						  /// NOTE: for MPI-based connections, send and receive must be paired calls, therefore a single send delivering
						  /// the header and payload together doesn't work if the peer try to receive them in two distinct receives.
						  /// if (writeHandles[i % workers].send(str.c_str(), sz+headersize) == -1) {
						  ///   MTCL_ERROR("[Server]:\t", "ERROR sending the message, errno=%d\n", errno);
						  /// }
					  }
					  
					  const char EOS[]="003EOS";
					  for(size_t i=0;i<workers;++i) {
						  if (writeHandles[i].send(EOS, headersize)==-1) {
							  MTCL_ERROR("[Server]:\t", "ERROR sending EOS header to worker%d, errno=%d\n", i, errno);
							  writeHandles[i].close();
							  continue;
						  }
						  if (writeHandles[i].send(EOS+headersize, headersize)==-1) {
							  MTCL_ERROR("[Server]:\t", "ERROR sending EOS to worker%d, errno=%d\n", i, errno);
							  writeHandles[i].close();
							  continue;
						  }
						  MTCL_PRINT("[Server]:\t", "Sent EOS to worker%d, %s\n", i, writeHandles[i].getName().c_str());
					  }
				  });

    for(auto& h : writeHandles) h.yield();
	
    char ack;
	while(workers>0) {
        auto h = Manager::getNext();
        ssize_t r;
		if ((r=h.receive(&ack, 1)) == -1) {
			if (errno==ECONNRESET) {
				MTCL_PRINT("[Server]:\t", "connection closed by worker %s\n", h.getName().c_str());
				--workers;
				h.close();
				continue;
			} else {
				MTCL_ERROR("[Server]:\t", "ERROR receiving ack, errno=%d\n", errno);
				break;
			}
		}
        if (r == 0)	{
			MTCL_PRINT("[Server]:\t", "connection closed by worker %s\n", h.getName().c_str());
			--workers;
			h.close();
		} else MTCL_PRINT("[Server]:\t", "received ack from worker %s\n", h.getName().c_str()); 
    }

	MTCL_PRINT("[Server]:\t", "closing handles\n");
	for(auto& h : writeHandles)	h.close(); 
	
    t.join();

	MTCL_PRINT("[Server]:\t", "finalizing\n");
    Manager::finalize();
   
    return 0;
}
