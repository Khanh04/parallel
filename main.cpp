// TODO: Delete following? Decide what to do with data of this type:
// Dependency graph constants are not used at the moment:
#define CONSTANT 0
#define SCALAR 1
#define ARRAY 2
#define POINTER 3

#include <iostream>
#include <vector>
#include <set> // For checking overlapping or LHS and RHS variables in a loop
#include <fstream>
#include <string>
#include <regex>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <map>
#include <cctype>
#include <cmath>


#include "common.h"
#include "parser.h"
#include "utils.h"
#include "dependency_graph.h"
#include "functions.h"
#include "mpi_management.h"
#include "file_operations.h"

using namespace std;

int main() {
    // Statements are numerated.
    // Each time a statement is parsed, it gets a new statement id.
    int maxStatementId = 0;
    bool inMain = false; // Whether currently parsed function is main()
    Functions f;

    ifstream fIn("test.cpp"); // input file stream
    ofstream fOut; // output file stream

    if (PARALLELIZE)
        fOut.open("testPar.cpp", std::fstream::out);

    std::cout << std::endl << std::endl << "Parsing input file test.cpp" << std::endl;

    if (PARALLELIZE)
        addDefinesAndIncludes(fOut);

    numOpenedBrackets = 0;
    string functionName = ""; // name of the function that is currently parsed
    string fileLine; // a string containing a line read from the input file

    while (getline(fIn, fileLine)) {
    parseInputFileLine(fIn, fOut, f, functionName, fileLine, maxStatementId, inMain, PARALLELIZE);
    }

    fIn.close(); // closing the input file

    if (PARALLELIZE)
        fOut.close(); // closing the output file
    
    cout << endl << endl << "Results:" << endl;

    // Current set of variables:    
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];

    // Printing all variables:
    v.printDetailed();

    // Printing dependencies in a form of a vector of dependencies:
    Graph::graphs[Graph::iCurrentGraph].print();

    // Printing dependencies in a form of a dependency graph:
    Graph::graphs[Graph::iCurrentGraph].printTable();

    f.print();

    return 0;
}
