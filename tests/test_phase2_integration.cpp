#include "test_framework.h"

class Phase2IntegrationTests {
private:
    TestFramework& framework;
    
public:
    Phase2IntegrationTests(TestFramework& f) : framework(f) {}
    
    void test_complex_test2_integration() {
        std::cout << "Testing complex_test2.cpp full integration..." << std::endl;
        
        // Use the actual complex_test2.cpp content
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
    const int n_points = 1000;
    
    // Test numerical integration with different functions
    double result1 = integrate_function(polynomial_eval, -2.0, 2.0, n_points);
    double result2 = integrate_function(exponential_decay, 0.0, 5.0, n_points);
    double result3 = integrate_function(trigonometric, 0.0, 3.14159/2, n_points);
    
    // Test complex data structures
    std::map<std::string, std::vector<double>> test_data;
    test_data["series1"] = {1.0, 2.0, 3.0, 4.0, 5.0};
    test_data["series2"] = {-1.0, -2.0, 3.0, 4.0};
    test_data["series3"] = {0.5, 1.5, 2.5};
    
    std::vector<std::string> keys = {"series1", "series2", "series3"};
    process_data_structures(test_data, keys);
    
    // Test search algorithm
    std::vector<int> search_array;
    for (int i = 0; i < 1000; i++) {
        search_array.push_back(i * 3 + 1);
    }
    
    int search_result = complex_search_algorithm(search_array, 301);
    
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "complex_integration_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Phase 2 Key Requirements:
        
        // 1. No function duplication
        framework.assert_equals(count_occurrences(output, "double integrate_function(MathFunction func, double a, double b, int n)"), 1,
                              "integrate_function appears exactly once");
        framework.assert_equals(count_occurrences(output, "double polynomial_eval(double x)"), 1,
                              "polynomial_eval appears exactly once");
        
        // 2. Proper typedef semicolon
        framework.assert_contains(output, "typedef double (*MathFunction)(double);", 
                                "MathFunction typedef has proper semicolon");
        
        // 3. Loop parallelization working
        framework.assert_contains(output, "#pragma omp parallel for reduction(+:sum)", 
                                "integrate_function loop is parallelized");
        framework.assert_contains(output, "Enhanced function with OpenMP pragmas: integrate_function", 
                                "integrate_function is enhanced");
        
        // 4. Compilation success
        std::string output_filepath = create_temp_cpp_file(output, "complex_integration_output.cpp");
        std::string compile_command = "cd " + std::string(std::getenv("HOME")) + "/parallel && mpicxx -std=c++17 -fopenmp " + output_filepath + " -o /tmp/complex_integration_test 2>&1";
        
        FILE* pipe = popen(compile_command.c_str(), "r");
        std::string compile_result;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            compile_result += buffer;
        }
        int exit_code = pclose(pipe);
        
        framework.assert_equals(exit_code, 0, "Complex integration test compiles successfully");
        framework.assert_not_contains(compile_result, "redefinition", "No redefinition errors");
        
        // 5. Execution success
        if (exit_code == 0) {
            FILE* exec_pipe = popen("/usr/bin/timeout 10s /usr/bin/time -f 'Real: %e' mpirun -np 1 /tmp/complex_integration_test 2>&1", "r");
            std::string exec_result;
            while (fgets(buffer, sizeof(buffer), exec_pipe) != nullptr) {
                exec_result += buffer;
            }
            int exec_exit = pclose(exec_pipe);
            
            framework.assert_equals(exec_exit, 0, "Generated code executes successfully");
            framework.assert_contains(exec_result, "Real:", "Execution produces timing output");
        }
        
