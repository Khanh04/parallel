# Phase 1 Unit Tests

This directory contains comprehensive unit tests for Phase 1 improvements to the MPI Parallelizer tool.

## Phase 1 Improvements Tested

### 1. **Typedef Extraction** (`test_typedef_extraction.cpp`)
- ✅ Function pointer typedefs
- ✅ Complex multi-parameter typedefs
- ✅ C++11 `using` type aliases
- ✅ Nested struct typedefs
- ✅ System header filtering (only user typedefs extracted)

### 2. **Function Body Extraction** (`test_function_extraction.cpp`)  
- ✅ Simple function signatures and bodies
- ✅ Complex functions with loops and control structures
- ✅ Functions with STL containers and complex types
- ✅ Template functions (basic support)
- ✅ Complete function structure preservation

### 3. **Variable Initialization** (`test_variable_initialization.cpp`)
- ✅ Simple variable assignments (`int a = 5;`)
- ✅ C++11 initializer lists (`{1, 2, 3, 4, 5}`)
- ✅ Constructor-style initialization (`vector<int>(10, 5)`)
- ✅ Complex expressions and function calls
- ✅ Variable declaration order preservation

### 4. **Complete Integration** (`test_phase1_integration.cpp`)
- ✅ End-to-end pipeline testing with complex_test2.cpp
- ✅ Before/after Phase 1 comparison
- ✅ Compilation readiness validation
- ✅ Performance metrics maintenance

## Running the Tests

### Quick Run
```bash
cd /home/khanh/parallel/tests
g++ -std=c++17 -I.. run_phase1_tests.cpp -o phase1-tests
./phase1-tests
```

### Using CMake (recommended)
```bash
cd /home/khanh/parallel/tests
mkdir build && cd build
cmake ..
make
make run-tests
```

### Individual Test Categories
```bash
# Test only typedef extraction
g++ -std=c++17 -I.. -DTEST_TYPEDEF_ONLY run_phase1_tests.cpp -o typedef-tests
./typedef-tests

# Test only function extraction  
g++ -std=c++17 -I.. -DTEST_FUNCTION_ONLY run_phase1_tests.cpp -o function-tests
./function-tests
```

## Test Results Interpretation

### Success Metrics
- **✅ PASS**: Feature working correctly
- **❌ FAIL**: Feature needs attention
- **⚠️ PARTIAL**: Feature partially working

### Expected Results for Phase 1
- **Typedef Extraction**: ~95% success rate (syntax edge cases may remain)
- **Function Body Extraction**: ~90% success rate (template edge cases)  
- **Variable Initialization**: ~85% success rate (complex expressions)
- **Integration Tests**: ~80% success rate (compilation readiness)

### Debugging Failed Tests

1. **Check generated output**: Failed tests show expected vs actual output
2. **Verify MPI parallelizer build**: Ensure `../build/mpi-parallelizer` exists
3. **Check temp file permissions**: Tests create files in `/tmp/` or `tests/` directory
4. **Validate test input**: Review the test case C++ code for syntax issues

## Adding New Tests

### Test Structure
```cpp
void test_new_feature() {
    std::string testCode = R"(
        // Your test C++ code here
    )";
    
    std::string filepath = create_temp_cpp_file(testCode, "test_name.cpp");
    std::string output = run_parallelizer_on_file(filepath);
    
    framework.assert_contains(output, "expected_string", "Test description");
    
    remove(filepath.c_str());
}
```

### Adding to Test Suite
1. Add test method to appropriate test class
2. Call from `run_all_tests()` method
3. Include in main test runner

## Test Coverage

### What's Tested ✅
- Core Phase 1 functionality
- Edge cases for each component
- Integration between components
- Regression prevention
- Before/after validation

### What's NOT Tested ❌
- Performance benchmarking
- Memory usage validation  
- Concurrent execution testing
- Large file processing (>10K lines)
- Cross-platform compatibility

## Continuous Integration

These tests serve as:
- **Regression Tests**: Prevent Phase 1 improvements from breaking
- **Validation Tests**: Verify new features work correctly
- **Integration Tests**: Ensure components work together
- **Quality Gates**: Block releases if core functionality breaks

## Troubleshooting

### Common Issues
1. **MPI parallelizer not found**: Ensure `build/mpi-parallelizer` is built
2. **Permission denied**: Check write permissions in test directory
3. **Compilation errors in tests**: Verify C++17 compiler support
4. **False test failures**: Check if generated code has minor formatting differences

### Debug Mode
Set `DEBUG=1` environment variable for verbose output:
```bash
DEBUG=1 ./phase1-tests
```