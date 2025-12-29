#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H

#include <string>
#include <vector>
#include <set>
#include <map>

// Structure to hold loop information for OpenMP parallelization
struct LoopInfo {
    std::string type;                    // "for", "while", "do-while"
    std::string source_code;             // Original loop source
    std::string loop_variable;           // Loop iterator variable
    std::string loop_variable_type;      // NEW: Type of loop variable (e.g., "int")
    std::vector<std::string> read_vars;  // Variables read in loop
    std::vector<std::string> write_vars; // Variables written in loop
    std::vector<std::string> reduction_vars; // Reduction variables
    std::string reduction_op;            // Reduction operation (+, *, etc.)
    bool has_dependencies;               // Loop-carried dependencies
    bool has_function_calls;             // Contains function calls
    bool has_io_operations;              // Contains I/O operations
    bool has_break_continue;             // Contains break/continue statements
    bool has_complex_condition;          // Complex loop condition (&&, ||)
    bool is_nested;                      // Is a nested loop
    bool has_thread_unsafe_calls;        // Contains thread-unsafe function calls
    std::vector<std::string> unsafe_functions; // List of thread-unsafe functions found
    std::set<std::string> thread_local_vars; // Variables that need thread-local storage
    bool parallelizable;                 // Can be parallelized
    std::string schedule_type;           // Recommended schedule
    std::string analysis_notes;          // Analysis details
    unsigned start_line, end_line;       // Source location
    unsigned start_col, end_col;         // Column positions
    std::string function_name;           // Function containing this loop
    std::string pragma_text;             // Generated OpenMP pragma
    
    // NEW: Fields for MPI loop parallelization
    std::string start_expr;              // Loop start expression (e.g., "0")
    std::string end_expr;                // Loop end expression (e.g., "N")
    std::string step_expr;               // Loop step expression (e.g., "1")
    bool is_mpi_parallelizable;          // Can be parallelized with MPI
    bool is_canonical;                   // Is in canonical form (for(i=start; i<end; i+=step))
};

// Structure to hold function information with loops
struct FunctionInfo {
    std::string name;
    std::string return_type;
    std::vector<std::string> parameter_types;
    std::vector<std::string> parameter_names;
    std::string original_body;
    std::string parallelized_body;
    std::string complete_function_source;   // NEW: Complete function source code including signature
    std::string function_signature;         // NEW: Just the function signature
    std::vector<LoopInfo> loops;
    std::set<std::string> global_reads;
    std::set<std::string> global_writes;
    std::set<std::string> local_vars;
    bool has_parallelizable_loops;
    unsigned start_line, end_line;
};

// Structure to hold function call information
struct FunctionCall {
    std::string functionName;
    std::string callExpression;
    unsigned lineNumber;
    bool hasReturnValue;
    std::string returnVariable;
    std::string returnType;
    std::vector<std::string> parameterVariables;
    std::set<std::string> usedLocalVariables;
};

// NEW: Structure to hold global variable information with types
struct GlobalVariable {
    std::string name;
    std::string type;
    std::string defaultValue;
    bool isArray;
    std::string arraySize;
};

// Structure to hold local variable information
struct LocalVariable {
    std::string name;
    std::string type;
    std::string initializationValue;  // Store the original initialization expression
    std::string completeDeclaration;  // NEW: Complete variable declaration from source
    bool hasComplexInitialization;    // NEW: Flag for complex C++11 initializations
    int declarationOrder;  // Order in which variable was declared in source
    int definedAtCall;
    std::set<int> usedInCalls;
    bool isParameter;
};

// Structure to hold function analysis results
struct FunctionAnalysis {
    std::set<std::string> readSet;
    std::set<std::string> writeSet;
    std::set<std::string> localReads;
    std::set<std::string> localWrites;
    bool isParallelizable = true;
    std::string returnType;
    std::vector<std::string> parameterTypes;
};

// Structure for dependency graph node
struct DependencyNode {
    std::string functionName;
    int callIndex;
    std::set<int> dependencies;
    std::set<int> dependents;
    std::string dependencyReason;
};

// NEW: Structure to hold typedef information
struct TypedefInfo {
    std::string name;           // Name of the typedef
    std::string definition;     // Complete typedef definition
    std::string underlyingType; // The underlying type
    unsigned line;              // Line number in source
};

// NEW: Structure to hold complete source code context
struct SourceCodeContext {
    std::vector<std::string> includes;          // All #include statements
    std::vector<TypedefInfo> typedefs;          // All typedef statements
    std::vector<std::string> globalDeclarations; // Global variable declarations
    std::string namespaceDeclarations;          // using namespace statements
    std::vector<std::string> forwardDeclarations; // Forward declarations
};

#endif // DATA_STRUCTURES_H