        remove(filepath.c_str());
        remove(output_filepath.c_str());
        remove("/tmp/complex_integration_test");
    }
    
    void test_before_after_comparison() {
        std::cout << "Testing Phase 1 vs Phase 2 improvements..." << std::endl;
        
        std::string testCode = R"(
typedef int (*BinaryOp)(int, int);

int add(int a, int b) {
    return a + b;
}

double parallel_sum(int n) {
    double sum = 0.0;
    for (int i = 1; i <= n; i++) {
        sum += i;
    }
    return sum;
}

int main() {
    BinaryOp op = add;
    int result1 = op(5, 3);
    double result2 = parallel_sum(100);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "before_after_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Phase 2 should resolve all Phase 1 issues:
        
        // Issue 1: Missing function definitions - RESOLVED
        framework.assert_contains(output, "int add(int a, int b)", "Function definitions present");
        
        // Issue 2: Missing type definitions - RESOLVED  
        framework.assert_contains(output, "typedef int (*BinaryOp)(int, int);", "Typedef definitions present");
        
        // Issue 3: Function duplication - RESOLVED
        framework.assert_equals(count_occurrences(output, "int add(int a, int b)"), 1,
                              "No function duplication");
        
        // Issue 4: Loop parallelization - IMPROVED
        framework.assert_contains(output, "#pragma omp parallel for reduction(+:sum)", 
                                "Loop parallelization working");
        
        // Issue 5: Compilation success - RESOLVED
        std::string output_filepath = create_temp_cpp_file(output, "before_after_output.cpp");
        std::string compile_command = "cd " + std::string(std::getenv("HOME")) + "/parallel && mpicxx -std=c++17 -fopenmp " + output_filepath + " -o /tmp/before_after_test 2>&1";
        
        FILE* pipe = popen(compile_command.c_str(), "r");
        std::string compile_result;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            compile_result += buffer;
        }
        int exit_code = pclose(pipe);
        
        framework.assert_equals(exit_code, 0, "Phase 2 resolves Phase 1 compilation issues");
        
        remove(filepath.c_str());
        remove(output_filepath.c_str());
        remove("/tmp/before_after_test");
    }
    
    void test_real_world_scenario() {
        std::cout << "Testing real-world scientific computation scenario..." << std::endl;
        
        std::string testCode = R"(
#include <vector>
#include <cmath>

typedef double (*ScientificFunc)(double, double);

// Physics simulation function
double heat_equation(double x, double t) {
    return exp(-t) * sin(x);
}

// Mathematical computation with parallelizable loops
double monte_carlo_integration(ScientificFunc func, double x_min, double x_max, 
                              double t_min, double t_max, int samples) {
    double sum = 0.0;
    double dx = (x_max - x_min) / samples;
    double dt = (t_max - t_min) / samples;
    
    // This should be parallelized
    for (int i = 0; i < samples; i++) {
        double x = x_min + i * dx;
        for (int j = 0; j < samples; j++) {
            double t = t_min + j * dt;
            sum += func(x, t) * dx * dt;
        }
    }
    
    return sum;
}

// Data processing with STL containers
void normalize_data(std::vector<double>& data) {
    double sum = 0.0;
    
    // Calculate mean
    for (size_t i = 0; i < data.size(); i++) {
        sum += data[i];
    }
    double mean = sum / data.size();
    
    // Normalize
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = (data[i] - mean) / data.size();
    }
}

