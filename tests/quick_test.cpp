#include "test_framework.h"

int main() {
    std::cout << "🧪 Quick Phase 1 Test" << std::endl;
    
    TestFramework framework;
    
    // Test the test framework itself
    framework.assert_true(true, "Framework Basic Test", "Testing that true is true");
    framework.assert_equals("hello", "hello", "String Equality Test");
    framework.assert_contains("Hello World", "World", "String Contains Test");
    
    // Test a simple typedef extraction
    std::string simpleCode = R"(
typedef int (*SimpleFunc)(int);
int double_it(int x) { return x * 2; }
int main() {
    SimpleFunc f = double_it;
    return f(5);
}
)";
    
    std::string filepath = create_temp_cpp_file(simpleCode, "quick_test_input.cpp");
    std::string output = run_parallelizer_on_file(filepath);
    
    // Basic checks
    framework.assert_contains(output, "typedef int (*SimpleFunc)(int)", "Simple Typedef Extraction");
    framework.assert_contains(output, "int double_it(int x)", "Simple Function Extraction");
    framework.assert_contains(output, "#include <mpi.h>", "MPI Header Included");
    
    // Clean up
    remove(filepath.c_str());
    
    framework.print_summary();
    return framework.all_passed() ? 0 : 1;
}