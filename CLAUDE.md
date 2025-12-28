# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **Hybrid MPI/OpenMP Parallelizer** - a sophisticated static analysis tool that automatically generates hybrid MPI/OpenMP parallel code from sequential C++ programs. The tool uses Clang/LLVM for AST analysis and generates production-ready parallel code with dependency analysis and visualization.

## Build System

### Prerequisites
- LLVM/Clang (version 13+)
- MPI implementation (OpenMPI, MPICH) 
- OpenMP support
- CMake (3.13+)
- Graphviz (optional, for dependency graph visualization)

### Building
```bash
# Configure and build the main tool
cmake -B build .
cmake --build build

# This creates: build/mpi-parallelizer
```

### Running the Tool
```bash
# Basic usage - analyze and parallelize a C++ file
./build/mpi-parallelizer input_file.cpp

# Disable loop parallelization (MPI-only mode)
./build/mpi-parallelizer --no-loops input_file.cpp
```

### Generated Outputs
- `enhanced_hybrid_mpi_openmp_output.cpp` - Main parallel code output
- `dependency_graph_visualization.html` - Interactive D3.js visualization
- `dependency_graph.dot` - Graphviz format for professional graphs

### Compiling Generated Code
```bash
# Compile the generated parallel code
mpicxx -fopenmp -o parallel_program enhanced_hybrid_mpi_openmp_output.cpp -lm

# Run with MPI processes
mpirun -np 4 ./parallel_program
```

## Architecture Overview

The codebase uses a **modular architecture** with clear separation of concerns:

### Core Data Structures (`data_structures.h`)
Contains all struct definitions:
- `LoopInfo` - Loop analysis with OpenMP pragma generation
- `FunctionInfo` - Function metadata and parallelization details
- `FunctionCall` - Function call extraction from main()
- `LocalVariable` - Variable dependency tracking
- `FunctionAnalysis` - Global variable read/write analysis
- `DependencyNode` - Dependency graph construction

### Analysis Modules
- **`loop_analyzer.h/cpp`** - `ComprehensiveLoopAnalyzer` class for loop parallelizability detection and OpenMP pragma generation
- **`function_analyzer.h/cpp`** - `GlobalVariableCollector` and `ComprehensiveFunctionAnalyzer` for variable dependency analysis
- **`main_extractor.h/cpp`** - `MainFunctionExtractor` for function call extraction from main()
- **`hybrid_parallelizer.h/cpp`** - `HybridParallelizer` class for MPI/OpenMP code generation and dependency graph building

### Frontend Integration
- **`ast_consumer.h/cpp`** - Clang AST integration with `HybridParallelizerConsumer` and `HybridParallelizerAction`
- **`mpi_parallelizer_new.cpp`** - Main entry point with command line handling

### Parallelization Strategy

1. **MPI Level**: Parallelizes independent function calls across processes with automatic dependency analysis
2. **OpenMP Level**: Parallelizes loops within functions with comprehensive analysis:
   - Detects parallelizable patterns (88-94% success rate)
   - Generates appropriate scheduling and reduction clauses
   - Handles thread-unsafe functions and complex conditions
   - Avoids loops with break/continue statements

3. **Dependency Analysis**: 
   - Global variable read/write tracking
   - Function call dependency chains
   - Automatic MPI communication generation
   - Deadlock-free process assignment

## Development Workflow

### Testing Generated Code
The repository contains numerous test files demonstrating different scenarios:
- `minimal_test.cpp` - Basic functionality validation
- `sophisticated_test.cpp` - Comprehensive 400+ line test with 20+ functions
- Various benchmark and performance test files

### Common Development Tasks

When working with this codebase:

1. **Modifying Analysis Logic**: Changes typically go in the specific analyzer modules
2. **Adding New Parallelization Patterns**: Extend the `LoopAnalyzer` or `HybridParallelizer` classes  
3. **Improving Code Generation**: Focus on the `generateParallelizedCode()` methods
4. **Debugging Analysis**: Check the generated `.dot` files and HTML visualizations

### Key Global Variables
- `enableLoopParallelization` - Controls OpenMP loop parallelization (can be disabled with `--no-loops`)

## Important Notes

- The tool automatically generates dependency graphs and visualizations for every analysis
- Generated code includes proper MPI initialization, process management, and finalization
- OpenMP pragmas are automatically optimized with reduction clauses and scheduling
- The modular architecture allows incremental compilation and easy feature extension
- All generated code is production-ready and handles edge cases like insufficient MPI processes