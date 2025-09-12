#include "test_framework.h"

class Phase2FunctionDeduplicationTests {
private:
    TestFramework& framework;
    
public:
    Phase2FunctionDeduplicationTests(TestFramework& f) : framework(f) {}
    
    void test_no_duplicate_functions() {
        std::cout << "Testing function deduplication..." << std::endl;
        
        std::string testCode = R"(
#include <iostream>
#include <cmath>

double simple_function(double x) {
    return x * x + 2.0;
}

double another_function(int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        sum += i * i;
    }
    return sum;
}

int main() {
    double result1 = simple_function(5.0);
    double result2 = another_function(10);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "dedup_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Count occurrences of function definitions
        size_t simple_function_count = count_occurrences(output, "double simple_function(double x)");
        size_t another_function_count = count_occurrences(output, "double another_function(int n)");
        
        // Phase 2: Should have exactly one definition of each function
        framework.assert_equals(simple_function_count, 1, "simple_function appears exactly once");
        framework.assert_equals(another_function_count, 1, "another_function appears exactly once");
        
        // Should NOT have "Parallelized function:" duplicates
        framework.assert_not_contains(output, "// Parallelized function: simple_function", 
                                    "No duplicate parallelized function comment");
        
        // Should have "Original function:" or "Enhanced function:" instead
        framework.assert_true(output.find("// Original function: simple_function") != std::string::npos ||
                             output.find("// Enhanced function") != std::string::npos,
                             "Functions have proper enhancement labels");
        
        remove(filepath.c_str());
    }
    
    void test_enhanced_vs_original_functions() {
        std::cout << "Testing enhanced function generation..." << std::endl;
        
        std::string testCode = R"(
#include <iostream>

// Function with parallelizable loop
double compute_sum(int n) {
    double sum = 0.0;
    for (int i = 1; i <= n; i++) {
        sum += i * i;
    }
    return sum;
}

// Function without parallelizable loops
int simple_add(int a, int b) {
    return a + b;
}

int main() {
    double result = compute_sum(100);
    int sum = simple_add(5, 3);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "enhanced_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Function with parallelizable loop should be enhanced
        framework.assert_contains(output, "// Enhanced function with OpenMP pragmas: compute_sum", 
                                "Function with loops is enhanced");
        framework.assert_contains(output, "#pragma omp parallel for", 
                                "OpenMP pragma is added to enhanced function");
        
        // Function without loops should be original
        framework.assert_contains(output, "// Original function: simple_add", 
                                "Function without loops remains original");
        
        // No duplicate functions
        size_t compute_sum_count = count_occurrences(output, "double compute_sum(int n)");
        framework.assert_equals(compute_sum_count, 1, "compute_sum appears exactly once");
        
        remove(filepath.c_str());
    }
    
    void test_compilation_success() {
        std::cout << "Testing Phase 2 compilation success..." << std::endl;
        
        std::string testCode = R"(
typedef int (*SimpleFunc)(int);

int double_value(int x) {
    return x * 2;
}

double reduction_loop(int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        sum += i + 1.0;
    }
    return sum;
}

int main() {
    SimpleFunc func = double_value;
    int doubled = func(21);
    double total = reduction_loop(100);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "compile_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Write output to temporary file for compilation test
        std::string output_filepath = create_temp_cpp_file(output, "phase2_output.cpp");
        
        // Try to compile the generated code
        std::string compile_command = "cd " + std::string(std::getenv("HOME")) + "/parallel && mpicxx -std=c++17 -fopenmp " + output_filepath + " -o /tmp/phase2_test 2>&1";
        
        FILE* pipe = popen(compile_command.c_str(), "r");
        std::string compile_result;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            compile_result += buffer;
        }
        int exit_code = pclose(pipe);
        
        // Phase 2: Should compile successfully (exit code 0)
        framework.assert_equals(exit_code, 0, "Generated code compiles successfully");
        
        // Should not have redefinition errors
        framework.assert_not_contains(compile_result, "redefinition of", "No redefinition errors");
        framework.assert_not_contains(compile_result, "previously defined", "No duplicate definition errors");
        
        remove(filepath.c_str());
        remove(output_filepath.c_str());
        remove("/tmp/phase2_test");
    }
    
    void run_all_tests() {
        test_no_duplicate_functions();
        test_enhanced_vs_original_functions();
        test_compilation_success();
    }
    
private:
    size_t count_occurrences(const std::string& text, const std::string& pattern) {
        size_t count = 0;
        size_t pos = 0;
        while ((pos = text.find(pattern, pos)) != std::string::npos) {
            count++;
            pos += pattern.length();
        }
        return count;
    }
};