int main() {
    // Scientific computation
    double result1 = monte_carlo_integration(heat_equation, 0.0, 3.14159, 0.0, 1.0, 100);
    
    // Data processing
    std::vector<double> experimental_data = {1.2, 2.3, 3.4, 4.5, 5.6, 6.7, 7.8, 8.9, 9.0};
    normalize_data(experimental_data);
    
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "real_world_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Should handle complex scientific computing patterns
        framework.assert_contains(output, "typedef double (*ScientificFunc)(double, double);", 
                                "Complex function pointer typedef extracted");
        
        // Should parallelize reduction loops in scientific functions
        framework.assert_contains(output, "#pragma omp parallel for reduction(+:sum)", 
                                "Scientific reduction loops parallelized");
        
        // Should handle nested loops appropriately
        framework.assert_contains(output, "Nested loop structure detected", 
                                "Nested scientific loops identified");
        
        // Should work with STL containers
        framework.assert_contains(output, "std::vector<double>& data", 
                                "STL containers handled correctly");
        
        // Should compile and run successfully
        std::string output_filepath = create_temp_cpp_file(output, "real_world_output.cpp");
        std::string compile_command = "cd " + std::string(std::getenv("HOME")) + "/parallel && mpicxx -std=c++17 -fopenmp " + output_filepath + " -o /tmp/real_world_test 2>&1";
        
        FILE* pipe = popen(compile_command.c_str(), "r");
        std::string compile_result;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            compile_result += buffer;
        }
        int exit_code = pclose(pipe);
        
        framework.assert_equals(exit_code, 0, "Real-world scenario compiles successfully");
        
        // Test execution with performance measurement
        if (exit_code == 0) {
            FILE* exec_pipe = popen("/usr/bin/timeout 15s /usr/bin/time -f 'Performance: %e seconds' mpirun -np 1 /tmp/real_world_test 2>&1", "r");
            std::string exec_result;
            char buffer[256];
            while (fgets(buffer, sizeof(buffer), exec_pipe) != nullptr) {
                exec_result += buffer;
            }
            int exec_exit = pclose(exec_pipe);
            
            framework.assert_equals(exec_exit, 0, "Real-world scenario executes successfully");
            framework.assert_contains(exec_result, "Performance:", "Performance measurement available");
        }
        
        remove(filepath.c_str());
        remove(output_filepath.c_str());
        remove("/tmp/real_world_test");
    }
    
    void test_performance_regression() {
        std::cout << "Testing performance regression..." << std::endl;
        
        std::string testCode = R"(
double intensive_computation(int n) {
    double result = 0.0;
    
    // This should be parallelized for performance
    for (int i = 1; i <= n; i++) {
        result += sqrt(i) * sin(i * 0.001) + cos(i * 0.002);
    }
    
    return result;
}

int main() {
    double result = intensive_computation(100000);  // Large workload
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "performance_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Should parallelize intensive computation
        framework.assert_contains(output, "#pragma omp parallel for reduction(+:result)", 
                                "Intensive computation is parallelized");
        
        // Test with and without parallelization
        std::string output_filepath = create_temp_cpp_file(output, "performance_output.cpp");
        std::string compile_command = "cd " + std::string(std::getenv("HOME")) + "/parallel && mpicxx -std=c++17 -fopenmp " + output_filepath + " -o /tmp/performance_test 2>&1";
        
        FILE* pipe = popen(compile_command.c_str(), "r");
        int exit_code = pclose(pipe);
        
        if (exit_code == 0) {
            // Test with 1 thread (sequential)
            FILE* seq_pipe = popen("OMP_NUM_THREADS=1 /usr/bin/time -f '%e' /tmp/performance_test 2>&1 | tail -1", "r");
            char seq_time_str[32];
            fgets(seq_time_str, sizeof(seq_time_str), seq_pipe);
            pclose(seq_pipe);
            
            // Test with 4 threads (parallel) 
            FILE* par_pipe = popen("OMP_NUM_THREADS=4 /usr/bin/time -f '%e' /tmp/performance_test 2>&1 | tail -1", "r");
            char par_time_str[32];
            fgets(par_time_str, sizeof(par_time_str), par_pipe);
            pclose(par_pipe);
            
            double seq_time = atof(seq_time_str);
            double par_time = atof(par_time_str);
            
            // Parallelized version should be faster (allowing some measurement noise)
            if (seq_time > 0.1 && par_time > 0.0) {  // Only test if we have meaningful measurements
                framework.assert_true(par_time <= seq_time * 1.2,  // Allow 20% measurement variance
                                     "Parallelized version is not significantly slower");
            }
        }
        
        framework.assert_equals(exit_code, 0, "Performance test compiles successfully");
        
        remove(filepath.c_str());
        remove(output_filepath.c_str());
        remove("/tmp/performance_test");
    }
    
    void run_all_tests() {
        test_complex_test2_integration();
        test_before_after_comparison();
        test_real_world_scenario();
        test_performance_regression();
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