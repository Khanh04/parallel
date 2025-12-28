# Copilot Instructions for Hybrid MPI/OpenMP Parallelizer

## Project Overview
This is a **static analysis tool** that automatically parallelizes sequential C++ code using LLVM/Clang AST analysis. It generates hybrid MPI (inter-process) + OpenMP (intra-process) parallel code with dependency analysis and visualizations.

**Core purpose**: Transform `input.cpp` → `enhanced_hybrid_mpi_openmp_output.cpp` with automatic parallelization and dependency graph visualizations.

## Architecture: Modular Pipeline (~1,300 LOC)

### Data Flow
```
input.cpp → Clang AST → Analyzers → HybridParallelizer → parallel code + visualizations
```

### Core Modules (all depend on `data_structures.h`)
1. **`loop_analyzer.cpp`** - `ComprehensiveLoopAnalyzer` - Detects parallelizable loops, generates OpenMP pragmas
2. **`function_analyzer.cpp`** - `ComprehensiveFunctionAnalyzer` - Tracks global variable reads/writes
3. **`main_extractor.cpp`** - `MainFunctionExtractor` - Extracts function calls from main()
4. **`hybrid_parallelizer.cpp`** - `HybridParallelizer` - Builds dependency graph, generates MPI/OpenMP code
5. **`ast_consumer.cpp`** - `HybridParallelizerConsumer` - Orchestrates analysis, generates visualizations
6. **`mpi_parallelizer_new.cpp`** - Entry point with `--no-loops` flag support

**Key data structure**: `data_structures.h` contains `LoopInfo`, `FunctionInfo`, `FunctionCall`, `LocalVariable`, `FunctionAnalysis`, `DependencyNode` - understand these first.

## Critical Build/Run Commands

### Build
```bash
cmake -B build .
cmake --build build
# Creates: build/mpi-parallelizer
```

### Run Tool
```bash
./build/mpi-parallelizer input.cpp              # Full hybrid MPI+OpenMP
./build/mpi-parallelizer --no-loops input.cpp   # MPI-only mode
# Generates: enhanced_hybrid_mpi_openmp_output.cpp, dependency_graph_visualization.html, dependency_graph.dot
```

### Test Generated Code
```bash
mpicxx -fopenmp -o program enhanced_hybrid_mpi_openmp_output.cpp -lm
mpirun -np 4 ./program
```

### Run Tests
```bash
cd tests
g++ -std=c++17 -I.. run_phase1_tests.cpp -o phase1-tests && ./phase1-tests
g++ -std=c++17 -I.. run_phase2_tests.cpp -o phase2-tests && ./phase2-tests
```

## Project-Specific Patterns

### 1. Global Variable Handling
- **Pattern**: Global variables are extracted and declared in generated code automatically
- **Implementation**: `GlobalVariableCollector` in `function_analyzer.cpp` tracks reads/writes
- **Generated code**: Uses `extern bool enableLoopParallelization;` pattern
- **Example**: See how `enableLoopParallelization` is declared externally in `mpi_parallelizer_new.cpp:50,57`

### 2. Loop Parallelization Decision Logic
**88-94% parallelization rate achieved by detecting these exclusions**:
- `has_break_continue` - Loops with break/continue are NOT parallelized
- `has_complex_condition` - Loops with `&&` or `||` in condition avoided
- `has_thread_unsafe_calls` - Detects `rand()`, `srand()`, non-thread-safe functions
- `has_io_operations` - Excludes I/O loops
- **Location**: `loop_analyzer.cpp` - `ComprehensiveLoopAnalyzer::VisitStmt()`

### 3. Dependency Graph Building
- **Algorithm**: Topological sort with cycle detection (lines 90-180 in `hybrid_parallelizer.cpp`)
- **Grouping**: Independent function calls → same MPI process (Group 0, 1, 2...)
- **Visualization**: Automatically generates D3.js HTML + Graphviz DOT (see `ast_consumer.cpp:150-300`)

### 4. Test Framework Pattern
- **Custom framework**: `tests/test_framework.h` with `assert_contains()`, `assert_equals()`, etc.
- **Structure**: `run_phase1_tests.cpp` includes individual test files
- **Testing generated code**: Create temp file → run parallelizer → validate output → cleanup
- **Example**: See `tests/test_function_extraction.cpp` for pattern

### 5. CMake Build System
- **Module compilation**: Add new `.cpp` to `add_executable()` in `CMakeLists.txt:14-23`
- **Required libraries**: `clangAST`, `clangFrontend`, `clangTooling` (lines 27-37)
- **LLVM libs**: Uses `llvm_map_components_to_libnames()` (line 41+)

## Key Integration Points

### Adding New Analysis Features
1. Add struct to `data_structures.h` if needed
2. Create analyzer class in new `.cpp/.h` files (inherit `RecursiveASTVisitor<>`)
3. Call from `ast_consumer.cpp:HybridParallelizerConsumer::HandleTranslationUnit()`
4. Pass results to `HybridParallelizer` constructor
5. Update CMakeLists.txt to include new source files

### Modifying Code Generation
- **MPI code**: Edit `HybridParallelizer::generateHybridMPIOpenMPCode()` (lines 400-750 in `hybrid_parallelizer.cpp`)
- **OpenMP pragmas**: Edit `generateParallelizedFunctionBody()` (lines 220-300)
- **Visualizations**: Edit `ast_consumer.cpp:90-300` for HTML/DOT generation

### Type Mapping for MPI
- **Utility**: `type_mapping.cpp` - `TypeMapper::cppTypeToMPIType()`
- **Supported**: `int→MPI_INT`, `double→MPI_DOUBLE`, `float→MPI_FLOAT`, etc.
- **Printability**: Only printable types (`int`, `double`, etc.) get automatic output in generated code

## Common Gotchas

1. **LLVM command line conflicts**: Handled by `CommandLineDisabler` hack in `mpi_parallelizer_new.cpp:16-29`
2. **Loop variable naming**: Loop analyzer tracks `loop_variable` field to avoid conflicts
3. **Function deduplication**: Phase 2 tests verify no duplicate function definitions in output
4. **Variable initialization**: Phase 1 feature extracts complete declarations including C++11 initializers (`completeDeclaration` field)
5. **Typedef extraction**: System headers filtered out, only user typedefs included (`SourceCodeContext` in `data_structures.h`)

## Reference Files for Examples

- **Comprehensive test**: `sophisticated_test.cpp` (400+ lines, 20+ functions)
- **Minimal example**: `minimal_test.cpp` (basic validation)
- **Performance tests**: `perf_hybrid.cpp`, `perf_mpi_only.cpp` (with `--no-loops`)
- **Generated example**: `build/enhanced_hybrid_mpi_openmp_output.cpp` (inspect after running tool)

## Documentation
- **README.md**: User-facing features and quick start
- **CLAUDE.md**: Original AI agent instructions (less comprehensive)
- **tests/README.md**: Phase 1/2 test documentation
- **TEST_RESULTS.md**: Validation results and metrics
