/*
 * 
 *
 */

#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include "mtcl.hpp"

const int     NROUND = 5;
const int          N = 19;
const size_t minsize = 16;              // bytes
const size_t maxsize = (1<<N)*minsize;


void Server() {
	// Some of the following calls might fail, but at least one will succeed
	Manager::listen("TCP:0.0.0.0:42000");
	Manager::listen("MPI:0:10");
	Manager::listen("MPIP2P:test");
    Manager::listen("MQTT:label");

	char *buff = new char[maxsize];
	assert(buff);

	auto handle=Manager::getNext(); 
	
	for(size_t size=minsize; size<=maxsize; size *= 2) {
		for(int i=0;i<NROUND;++i) {
			size_t r;
			if ((r=handle.receive(buff, size))<=0) {
				MTCL_PRINT("[Server]:\t", "receive error, errno=%d (%s)\n",
						   errno, strerror(errno));
				break;
			}
			assert(r==size);
			if (handle.send(buff, size)<=0) {
				MTCL_PRINT("[Server]:\t", "send error, errno=%d (%s)\n",
						   errno, strerror(errno));
				break;
			}
		}
	}
	delete [] buff;
	handle.close();
}

void Client(const char serveraddr[]) {
	// try to connect with different transports until we find a valid one
	auto handle = [serveraddr]() {
					  auto h = Manager::connect(serveraddr);
					  if (!h.isValid()) {
						  auto h = Manager::connect(serveraddr);
						  if (!h.isValid()) {
							  auto h = Manager::connect(serveraddr);
							  if (!h.isValid()) {
								  auto h = Manager::connect(serveraddr);
								  assert(h.isValid());
								  return h;
							  } else return h;
						  } else return h;
					  } else return h;
				  }();
	assert(handle.isValid());
	
	char *buff = new char[maxsize];
	assert(buff);
	memset(buff, 'a', maxsize);

	std::vector<std::pair<double, double>> times;
	
	for(size_t size=minsize; size<=maxsize; size *= 2) {
		std::chrono::duration<double> V[NROUND];
		double sum{0.0};
		for(int i=0;i<NROUND;++i) {
			size_t r;
			auto start = std::chrono::system_clock::now();
			if (handle.send(buff, size)<=0) {
				MTCL_PRINT("[Client]:\t", "send error, errno=%d (%s)\n",
						   errno, strerror(errno));
				break;
			}
			if ((r=handle.receive(buff, size))<=0) {
				MTCL_PRINT("[Client]:\t", "receive error, errno=%d (%s)\n",
						   errno, strerror(errno));
				break;
			}			
			auto end = std::chrono::system_clock::now();
			assert(r==size);
			for(size_t j=0;j<size;++j) assert(buff[j]=='a');
			V[i] = end-start;
			sum += V[i].count();
		}
		double mean = sum/NROUND;
		double s{0.0};
		for(int i=0;i<NROUND;++i) {
			s += std::pow(V[i].count() - mean, 2);
		}
		times.push_back(std::make_pair(sum/NROUND, std::sqrt(s/(NROUND-1))));
	}
	delete [] buff;
	handle.close();

	size_t size = minsize;
	std::cout << "   size      avg (ms)       std (ms)        Bw (MB/s)\n";
	std::cout << "-----------------------------------------------------\n";
	for(auto& p:times) {		
		std::cout << std::fixed << std::setprecision(3)
				  << std::setw(7) << size << "        "
				  << std::setw(6) << p.first*1000.0 << "         "
				  << std::setw(6) << p.second*1000.0 << "        "
				  << std::setw(9) << size/(1048576*p.first) << "\n";
		size *= 2;
	}
	
}

int main(int argc, char** argv){
    if(argc < 2) {
		MTCL_ERROR("Usage: ", "%s <0|1> [server-addr]\n", argv[0]);
        return -1;
    }
	long cs=std::stol(argv[1]);
	if (cs != 0) {
		if (argc != 3) {
			MTCL_ERROR("Usage: ", "%s 1 server-addr\n", argv[0]);
			return -1;
		}
	}
	
    Manager::init(argv[1]);   
    if (std::stol(argv[1]) == 0)
		Server();            
    else
		Client(argv[2]);	
    Manager::finalize();
	
    return 0;
}
