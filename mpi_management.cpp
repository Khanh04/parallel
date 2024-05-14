#include "mpi_management.h"

void addDefinesAndIncludes(std::ofstream &fOut) {
    fOut << "\n\
using namespace std;\n\
\n\
#define VERBOSE 0\n\
// PARALLELIZE constant directs whether the parallel program should be created.\n\
// Value 0 means that the input file will be solely parsed,\n\
// without parallelization\n\
// Value 1 means that the input file will be parsed,\n\
// and the parallel version of the algorithm will be created.\n\
#define MAX_RANKS 100 // maximum number of ranks allowed\n\
#define MAX_BYTES 1000 // maximum number of bytes that each rank is allowed to send/receive\n\
\n\
#include \"mpi_functions.cpp\"\n\
\n";
}
