
using namespace std;

#define VERBOSE 0
// PARALLELIZE constant directs whether the parallel program should be created.
// Value 0 means that the input file will be solely parsed,
// without parallelization
// Value 1 means that the input file will be parsed,
// and the parallel version of the algorithm will be created.
#define MAX_RANKS 100 // maximum number of ranks allowed
#define MAX_BYTES 1000 // maximum number of bytes that each rank is allowed to send/receive

#include "mpi_functions.cpp"

#include <mpi.h>
#include <string>
#include <vector>
#include <array>
#include <iostream>
int f(int aa, int bb){
    return aa*1000 + bb;
}
int main(){

        int rank, nRanks;
        MPI_Init(NULL, NULL);
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Comm_size(MPI_COMM_WORLD, &nRanks);
        int a = 1, b = 2, p = 3, i;
int result, d;
    b = a+p;


int tempRank = 1;
// Rank 0 sends arguments to other rank:
if(rank == 0){
    char* array = (char *) malloc(MAX_BYTES);
    int nArray = 0; // length of the array
    PUSH(a);
    PUSH(b);

    MPI_Send(array, nArray, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD);
}else{
    if(rank == tempRank){
       char* array = (char *) malloc(MAX_BYTES);
       int nArray = 0;
       MPI_Recv(array, MAX_BYTES, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    POP(a);
    POP(b);

   }
}

MPI_Barrier(MPI_COMM_WORLD);
cout << "Rank " << rank << " in the middle." << endl;

if(rank == tempRank){
        result = f(a,b);
    char* array = (char *) malloc(MAX_BYTES);
    int nArray = 0;
    PUSH(result);
    MPI_Send(array, nArray, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
}else{
    if(!rank){
        char* array = (char *) malloc(MAX_BYTES);
        int nArray = 0;
        MPI_Recv(array, MAX_BYTES, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        POP(result);
    }
}

        d=p+2;
        d=p+2;
        d=p+2;
    b = a*p;
}
