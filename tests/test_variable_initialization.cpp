#include "test_framework.h"

class VariableInitializationTests {
private:
    TestFramework& framework;
    
public:
    VariableInitializationTests(TestFramework& tf) : framework(tf) {}
    
    void test_simple_variable_initialization() {
        std::string testCode = R"(
#include <iostream>

int simple_function(int x) {
    return x * 2;
}

int main() {
    int a = 5;
    double b = 3.14;
    char c = 'A';
    bool flag = true;
    
    int result = simple_function(a);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_simple_vars.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test that simple initializations are preserved correctly
        framework.assert_contains(output, "int a = 5;", 
                                 "Integer initialization preserved");
        framework.assert_contains(output, "double b = 3.14;", 
                                 "Double initialization preserved");
        framework.assert_contains(output, "char c = 'A';", 
                                 "Character initialization preserved");
        framework.assert_contains(output, "bool flag = true;", 
                                 "Boolean initialization preserved");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_cpp11_initializer_lists() {
        std::string testCode = R"(
#include <vector>
#include <map>
#include <string>

void process_data() {
    // Simple processing
}

int main() {
    std::vector<int> numbers = {1, 2, 3, 4, 5};
    std::vector<std::string> words = {"hello", "world", "test"};
    std::map<std::string, int> scores = {{"alice", 95}, {"bob", 87}, {"charlie", 92}};
    
    int arr[] = {10, 20, 30, 40};
    double matrix[2][2] = {{1.0, 2.0}, {3.0, 4.0}};
    
    process_data();
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_initializer_lists.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test C++11 initializer list preservation
        framework.assert_contains(output, "{1, 2, 3, 4, 5}", 
                                 "Vector initializer list preserved");
        framework.assert_contains(output, "{\"hello\", \"world\", \"test\"}", 
                                 "String vector initializer list preserved");
        framework.assert_contains(output, "{{\"alice\", 95}, {\"bob\", 87}, {\"charlie\", 92}}", 
                                 "Map initializer list preserved");
        
        // Test array initializations
        framework.assert_contains(output, "int arr[] = {10, 20, 30, 40};", 
                                 "Array initializer list preserved");
        framework.assert_contains(output, "{{1.0, 2.0}, {3.0, 4.0}}", 
                                 "2D array initializer preserved");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_constructor_initialization() {
        std::string testCode = R"(
#include <vector>
#include <string>

void process_containers() {
    // Processing logic
}

int main() {
    std::vector<int> vec1(10, 5);  // 10 elements, all set to 5
    std::vector<double> vec2(100);  // 100 elements, default initialized
    std::string str1(20, 'x');     // 20 'x' characters
    std::string str2("hello world");
    
    const int SIZE = 1000;
    std::vector<int> large_vec(SIZE, 0);
    
    process_containers();
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_constructor_init.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test constructor-style initializations
        framework.assert_contains(output, "std::vector<int> vec1(10, 5);", 
                                 "Vector constructor initialization preserved");
        framework.assert_contains(output, "std::vector<double> vec2(100);", 
                                 "Single argument constructor preserved");
        framework.assert_contains(output, "std::string str1(20, 'x');", 
                                 "String constructor with char preserved");
        framework.assert_contains(output, "std::string str2(\"hello world\");", 
                                 "String constructor with literal preserved");
        
        // Test const variable initialization
        framework.assert_contains(output, "const int SIZE = 1000;", 
                                 "Const variable initialization preserved");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_complex_expressions() {
        std::string testCode = R"(
#include <cmath>

double compute_value(double x) {
    return x * x + 2 * x + 1;
}

int main() {
    double a = 5.0;
    double b = sqrt(16.0);
    double c = compute_value(3.0) + sin(3.14159/2);
    int x = static_cast<int>(c);
    
    auto lambda = [](int n) { return n * n; };
    int squared = lambda(5);
    
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_complex_expr.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test function call initialization
        framework.assert_contains(output, "double b = sqrt(16.0);", 
                                 "Function call initialization preserved");
        framework.assert_contains(output, "compute_value(3.0) + sin(3.14159/2)", 
                                 "Complex expression initialization preserved");
        
        // Test type casting
        framework.assert_contains(output, "int x = static_cast<int>(c);", 
                                 "Static cast initialization preserved");
        
        // Test lambda (may be challenging)
        framework.assert_contains(output, "auto lambda", 
                                 "Lambda variable found");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_default_and_empty_initialization() {
        std::string testCode = R"(
#include <vector>
#include <map>

void use_containers() {
    // Use the containers
}

int main() {
    int a;                    // Uninitialized
    int b = 0;               // Explicit zero
    std::vector<int> vec;    // Default constructor
    std::map<int, int> map{};  // Empty initializer list
    
    double* ptr = nullptr;   // Null pointer
    bool flag = false;       // Explicit false
    
    use_containers();
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_default_init.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test various initialization patterns
        framework.assert_contains(output, "int a;", 
                                 "Uninitialized variable preserved");
        framework.assert_contains(output, "int b = 0;", 
                                 "Explicit zero initialization preserved");
        framework.assert_contains(output, "std::vector<int> vec;", 
                                 "Default constructor preserved");
        framework.assert_contains(output, "std::map<int, int> map{};", 
                                 "Empty initializer list preserved");
        framework.assert_contains(output, "double* ptr = nullptr;", 
                                 "Nullptr initialization preserved");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_variable_order_preservation() {
        std::string testCode = R"(
#include <iostream>

void process() {
    // Processing
}

int main() {
    const int FIRST = 10;      // Line ~10
    double second = 3.14;      // Line ~11
    auto third = FIRST * 2;    // Line ~12
    std::string fourth = "test"; // Line ~13
    bool fifth = true;         // Line ~14
    
    // Variables should maintain source order
    process();
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_variable_order.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Find positions of variable declarations
        size_t firstPos = output.find("const int FIRST = 10;");
        size_t secondPos = output.find("double second = 3.14;");
        size_t thirdPos = output.find("auto third = FIRST * 2;");
        size_t fourthPos = output.find("std::string fourth = \"test\";");
        size_t fifthPos = output.find("bool fifth = true;");
        
        // Test that order is preserved
        framework.assert_true(firstPos < secondPos, 
                             "Variable declaration order preserved (1st < 2nd)");
        framework.assert_true(secondPos < thirdPos, 
                             "Variable declaration order preserved (2nd < 3rd)");
        framework.assert_true(thirdPos < fourthPos, 
                             "Variable declaration order preserved (3rd < 4th)");
        framework.assert_true(fourthPos < fifthPos, 
                             "Variable declaration order preserved (4th < 5th)");
        
        // Test auto keyword preservation
        framework.assert_contains(output, "auto third = FIRST * 2;", 
                                 "Auto keyword preserved in initialization");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void run_all_tests() {
        framework.run_test("Simple Variable Initialization", 
                          [this]() { test_simple_variable_initialization(); });
        
        framework.run_test("C++11 Initializer Lists", 
                          [this]() { test_cpp11_initializer_lists(); });
        
        framework.run_test("Constructor Initialization", 
                          [this]() { test_constructor_initialization(); });
        
        framework.run_test("Complex Expressions", 
                          [this]() { test_complex_expressions(); });
        
        framework.run_test("Default and Empty Initialization", 
                          [this]() { test_default_and_empty_initialization(); });
        
        framework.run_test("Variable Order Preservation", 
                          [this]() { test_variable_order_preservation(); });
    }
};