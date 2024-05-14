
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
        int a = 1, b = 2, p = 3;
    int dd, ee;
    b = a+p;


int tempRank = 1;
// Rank 0 sends arguments to other rank:
if(rank == 0){
    // Array storing data for the rank executing the function:
    char* array = (char *) malloc(MAX_BYTES);
    int nArray = 0; // length of the array
    PUSH(a);
    PUSH(b);

    // Send portion of data to processing element of rank number tempRank:
    cout << "Rank 0 sending function call parameters to rank 1..." << endl;
    MPI_Send(array, nArray, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD);
    cout << "Rank 0 sent function call parameters to rank 1." << endl;
}else{
    // Only tempRank should be receiving the data
    // and executing the function:
    if(rank == tempRank){
       // Rank receives arguments prepared for it:
       char* array = (char *) malloc(MAX_BYTES); // array storing portion of data for this rank
       int nArray = 0; // length of the array
       cout << "  Rank 1 receiving function call parameters from rank 0..." << endl;
       MPI_Recv(array, MAX_BYTES, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // maximum MAX_BYTES characters
    POP(a);
    POP(b);

       cout << "  Rank 1 received function call parameters from rank 0." << endl;
   }
}

MPI_Barrier(MPI_COMM_WORLD);
cout << "Rank " << rank << " in the middle." << endl;

// Only rank 1 sends result of a function call to rank 0:
if(rank == tempRank){
    cout << "Rank 1 EXECUTING A FUNCTION CALL." << endl;
    dd = f(a,b);
    // Array storing data for the result of a function call:
    char* array = (char *) malloc(MAX_BYTES);
    int nArray = 0; // length of the array
    // Send results to rank number 0:
    cout << "  Rank 1 sending results of a function call to rank 0..." << endl;
    PUSH(dd);
    MPI_Send(array, nArray, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
    cout << "  Rank 1 sent results of a function call to rank 0." << endl;
}else{
    // Only rank 0 should be receiving the result:
    if(!rank){
        // Array storing data for the result of a function call:
        char* array = (char *) malloc(MAX_BYTES);
        int nArray = 0; // length of the array
        // Start receiving results:
        cout << "Rank 0 collecting results of a function call from rank 1..." << endl;
        MPI_Recv(array, MAX_BYTES, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // maximum MAX_BYTES characters
        POP(dd);
        cout << "Rank 0 collected results of a function call from rank 1 (" << dd << ")." << endl;
        //nArrayResults[tempRank] = deserialize_data(arrayInput);
    }
}

    b = a*p;
    MPI_Finalize();
}
