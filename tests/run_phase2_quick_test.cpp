#include "test_framework.h"

int main() {
    std::cout << "🧪 PHASE 2 QUICK VALIDATION TEST" << std::endl;
    std::cout << "=================================" << std::endl;
    std::cout << "Testing Phase 2 core improvements" << std::endl;
    std::cout << std::endl;
    
    TestFramework framework;
    
    // Test 1: Function Deduplication
    std::cout << "📁 Testing Function Deduplication..." << std::endl;
    {
        std::string testCode = R"(
typedef double (*MathFunc)(double);

double square(double x) {
    return x * x;
}

double sum_loop(int n) {
    double sum = 0.0;
    for (int i = 1; i <= n; i++) {
        sum += i;
    }
    return sum;
}

int main() {
    MathFunc f = square;
    double result1 = f(5.0);
    double result2 = sum_loop(100);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "phase2_dedup_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Phase 2 Key Tests:
        
        // 1. Proper typedef semicolon
        framework.assert_contains(output, "typedef double (*MathFunc)(double);", 
                                "Typedef has proper semicolon");
        
        // 2. No function duplication (should only appear once)
        size_t square_count = 0;
        size_t pos = 0;
        while ((pos = output.find("double square(double x)", pos)) != std::string::npos) {
            square_count++;
            pos++;
        }
        framework.assert_true(square_count == 1, "No function duplication", 
                             "square function appears exactly once");
        
        // 3. Loop parallelization
        framework.assert_contains(output, "#pragma omp parallel for reduction(+:sum)", 
                                "Loop parallelization working");
        
        // 4. Enhanced function labels
        framework.assert_true(output.find("Enhanced function with OpenMP pragmas") != std::string::npos ||
                             output.find("Original function") != std::string::npos,
                             "Functions have proper enhancement labels");
        
        // 5. Compilation test
        std::string output_filepath = create_temp_cpp_file(output, "phase2_dedup_output.cpp");
        std::string compile_command = "cd " + std::string(std::getenv("HOME")) + "/parallel && mpicxx -std=c++17 -fopenmp " + output_filepath + " -o /tmp/phase2_dedup_test 2>&1";
        
        FILE* pipe = popen(compile_command.c_str(), "r");
        std::string compile_result;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            compile_result += buffer;
        }
        int exit_code = pclose(pipe);
        
        framework.assert_equals(0, exit_code, "Phase 2 code compiles successfully");
        framework.assert_true(compile_result.find("redefinition") == std::string::npos,
                             "No redefinition errors");
        
        remove(filepath.c_str());
        remove(output_filepath.c_str());
        remove("/tmp/phase2_dedup_test");
    }
    
    // Test 2: Complex Integration Test
    std::cout << "\n📁 Testing Complex Integration..." << std::endl;
    {
        std::string testCode = R"(
#include <iostream>
#include <vector>
#include <cmath>

typedef double (*IntegratorFunc)(double);

double polynomial(double x) {
    return x * x * x - 2 * x * x + 3 * x - 1;
}

double integrate_trapezoidal(IntegratorFunc func, double a, double b, int n) {
    double h = (b - a) / n;
    double sum = 0.0;
    
    for (int i = 0; i < n; i++) {
        double x1 = a + i * h;
        double x2 = a + (i + 1) * h;
        sum += (func(x1) + func(x2)) * h * 0.5;
    }
    
    return sum;
}

int main() {
    double result = integrate_trapezoidal(polynomial, -2.0, 2.0, 1000);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "phase2_integration_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Complex integration test
        framework.assert_contains(output, "typedef double (*IntegratorFunc)(double);", 
                                "Complex typedef extracted");
        framework.assert_contains(output, "#pragma omp parallel for reduction(+:sum)", 
                                "Integration loop parallelized");
        framework.assert_contains(output, "Enhanced function with OpenMP pragmas", 
                                "Function enhancement working");
        
        // Compilation and execution test
        std::string output_filepath = create_temp_cpp_file(output, "phase2_integration_output.cpp");
        std::string compile_command = "cd " + std::string(std::getenv("HOME")) + "/parallel && mpicxx -std=c++17 -fopenmp " + output_filepath + " -o /tmp/phase2_integration_test 2>&1";
        
        FILE* compile_pipe = popen(compile_command.c_str(), "r");
        int compile_exit = pclose(compile_pipe);
        
        framework.assert_equals(0, compile_exit, "Complex integration test compiles");
        
        // Performance test
        if (compile_exit == 0) {
            FILE* exec_pipe = popen("OMP_NUM_THREADS=4 /usr/bin/timeout 10s /usr/bin/time -f 'Time: %e' mpirun -np 1 /tmp/phase2_integration_test 2>&1", "r");
            std::string exec_result;
            char buffer[256];
            while (fgets(buffer, sizeof(buffer), exec_pipe) != nullptr) {
                exec_result += buffer;
            }
            int exec_exit = pclose(exec_pipe);
            
            framework.assert_equals(0, exec_exit, "Generated code executes successfully");
            framework.assert_contains(exec_result, "Time:", "Performance measurement available");
        }
        
        remove(filepath.c_str());
        remove(output_filepath.c_str());
        remove("/tmp/phase2_integration_test");
    }
    
    // Print results
    std::cout << "\n🎯 PHASE 2 QUICK TEST RESULTS" << std::endl;
    std::cout << "==============================" << std::endl;
    framework.print_summary();
    
    if (framework.all_passed()) {
        std::cout << "\n🎉 PHASE 2 QUICK TESTS PASSED! ✅" << std::endl;
        std::cout << "Phase 2 core improvements verified:" << std::endl;
        std::cout << "  ✅ Function deduplication working" << std::endl;
        std::cout << "  ✅ Typedef semicolon fixes working" << std::endl;
        std::cout << "  ✅ Loop parallelization enhanced" << std::endl;
        std::cout << "  ✅ Compilation and execution success" << std::endl;
        std::cout << "  ✅ Performance measurement working" << std::endl;
        std::cout << "\nPhase 2 ready for production! 🚀" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ PHASE 2 QUICK TESTS FAILED" << std::endl;
        std::cout << "Please review failures before proceeding." << std::endl;
        return 1;
    }
}