/*
 *  For maximum performance, set to 0 the *_POLL_TIMEOUT constants in the
 *  config.hpp file and compile the program with SINGLE_IO_THREAD=1.
 *
 *  $> TPROTOCOL="MPI UCX" make SINGLE_IO_THREAD=1 cleanall p2p-perf
 *
 *  When using the MPI transport with the IO_THREAD present, then it is 
 *  important to control thread affinity for example by using something like:
 * 
 *  $> mpirun --mca hwloc_base_binding_policy socket    \
 *          -n 1 --host host1 ./p2p-perf 0 "MPI:0:10" :	\
 *          -n 1 --host host2 ./p2p-perf 1 "MPI:0:10" 
 *
 *  or 
 *  $>  mpirun --report-bindings --bind-to-core .....
 *
 */

#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include <mpi.h>
#include <chrono>
#include <cstring>


const int     NROUND = 100;
const int          N = 24;
const size_t minsize = 16;              // bytes
const size_t maxsize = (1<<N);

#define DATACHECK
#if defined(DATACHECK)
#define CHECK(X) {X;}
#else
#define CHECK(X) 
#endif

void Server(const char serveraddr[]) {
	char *buff = new char[maxsize];
	assert(buff);

	
	for(size_t size=minsize; size<=maxsize; size *= 2) {
		for(int i=0;i<NROUND;++i) {
			size_t r;
			
			if (MPI_Recv(buff, size, MPI_BYTE, 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE) != MPI_SUCCESS) {
				printf("[Server]:\treceive error\n");
				break;
			}
			CHECK(buff[i]='b');
			MPI_Request req;
			if (MPI_Isend(buff, size, MPI_BYTE, 1, 1, MPI_COMM_WORLD, &req) != MPI_SUCCESS) {
				printf("[Server]:\tsend error\n");
				break;
			}
			MPI_Wait(&req, MPI_STATUS_IGNORE);
		}
	}
	
	printf("[Server]:\tclosing\n");
	delete [] buff;
}

void Client(const char serveraddr[]) {

	char *buff = new char[maxsize];
	assert(buff);
	char *buff2 = new char[maxsize];
	assert(buff2);
	memset(buff, 'a', maxsize);

	std::vector<std::pair<double, double>> times;
	
	for(size_t size=minsize; size<=maxsize; size *= 2) {
		std::chrono::microseconds V[NROUND];
		for(size_t i=0;i<NROUND;++i) {
			auto start = std::chrono::system_clock::now();
			MPI_Request req;
			if (MPI_Isend(buff, size, MPI_BYTE, 0, 1, MPI_COMM_WORLD, &req) != MPI_SUCCESS) {
				printf("[Client]:\tsend error\n");
				break;
			}
			
			MPI_Wait(&req, MPI_STATUS_IGNORE);

			if (MPI_Recv(buff2, size, MPI_BYTE, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE) != MPI_SUCCESS) {
				printf("[Client]:\treceive error\n");
				break;
			}			
			auto end = std::chrono::system_clock::now();
			CHECK(for(size_t j=0;j<size;++j) { if (i==j) { if (buff2[j]!='b') abort();} else { if (buff2[j]!='a') abort();}})
			V[i] = std::chrono::duration_cast<std::chrono::microseconds>(end-start);
		}
		double sum{0.0};
		for(int i=0;i<NROUND;++i)
			sum += (V[i].count()/2.0);
		double mean{sum/NROUND};
		sum=0.0;
		for(int i=0;i<NROUND;++i) 
			sum += std::pow(V[i].count() - mean, 2);
		times.push_back(std::make_pair(mean, std::sqrt(sum/(NROUND-1))));
	}
	printf("[Client]:\tclosing\n");
	
	size_t size = minsize;
	std::cout << "   size   lat avg (ms)   lat std (ms)       Bw (MB/s)\n";
	std::cout << "-----------------------------------------------------\n";
	for(auto& p:times) {		
		std::cout << std::fixed << std::setprecision(4)
				  << std::setw(8) << size << "        "
				  << std::setw(6) << p.first/1000.0 << "         "
				  << std::setw(6) << p.second/1000.0 << "        "
				  << std::setw(9) << (size*1e6)/(1048576*p.first) << "\n";
		size *= 2;
	}
	delete [] buff;
	delete [] buff2;
}

int main(int argc, char** argv){
    MPI_Init(&argc, &argv);
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == 0)
		Server(argv[2]);            
    else
		Client(argv[2]);	
    
	MPI_Finalize();
	
    return 0;
}
