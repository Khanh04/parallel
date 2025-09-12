#include "test_framework.h"

class Phase2LoopParallelizationTests {
private:
    TestFramework& framework;
    
public:
    Phase2LoopParallelizationTests(TestFramework& f) : framework(f) {}
    
    void test_reduction_loop_parallelization() {
        std::cout << "Testing reduction loop parallelization..." << std::endl;
        
        std::string testCode = R"(
double sum_squares(int n) {
    double sum = 0.0;
    for (int i = 1; i <= n; i++) {
        sum += i * i;
    }
    return sum;
}

int main() {
    double result = sum_squares(100);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "reduction_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Should detect and parallelize the reduction loop
        framework.assert_contains(output, "#pragma omp parallel for", 
                                "OpenMP pragma is generated");
        framework.assert_contains(output, "reduction(+:sum)", 
                                "Reduction clause is correctly identified");
        framework.assert_contains(output, "schedule(static)", 
                                "Schedule clause is added");
        
        // Function should be marked as enhanced
        framework.assert_contains(output, "Enhanced function with OpenMP pragmas", 
                                "Function is marked as enhanced");
        
        // Should report parallelization in analysis
        framework.assert_contains(output, "PARALLELIZED (for)", 
                                "Loop is reported as parallelized in analysis");
        
        remove(filepath.c_str());
    }
    
    void test_simple_loop_parallelization() {
        std::cout << "Testing simple loop parallelization..." << std::endl;
        
        std::string testCode = R"(
double integrate_simple(double a, double b, int n) {
    double h = (b - a) / n;
    double sum = 0.0;
    
    for (int i = 0; i < n; i++) {
        double x = a + i * h;
        sum += x * x * h;
    }
    
    return sum;
}

int main() {
    double result = integrate_simple(0.0, 1.0, 1000);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "simple_loop_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Should detect the simple loop condition and parallelize
        framework.assert_contains(output, "#pragma omp parallel for reduction(+:sum)", 
                                "Simple loop gets parallelized with reduction");
        framework.assert_contains(output, "Enhanced function with OpenMP pragmas", 
                                "Function is enhanced");
        
        // Should NOT be blocked by "complex condition"
        framework.assert_not_contains(output, "Complex loop condition - not parallelizable", 
                                    "Simple condition is not marked as complex");
        
        remove(filepath.c_str());
    }
    
    void test_complex_condition_with_reduction() {
        std::cout << "Testing complex condition with reduction..." << std::endl;
        
        std::string testCode = R"(
double conditional_sum(int n) {
    double sum = 0.0;
    for (int i = 0; i < n && sum < 1000.0; i++) {
        sum += i * 0.5;
    }
    return sum;
}

int main() {
    double result = conditional_sum(2000);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "complex_condition_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Phase 2: Complex condition with reduction should still be parallelizable
        framework.assert_contains(output, "Complex loop condition but has reduction operations", 
                                "Complex condition with reduction is recognized");
        framework.assert_contains(output, "#pragma omp parallel for reduction(+:sum)", 
                                "Loop is parallelized despite complex condition");
        
        remove(filepath.c_str());
    }
    
    void test_non_parallelizable_loops() {
        std::cout << "Testing non-parallelizable loops..." << std::endl;
        
        std::string testCode = R"(
int fibonacci_loop(int n) {
    int a = 1, b = 1;
    for (int i = 2; i < n; i++) {
        int temp = a + b;
        a = b;
        b = temp;
    }
    return b;
}

void io_loop(int n) {
    for (int i = 0; i < n; i++) {
        printf("Number: %d\n", i);
    }
}

int main() {
    int fib = fibonacci_loop(10);
    io_loop(5);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "non_parallel_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Fibonacci has loop-carried dependencies
        framework.assert_contains(output, "Has loop-carried dependencies", 
                                "Loop-carried dependencies are detected");
        framework.assert_contains(output, "NOT PARALLELIZABLE", 
                                "Dependent loops are correctly marked non-parallelizable");
        
        // I/O loop should be blocked
        framework.assert_contains(output, "Contains I/O operations", 
                                "I/O operations are detected");
        
        // Functions should remain original (not enhanced)
        framework.assert_contains(output, "Original function:", 
                                "Non-parallelizable functions remain original");
        
        remove(filepath.c_str());
    }
    
    void test_nested_loop_handling() {
        std::cout << "Testing nested loop handling..." << std::endl;
        
        std::string testCode = R"(
double matrix_sum(int size) {
    double sum = 0.0;
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            sum += i * j + 1.0;
        }
    }
    return sum;
}

int main() {
    double result = matrix_sum(100);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "nested_loop_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Should detect nested structure
        framework.assert_contains(output, "Nested loop structure detected", 
                                "Nested loops are identified");
        
        // Should parallelize the outer loop only
        framework.assert_contains(output, "#pragma omp parallel for", 
                                "Outer loop gets parallelized");
        
        // Inner loop should be marked as not parallelized (to avoid race conditions)
        framework.assert_contains(output, "Inner loop in nested structure", 
                                "Inner loop is protected from parallelization");
        
        remove(filepath.c_str());
    }
    
    void test_thread_unsafe_function_handling() {
        std::cout << "Testing thread-unsafe function handling..." << std::endl;
        
        std::string testCode = R"(
#include <cstdlib>

double random_sum(int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        sum += rand() % 100;
    }
    return sum;
}

int main() {
    double result = random_sum(1000);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "thread_unsafe_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Should detect thread-unsafe function
        framework.assert_contains(output, "Thread-unsafe functions detected", 
                                "Thread-unsafe functions are identified");
        
        // Should replace with thread-safe alternative
        framework.assert_contains(output, "rand_r(&__thread_seed)", 
                                "rand() is replaced with rand_r()");
        
        // Should still be parallelizable with fixes
        framework.assert_contains(output, "#pragma omp parallel for", 
                                "Loop is parallelized with thread-safe fixes");
        
        remove(filepath.c_str());
    }
    
    void run_all_tests() {
        test_reduction_loop_parallelization();
        test_simple_loop_parallelization();
        test_complex_condition_with_reduction();
        test_non_parallelizable_loops();
        test_nested_loop_handling();
        test_thread_unsafe_function_handling();
    }
};