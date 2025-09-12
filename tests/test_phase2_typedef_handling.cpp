#include "test_framework.h"

class Phase2TypedefHandlingTests {
private:
    TestFramework& framework;
    
public:
    Phase2TypedefHandlingTests(TestFramework& f) : framework(f) {}
    
    void test_function_pointer_typedef_semicolon() {
        std::cout << "Testing function pointer typedef semicolon..." << std::endl;
        
        std::string testCode = R"(
#include <iostream>

typedef double (*MathFunction)(double);

double square(double x) {
    return x * x;
}

double cube(double x) {
    return x * x * x;
}

int main() {
    MathFunction func = square;
    double result = func(5.0);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "typedef_semicolon_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Phase 2 fix: Should have proper semicolon
        framework.assert_contains(output, "typedef double (*MathFunction)(double);", 
                                "Function pointer typedef has proper semicolon");
        
        // Should NOT have incomplete typedef
        framework.assert_not_contains(output, "typedef double (*MathFunction)(double)", 
                                    "No incomplete typedef without semicolon", true);
        
        // Should be in the correct section
        framework.assert_contains(output, "// Type definitions from original source", 
                                "Typedef is in correct section");
        
        remove(filepath.c_str());
    }
    
    void test_complex_typedef_extraction() {
        std::cout << "Testing complex typedef extraction..." << std::endl;
        
        std::string testCode = R"(
#include <vector>
#include <map>

typedef std::vector<double> DoubleVector;
typedef std::map<std::string, DoubleVector> DataMap;
typedef int (*ComparisonFunc)(const void*, const void*);

void process_data(DataMap& data) {
    for (auto& pair : data) {
        DoubleVector& vec = pair.second;
        for (size_t i = 0; i < vec.size(); i++) {
            vec[i] *= 2.0;
        }
    }
}

int main() {
    DataMap data;
    process_data(data);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "complex_typedef_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // All typedefs should be extracted with semicolons
        framework.assert_contains(output, "typedef std::vector<double> DoubleVector;", 
                                "STL vector typedef extracted");
        framework.assert_contains(output, "typedef std::map<std::string, DoubleVector> DataMap;", 
                                "Complex nested typedef extracted");
        framework.assert_contains(output, "typedef int (*ComparisonFunc)(const void*, const void*);", 
                                "Function pointer typedef with parameters extracted");
        
        // Functions should use the typedefs correctly
        framework.assert_contains(output, "void process_data(DataMap& data)", 
                                "Function signature uses typedef");
        
        remove(filepath.c_str());
    }
    
    void test_using_alias_support() {
        std::cout << "Testing using alias support..." << std::endl;
        
        std::string testCode = R"(
#include <vector>

using IntVector = std::vector<int>;
using ProcessorFunc = void(*)(IntVector&);

void double_values(IntVector& vec) {
    for (size_t i = 0; i < vec.size(); i++) {
        vec[i] *= 2;
    }
}

int main() {
    IntVector numbers = {1, 2, 3, 4, 5};
    ProcessorFunc processor = double_values;
    processor(numbers);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "using_alias_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // C++11 using aliases should be extracted
        framework.assert_contains(output, "using IntVector = std::vector<int>;", 
                                "C++11 using alias extracted");
        framework.assert_contains(output, "using ProcessorFunc = void(*)(IntVector&);", 
                                "Function pointer using alias extracted");
        
        // Functions should work with aliases
        framework.assert_contains(output, "void double_values(IntVector& vec)", 
                                "Function uses type alias");
        
        remove(filepath.c_str());
    }
    
    void test_typedef_compilation() {
        std::cout << "Testing typedef compilation..." << std::endl;
        
        std::string testCode = R"(
typedef double (*UnaryFunc)(double);
typedef int (*BinaryFunc)(int, int);

double exponential(double x) {
    return x * x * x;
}

int add(int a, int b) {
    return a + b;
}

double apply_unary(UnaryFunc func, double value) {
    return func(value);
}

int main() {
    UnaryFunc f1 = exponential;
    BinaryFunc f2 = add;
    
    double result1 = apply_unary(f1, 3.0);
    int result2 = f2(5, 7);
    
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "typedef_compile_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Write output to file for compilation test
        std::string output_filepath = create_temp_cpp_file(output, "typedef_output.cpp");
        
        // Test compilation
        std::string compile_command = "cd " + std::string(std::getenv("HOME")) + "/parallel && mpicxx -std=c++17 -fopenmp " + output_filepath + " -o /tmp/typedef_test 2>&1";
        
        FILE* pipe = popen(compile_command.c_str(), "r");
        std::string compile_result;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            compile_result += buffer;
        }
        int exit_code = pclose(pipe);
        
        // Should compile successfully
        framework.assert_equals(exit_code, 0, "Code with typedefs compiles successfully");
        
        // Should not have typedef-related errors
        framework.assert_not_contains(compile_result, "was not declared", 
                                    "No typedef declaration errors");
        framework.assert_not_contains(compile_result, "expected initializer before", 
                                    "No typedef syntax errors");
        
        remove(filepath.c_str());
        remove(output_filepath.c_str());
        remove("/tmp/typedef_test");
    }
    
    void test_system_header_filtering() {
        std::cout << "Testing system header filtering..." << std::endl;
        
        std::string testCode = R"(
#include <iostream>
#include <vector>
#include <algorithm>

// User typedef should be extracted
typedef std::vector<int> IntList;

// User function
void process_list(IntList& list) {
    std::sort(list.begin(), list.end());
}

int main() {
    IntList numbers = {3, 1, 4, 1, 5};
    process_list(numbers);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "header_filter_test.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Should extract user typedef
        framework.assert_contains(output, "typedef std::vector<int> IntList;", 
                                "User typedef is extracted");
        
        // Should have proper includes  
        framework.assert_contains(output, "#include <iostream>", 
                                "System headers are preserved");
        framework.assert_contains(output, "#include <vector>", 
                                "Required headers are included");
        
        // Should NOT extract system typedefs (we can't test specific system typedefs,
        // but we can verify the structure is correct)
        size_t typedef_section_start = output.find("// Type definitions from original source");
        size_t first_function = output.find("// Original function:");
        if (typedef_section_start != std::string::npos && first_function != std::string::npos) {
            std::string typedef_section = output.substr(typedef_section_start, first_function - typedef_section_start);
            
            // Should only contain our user typedef, not system ones
            size_t user_typedef_count = count_occurrences(typedef_section, "typedef");
            framework.assert_true(user_typedef_count <= 2, // Our typedef + maybe one more reasonable one
                                 "Only user typedefs are extracted, not system ones");
        }
        
        remove(filepath.c_str());
    }
    
    void run_all_tests() {
        test_function_pointer_typedef_semicolon();
        test_complex_typedef_extraction();
        test_using_alias_support();
        test_typedef_compilation();
        test_system_header_filtering();
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