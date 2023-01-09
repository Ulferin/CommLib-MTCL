#ifndef UTILS_HPP
#define UTILS_HPP

#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>

extern int mtcl_verbose;

#define MTCL_PRINT(LEVEL, prefix, str, ...)								\
	if (mtcl_verbose>=LEVEL) print_prefix(stdout, str, prefix, ##__VA_ARGS__)
#define MTCL_ERROR(prefix, str, ...)									\
	print_prefix(stderr, str, prefix, ##__VA_ARGS__)
#define MTCL_TCP_PRINT(LEVEL, str, ...) MTCL_PRINT(LEVEL, "[MTCL TCP]:\t",str, ##__VA_ARGS__)
#define MTCL_SHM_PRINT(LEVEL, str, ...) MTCL_PRINT(LEVEL, "[MTCL SHM]:\t",str, ##__VA_ARGS__)
#define MTCL_UCX_PRINT(LEVEL, str, ...) MTCL_PRINT(LEVEL, "[MTCL UCX]:\t",str, ##__VA_ARGS__)
#define MTCL_MPI_PRINT(LEVEL, str, ...) MTCL_PRINT(LEVEL, "[MTCL MPI]:\t",str, ##__VA_ARGS__)
#define MTCL_MQTT_PRINT(LEVEL, str, ...) MTCL_PRINT(LEVEL, "[MTCL MQTT]:\t",str, ##__VA_ARGS__)
#define MTCL_MPIP2P_PRINT(LEVEL, str, ...) MTCL_PRINT(LEVEL, "[MTCL MPIP2P]:\t",str, ##__VA_ARGS__)
#define MTCL_TCP_ERROR(str, ...) MTCL_ERROR("[MTCL TCP]:\t",str, ##__VA_ARGS__)
#define MTCL_SHM_ERROR(str, ...) MTCL_ERROR("[MTCL SHM]:\t",str, ##__VA_ARGS__)
#define MTCL_UCX_ERROR(str, ...) MTCL_ERROR("[MTCL UCX]:\t",str, ##__VA_ARGS__)
#define MTCL_MPI_ERROR(str, ...) MTCL_ERROR("[MTCL MPI]:\t",str, ##__VA_ARGS__)
#define MTCL_MQTT_ERROR(str, ...) MTCL_ERROR("[MTCL MQTT]:\t",str, ##__VA_ARGS__)
#define MTCL_MPIP2P_ERROR(str, ...) MTCL_ERROR("[MTCL MPIP2P]:\t",str, ##__VA_ARGS__)

static inline void print_prefix(FILE *stream, const char * str, const char *prefix, ...) {
    va_list argp;
    char * p=(char *)malloc(strlen(str)+strlen(prefix)+1);
    if (!p) {
		perror("malloc");
        fprintf(stderr,"FATAL ERROR in print_prefix\n");
        return;
    }
    strcpy(p,prefix);
    strcpy(p+strlen(prefix), str);
    va_start(argp, prefix);
    vfprintf(stream, p, argp);
	fflush(stream);
    va_end(argp);
    free(p);
}


// if SINGLE_IO_THREAD is defined, we do not use locking for accessing
// internal data structures, thus some code can be removed.
// A different case is for the MPIP2P transport protocol.
#if defined(SINGLE_IO_THREAD)
#define REMOVE_CODE_IF(X)
#define ADD_CODE_IF(X)    X
#else
#define REMOVE_CODE_IF(X) X
#define ADD_CODE_IF(X) 
#endif


#endif
