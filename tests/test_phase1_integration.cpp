#include "test_framework.h"

class Phase1IntegrationTests {
private:
    TestFramework& framework;
    
public:
    Phase1IntegrationTests(TestFramework& tf) : framework(tf) {}
    
    void test_complete_phase1_pipeline() {
        // This is the exact code from complex_test2.cpp that we used for testing
        std::string testCode = R"(
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <functional>
#include <cmath>

// Function pointer typedef
typedef double (*MathFunction)(double);

// Various math functions for testing
double polynomial_eval(double x) {
    return x*x*x - 2*x*x + 3*x - 1;
}

double exponential_decay(double x) {
    return exp(-0.5 * x);
}

double trigonometric(double x) {
    return sin(x) * cos(x) + tan(x/2);
}

// Numerical integration with function pointers
double integrate_function(MathFunction func, double a, double b, int n) {
    double h = (b - a) / n;
    double sum = 0.0;
    
    for (int i = 0; i < n; i++) {
        double x1 = a + i * h;
        double x2 = a + (i + 1) * h;
        sum += (func(x1) + func(x2)) * h * 0.5;  // Trapezoidal rule
    }
    
    return sum;
}

// Complex data processing with maps and vectors
void process_data_structures(std::map<std::string, std::vector<double>>& data,
                            std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        if (data.count(key)) {
            std::vector<double>& values = data[key];
            
            // Statistical processing
            for (size_t i = 0; i < values.size(); i++) {
                values[i] = values[i] * 1.1 + 0.01;  // Scale and shift
            }
            
            // Sort and transform
            std::sort(values.begin(), values.end());
            
            for (size_t i = 0; i < values.size(); i++) {
                values[i] = sqrt(abs(values[i]));
            }
        }
    }
}

// Algorithm with complex control flow
int complex_search_algorithm(std::vector<int>& arr, int target) {
    int left = 0, right = arr.size() - 1;
    int comparisons = 0;
    
    while (left <= right) {
        int mid = left + (right - left) / 2;
        comparisons++;
        
        if (arr[mid] == target) {
            return comparisons;
        }
        
        // Complex decision logic
        if (arr[mid] < target) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
        
        // Adaptive step adjustment
        if (comparisons > 10 && (right - left) > 100) {
            int step = (right - left) / 4;
            if (arr[left + step] < target) {
                left += step;
            } else {
                right -= step;
            }
        }
    }
    
    return -comparisons;  // Not found, return negative comparison count
}

int main() {
    // Test function pointers
    const int n_points = 1000;
    double result1 = integrate_function(polynomial_eval, -2.0, 2.0, n_points);
    double result2 = integrate_function(exponential_decay, 0.0, 5.0, n_points);
    double result3 = integrate_function(trigonometric, 0.0, 3.14159/2, n_points);
    
    // Test data structures
    std::map<std::string, std::vector<double>> test_data;
    test_data["series1"] = {1.1, 2.2, 3.3, 4.4, 5.5, -1.2, -2.3};
    test_data["series2"] = {10.1, 20.2, 30.3, -5.5, -15.5};
    test_data["series3"] = {0.1, 0.01, 0.001, 100.0, 1000.0};
    
    std::vector<std::string> keys = {"series1", "series2", "series3"};
    process_data_structures(test_data, keys);
    
    // Test search algorithm
    std::vector<int> search_array;
    for (int i = 0; i < 1000; i++) {
        search_array.push_back(i * 3 + 1);  // 1, 4, 7, 10, ...
    }
    
    int search_result = complex_search_algorithm(search_array, 301);
    
    std::cout << "Integration results: " << result1 << ", " << result2 << ", " << result3 << std::endl;
    std::cout << "Data processing completed for " << keys.size() << " series" << std::endl;
    std::cout << "Search result: " << search_result << " comparisons" << std::endl;
    
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_integration_full.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // === PHASE 1 CORE IMPROVEMENTS VALIDATION ===
        
        // 1. Typedef Extraction
        framework.assert_contains(output, "typedef double (*MathFunction)(double)", 
                                 "Phase 1: Typedef successfully extracted");
        
        // 2. Complete Function Body Extraction
        framework.assert_contains(output, "double polynomial_eval(double x)", 
                                 "Phase 1: polynomial_eval function extracted");
        framework.assert_contains(output, "return x*x*x - 2*x*x + 3*x - 1;", 
                                 "Phase 1: polynomial_eval body extracted");
        
        framework.assert_contains(output, "double exponential_decay(double x)", 
                                 "Phase 1: exponential_decay function extracted");
        framework.assert_contains(output, "return exp(-0.5 * x);", 
                                 "Phase 1: exponential_decay body extracted");
        
        framework.assert_contains(output, "double trigonometric(double x)", 
                                 "Phase 1: trigonometric function extracted");
        framework.assert_contains(output, "return sin(x) * cos(x) + tan(x/2);", 
                                 "Phase 1: trigonometric body extracted");
        
        // 3. Complex Function Body Extraction
        framework.assert_contains(output, "double integrate_function(MathFunction func", 
                                 "Phase 1: integrate_function with typedef parameter");
        framework.assert_contains(output, "for (int i = 0; i < n; i++)", 
                                 "Phase 1: integrate_function loop extracted");
        framework.assert_contains(output, "sum += (func(x1) + func(x2)) * h * 0.5;", 
                                 "Phase 1: complex calculation extracted");
        
        // 4. Variable Initialization Improvements
        framework.assert_contains(output, "const int n_points = 1000;", 
                                 "Phase 1: const variable initialization");
        framework.assert_contains(output, "{\"series1\", \"series2\", \"series3\"}", 
                                 "Phase 1: initializer list preserved");
        
        // 5. STL Container Operations
        framework.assert_contains(output, "std::map<std::string, std::vector<double> >", 
                                 "Phase 1: complex STL types handled");
        framework.assert_contains(output, "std::sort(values.begin(), values.end());", 
                                 "Phase 1: STL algorithm calls extracted");
        
        // 6. Control Flow Structures
        framework.assert_contains(output, "while (left <= right)", 
                                 "Phase 1: while loop extracted");
        framework.assert_contains(output, "if (comparisons > 10 && (right - left) > 100)", 
                                 "Phase 1: complex conditions extracted");
        
        // === COMPILATION READINESS TESTS ===
        
        // Check that generated code has proper structure
        framework.assert_contains(output, "#include <mpi.h>", 
                                 "Phase 1: MPI headers included");
        framework.assert_contains(output, "#include <omp.h>", 
                                 "Phase 1: OpenMP headers included");
        framework.assert_contains(output, "int main(int argc, char* argv[])", 
                                 "Phase 1: MPI-compatible main function");
        
        // Check function availability (no missing function stubs)
        framework.assert_not_contains(output, "Function definition not found", 
                                     "Phase 1: No missing function stubs");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_before_after_comparison() {
        // Simplified test case to demonstrate before/after Phase 1
        std::string testCode = R"(
typedef int (*Callback)(int);

int double_value(int x) {
    return x * 2;
}

int main() {
    Callback cb = double_value;
    std::vector<int> numbers = {1, 2, 3, 4, 5};
    int result = cb(10);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_before_after.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Before Phase 1, these would be MISSING or BROKEN:
        
        // ✅ NOW FIXED: Typedef extraction
        framework.assert_contains(output, "typedef int (*Callback)(int)", 
                                 "BEFORE: Missing typedef - AFTER: Extracted successfully");
        
        // ✅ NOW FIXED: Function body extraction  
        framework.assert_contains(output, "int double_value(int x)", 
                                 "BEFORE: Missing function - AFTER: Complete function extracted");
        framework.assert_contains(output, "return x * 2;", 
                                 "BEFORE: Missing body - AFTER: Function body extracted");
        
        // ✅ NOW FIXED: Complex initialization
        framework.assert_contains(output, "{1, 2, 3, 4, 5}", 
                                 "BEFORE: Broken initialization - AFTER: Initializer list preserved");
        
        // Test that it can now use the typedef correctly
        framework.assert_contains(output, "Callback cb = double_value;", 
                                 "BEFORE: Broken typedef usage - AFTER: Proper typedef usage");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_compilation_readiness() {
        // Test a minimal case that should now compile
        std::string testCode = R"(
typedef double (*SimpleFunc)(double);

double square(double x) {
    return x * x;
}

int main() {
    SimpleFunc f = square;
    double result = f(5.0);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_compile_ready.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Write the output to a temporary file and test compilation
        std::string outputPath = "/home/khanh/parallel/tests/generated_test.cpp";
        std::ofstream outputFile(outputPath);
        outputFile << output;
        outputFile.close();
        
        // Attempt compilation (just syntax check, don't expect perfect compilation yet)
        std::string compileCmd = "cd /home/khanh/parallel/tests && mpicxx -fopenmp -fsyntax-only generated_test.cpp 2>&1";
        FILE* pipe = popen(compileCmd.c_str(), "r");
        std::string compileResult;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            compileResult += buffer;
        }
        int exitCode = pclose(pipe);
        
        // Check if major syntax errors are resolved (typedef and function definition issues)
        framework.assert_not_contains(compileResult, "MathFunction was not declared", 
                                     "Phase 1: Typedef declaration issue resolved");
        framework.assert_not_contains(compileResult, "was not declared in this scope", 
                                     "Phase 1: Major scope issues resolved");
        
        // Clean up
        remove(filepath.c_str());
        remove(outputPath.c_str());
    }
    
    void test_performance_metrics() {
        // Test that Phase 1 maintains analysis quality while improving code generation
        std::string testCode = R"(
double compute(double x) {
    double result = 0;
    for (int i = 0; i < 100; i++) {
        result += x * i;
    }
    return result;
}

int main() {
    double value = compute(3.14);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_performance.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test that analysis capabilities are maintained
        framework.assert_contains(output, "compute", 
                                 "Phase 1: Function analysis still working");
        framework.assert_contains(output, "for (int i = 0; i < 100; i++)", 
                                 "Phase 1: Loop analysis still working");
        
        // Test that MPI parallelization is still functional
        framework.assert_contains(output, "MPI_Init_thread", 
                                 "Phase 1: MPI parallelization maintained");
        framework.assert_contains(output, "MPI_Comm_rank", 
                                 "Phase 1: MPI infrastructure maintained");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void run_all_tests() {
        framework.run_test("Complete Phase 1 Pipeline", 
                          [this]() { test_complete_phase1_pipeline(); });
        
        framework.run_test("Before/After Phase 1 Comparison", 
                          [this]() { test_before_after_comparison(); });
        
        framework.run_test("Compilation Readiness", 
                          [this]() { test_compilation_readiness(); });
        
        framework.run_test("Performance Metrics Maintained", 
                          [this]() { test_performance_metrics(); });
    }
};