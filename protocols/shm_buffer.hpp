#ifndef SHM_BUFFER_HPP
#define SHM_BUFFER_HPP

#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>


/*
 * Multi-producer multi-consumer circular buffer.
 * Each buffer entry contains a buffer_element_t messages.
 * Small messages (i.e., less than SHM_SMALL_MSG_SIZE) are stored directly into the buffer, 
 * large messages, instead, use a dedicated memory segment whose name is passed in the buffer.
 */
class shmBuffer {
protected:
	struct buffer_element_t {
		size_t  size;
		char    data[SHM_SMALL_MSG_SIZE];
	};
	struct shmSegment {
		sem_t mutex;
		sem_t full;
		sem_t empty;

		int    first;
		int    last;
		buffer_element_t data[SHM_BUFFER_SLOTS];
	} *shmp = nullptr;
	std::string segmentname{};
	std::atomic<int> cnt{0};
	std::atomic<bool> opened{false};
	
	void* createSegment(const std::string& name, size_t size) {
		int fd = shm_open(name.c_str(), O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
		if (fd == -1) return nullptr;
		if (ftruncate(fd, size) == -1) return nullptr;
		void *ptr = (shmSegment*)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		::close(fd);
		if (ptr == MAP_FAILED) return nullptr;
		return ptr;
	}
	void* openSegment(const std::string& name, size_t size) {
		int fd = shm_open(name.c_str(), O_RDWR, S_IRUSR|S_IWUSR);
		if (fd == -1) return nullptr;
#if 0
		struct stat sb;
		if (fstat(fd, &sb) == -1)	abort();
		assert(sb.st_size == (ssize_t)size);
#endif		
		void *ptr = (shmSegment*)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		::close(fd);
		if (ptr == MAP_FAILED) return nullptr;
		return ptr;
	}

	int createBuffer(const std::string& name, bool force=false) {
		int flags = O_CREAT|O_RDWR;
		if (force) flags |= O_TRUNC;
		int fd = shm_open(name.c_str(), flags, S_IRUSR|S_IWUSR);
		if (fd == -1) return -1;
		if (ftruncate(fd, sizeof(shmSegment)) == -1) return -1;
		shmp = (shmSegment*)mmap(NULL, sizeof(shmSegment), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		::close(fd);
		if (shmp == MAP_FAILED) {
			shmp = nullptr;
			return -1;
		}
		if (sem_init(&shmp->mutex, 1, 1) == -1) return -1;
		if (sem_init(&shmp->full, 1, SHM_BUFFER_SLOTS) == -1) return -1;
		if (sem_init(&shmp->empty, 1, 0) == -1) return -1;
		shmp->first = shmp->last = 0;
		segmentname=name;
		opened=true;
		return 0;
	}
	void extract(void* data, const size_t sz, size_t& size) {
		int first = shmp->first;
		size = shmp->data[first].size;
		if (size <= SHM_SMALL_MSG_SIZE) {
			memcpy((char*)data, shmp->data[first].data, std::min(sz,size));
		} else {
			std::string largedataseg(shmp->data[first].data);
			auto ptr= openSegment(largedataseg, size);
			if (!ptr) { // ERROR
				size=0;
				return;
			}
			memcpy((char*)data, (char*)ptr, std::min(sz, size));
			munmap(ptr, size);
			shm_unlink(largedataseg.c_str());
		}
		shmp->first = (first + 1) % SHM_BUFFER_SLOTS;
	}
	
public:

	shmBuffer() {}
	shmBuffer(const shmBuffer& o):shmp(o.shmp),segmentname(o.segmentname),cnt(o.cnt.load()), opened(o.opened.load()) {}
	
	const std::string& name() {return segmentname;}
	
	// creates a shared-memory buffer with a name
	int create(const std::string name, bool force=false) {
		if constexpr (SHM_SMALL_MSG_SIZE<sizeof(size_t)) {
			errno = EINVAL;
			return -1;
		}
		return createBuffer(name,force);
	}
	const bool isOpen() { return opened;}
	// opens an existing shared-memory buffer
	int open(const std::string name) {
		if constexpr (SHM_SMALL_MSG_SIZE<sizeof(size_t)) {
			errno = EINVAL;
			return -1;
		}
		if (opened) {
			errno = EPERM;
			return -1;
		}
		int fd = shm_open(name.c_str(), O_RDWR, 0); 
		if (fd == -1)
			return -1;
		//struct stat sb;
		//if (fstat(fd, &sb) == -1) return -1;
		//assert(sb.st_size == sizeof(shmSegment));
		shmp = (shmSegment*)mmap(NULL, sizeof(shmSegment), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		if (shmp == MAP_FAILED) return -1;
		::close(fd);
		segmentname=name;
		opened=true;
		return 0;
	}
	// closes and destroys (unlink=true) a shared-memory buffer previously
	// created or opened
	int close(bool unlink=false) {
		if (!shmp) {
			errno = EPERM;
			return -1;
		}
		sem_close(&shmp->mutex);
		sem_close(&shmp->full);
		sem_close(&shmp->empty);		
		munmap(shmp,sizeof(shmSegment));
		if (unlink) shm_unlink(segmentname.c_str());
		shmp=nullptr;
		return 0;
	}	
	// adds a message to the buffer
	int put(const void* data, const size_t sz) {
		if (!shmp || !data) {
			errno=EINVAL;
			return -1;
		}
		std::string largedataseg{""};
		if (sz > SHM_SMALL_MSG_SIZE) {
			auto id= cnt++ % (SHM_BUFFER_SLOTS<<2);

			largedataseg=segmentname+"_largemsg_"+std::to_string(id);
			auto ptr= createSegment(largedataseg, sz);
			if (!ptr) return -1;
			memcpy((char*)ptr, (char*)data, sz);
			munmap(ptr, sz);
		}
		if (sem_wait(&shmp->full) == -1) return -1;
		if (sem_wait(&shmp->mutex) == -1) return -1;

		int last = shmp->last;
		shmp->data[last].size=sz;
		if (sz <= SHM_SMALL_MSG_SIZE) {		
			if (sz) memcpy(shmp->data[last].data, data, sz);
		} else {
			assert(largedataseg.length()+1 <=SHM_SMALL_MSG_SIZE);
			memcpy(shmp->data[last].data, largedataseg.c_str(), largedataseg.length()+1);
		}
		shmp->last = (last + 1) % SHM_BUFFER_SLOTS;
		if (sem_post(&shmp->mutex) == -1) return -1;
		if (sem_post(&shmp->empty) == -1) return -1;
		return 0;
	}
	// retrieves a message from the buffer, it blocks if the buffer is empty	
	ssize_t get(void* data, const size_t sz) {
		if (!shmp || !data || !sz) {
			errno=EINVAL;
			return -1;
		}
		size_t size =0;
		if (sem_wait(&shmp->empty) == -1) return -1;
		if (sem_wait(&shmp->mutex) == -1) return -1;

		extract(data,sz, size);

		if (sem_post(&shmp->mutex) == -1) return -1;
		if (sem_post(&shmp->full) == -1) return -1;

		if (sz<size || size==0) {
			errno=ENOMEM;
			return -1;
		}
		return size;
	}
	// retrieves the size of the message in the buffer without removing the message
	// from the buffer, it blocks if the buffer is empty	
	ssize_t getsize() {
		if (!shmp) {
			errno=EINVAL;
			return -1;
		}
		if (sem_wait(&shmp->empty) == -1) return -1;
		if (sem_wait(&shmp->mutex) == -1) return -1;

		size_t size =  shmp->data[shmp->first].size;
		if (sem_post(&shmp->empty) == -1) return -1; // undo previous sem_wait
		
		if (sem_post(&shmp->mutex) == -1) return -1;
		return size;
	}
	// retrieves a message from the buffer, it doesn't block if the buffer is empty	
	ssize_t tryget(void* data, const size_t sz) {
		if (!shmp || !data || !sz) {
			errno=EINVAL;
			return -1;
		}
		
		size_t size =0;
		if (sem_trywait(&shmp->empty) == -1) { // returns errno=EAGAIN if it would block
			return -1;
		}
		if (sem_wait(&shmp->mutex) == -1) { // this could block for a while
			return -1;
		}

		extract(data,sz, size);

		if (sem_post(&shmp->mutex) == -1) return -1;
		if (sem_post(&shmp->full) == -1) return -1; 

		if (sz<size) {
			errno=ENOMEM;
			return -1;
		}
		return size;
	}
	// retrieves the size of the message in the buffer without removing the message
	// from the buffer, it doesn't block if the buffer is empty	
	ssize_t trygetsize() {
		if (!shmp) {
			errno=EINVAL;
			return -1;
		}
		if (sem_trywait(&shmp->empty) == -1) { // returns errno=EAGAIN if it would block
			return -1;
		}
		if (sem_wait(&shmp->mutex) == -1) return -1; // this could block fro a while

		size_t size =  shmp->data[shmp->first].size;
		if (sem_post(&shmp->empty) == -1) return -1; // undo previous sem_trywait
		
		if (sem_post(&shmp->mutex) == -1) return -1;
		return size;
	}
	// it peeks at whether there are any messages in the buffer
	// WARNING: The buffer may already be emptied by the time 'pick' returns.
	ssize_t peek() {
		int val;
		if (sem_getvalue(&shmp->empty, &val) == -1) return -1;
		return val;
	}
};

#endif
