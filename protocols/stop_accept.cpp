#include <mpi.h>
#include <stdio.h>

int main(int argc, char *argv[]){
    MPI_Init(&argc,&argv);

    MPI_Comm server;

    MPI_Comm_connect(argv[1], MPI_INFO_NULL, 0, MPI_COMM_WORLD, &server);
    printf("[STOP_ACCEPT]Connected to %s\n", argv[1]);
    
    /*NOTE: questa disconnect si blocca dato che il server potrebbe terminare
            prima di questa chiamata
    */
    // MPI_Comm_disconnect(&server);

    MPI_Finalize();
} 
