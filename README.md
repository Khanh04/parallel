# MPI Parallelizer

### Core Data Structures
- **`data_structures.h`** - Contains all struct definitions:
  - `LoopInfo` - Loop analysis information
  - `FunctionInfo` - Function metadata and analysis
  - `FunctionCall` - Function call details
  - `LocalVariable` - Local variable information
  - `FunctionAnalysis` - Analysis results
  - `DependencyNode` - Dependency graph nodes

### Analysis Modules
- **`loop_analyzer.h/cpp`** - Loop analysis functionality:
  - `ComprehensiveLoopAnalyzer` class
  - Loop detection and parallelizability analysis
  - OpenMP pragma generation

- **`function_analyzer.h/cpp`** - Function analysis:
  - `GlobalVariableCollector` class
  - `ComprehensiveFunctionAnalyzer` class
  - Variable dependency analysis

- **`main_extractor.h/cpp`** - Main function analysis:
  - `MainFunctionExtractor` class
  - Function call extraction from main()
  - Local variable analysis

- **`hybrid_parallelizer.h/cpp`** - Parallelization logic:
  - `HybridParallelizer` class
  - Dependency graph building
  - MPI/OpenMP code generation

### Frontend Components
- **`ast_consumer.h/cpp`** - Clang AST integration:
  - `HybridParallelizerConsumer` class
  - `HybridParallelizerAction` class
  - Analysis coordination and output generation

### Main Application
- **`mpi_parallelizer_new.cpp`** - Refactored main file:
  - Clean main() function
  - Module includes
  - Command line handling

## Benefits of Refactoring

1. **Modularity** - Each component has a clear, single responsibility
2. **Maintainability** - Easier to modify individual components
3. **Reusability** - Components can be reused in other projects
4. **Testability** - Individual modules can be tested in isolation
5. **Readability** - Smaller, focused files are easier to understand
6. **Compilation** - Faster incremental builds when only one module changes

## Build System

The `CMakeLists.txt` has been updated to:
- Build the new modular version as `mpi-parallelizer`
- Keep the original version as `mpi-parallelizer-original` for comparison
- Link all required libraries to both executables

## File Sizes Comparison

- **Original**: `mpi_parallelizer.cpp` - 1733 lines
- **Refactored**: 
  - `mpi_parallelizer_new.cpp` - 68 lines
  - `data_structures.h` - 74 lines
  - `loop_analyzer.h/cpp` - 55 + 350 lines
  - `function_analyzer.h/cpp` - 48 + 180 lines  
  - `main_extractor.h/cpp` - 39 + 150 lines
  - `hybrid_parallelizer.h/cpp` - 40 + 400 lines
  - `ast_consumer.h/cpp` - 34 + 200 lines

## Usage

Both versions can be built and used:

```bash
# Build both versions
cmake --build .

# Use the refactored version
./mpi-parallelizer source_file.cpp

# Use the original version (for comparison)
./mpi-parallelizer-original source_file.cpp
```

The functionality remains identical - the refactored version produces the same analysis and output as the original.