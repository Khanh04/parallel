#include "test_framework.h"

class FunctionExtractionTests {
private:
    TestFramework& framework;
    
public:
    FunctionExtractionTests(TestFramework& tf) : framework(tf) {}
    
    void test_simple_function_extraction() {
        std::string testCode = R"(
#include <iostream>

int add(int a, int b) {
    return a + b;
}

double multiply(double x, double y) {
    double result = x * y;
    return result;
}

int main() {
    int sum = add(5, 3);
    double product = multiply(2.5, 4.0);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_simple_functions.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test that both functions are extracted with complete bodies
        framework.assert_contains(output, "int add(int a, int b)", 
                                 "Simple function signature extracted");
        framework.assert_contains(output, "return a + b;", 
                                 "Simple function body extracted");
        
        framework.assert_contains(output, "double multiply(double x, double y)", 
                                 "Function with local variables extracted");
        framework.assert_contains(output, "double result = x * y;", 
                                 "Local variable declaration extracted");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_complex_function_with_loops() {
        std::string testCode = R"(
#include <vector>

int factorial(int n) {
    if (n <= 1) return 1;
    int result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

void process_array(std::vector<int>& arr) {
    for (size_t i = 0; i < arr.size(); i++) {
        arr[i] = arr[i] * 2 + 1;
    }
    
    // Nested loops
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (i == j) continue;
            // Some processing
        }
    }
}

int main() {
    int fact = factorial(5);
    std::vector<int> data = {1, 2, 3, 4, 5};
    process_array(data);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_complex_functions.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test factorial function extraction
        framework.assert_contains(output, "int factorial(int n)", 
                                 "Complex function signature extracted");
        framework.assert_contains(output, "if (n <= 1) return 1;", 
                                 "Conditional logic extracted");
        framework.assert_contains(output, "for (int i = 2; i <= n; i++)", 
                                 "For loop in function extracted");
        
        // Test function with references and complex loops
        framework.assert_contains(output, "void process_array(std::vector<int> & arr)", 
                                 "Function with reference parameter");
        framework.assert_contains(output, "arr[i] = arr[i] * 2 + 1;", 
                                 "Complex array operation extracted");
        framework.assert_contains(output, "for (int j = 0; j < 3; j++)", 
                                 "Nested loop extracted");
        framework.assert_contains(output, "if (i == j) continue;", 
                                 "Continue statement extracted");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_function_with_complex_types() {
        std::string testCode = R"(
#include <map>
#include <string>
#include <vector>

std::map<std::string, int> create_word_count(const std::vector<std::string>& words) {
    std::map<std::string, int> counts;
    for (const auto& word : words) {
        counts[word]++;
    }
    return counts;
}

template<typename T>
T find_maximum(const std::vector<T>& vec) {
    if (vec.empty()) return T{};
    T max_val = vec[0];
    for (size_t i = 1; i < vec.size(); i++) {
        if (vec[i] > max_val) {
            max_val = vec[i];
        }
    }
    return max_val;
}

int main() {
    std::vector<std::string> words = {"hello", "world", "hello"};
    auto counts = create_word_count(words);
    
    std::vector<int> numbers = {3, 1, 4, 1, 5, 9};
    int max_num = find_maximum(numbers);
    
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_complex_types.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test STL container function extraction
        framework.assert_contains(output, "std::map<std::string, int> create_word_count", 
                                 "STL return type extracted");
        framework.assert_contains(output, "const std::vector<std::string> & words", 
                                 "Const reference parameter extracted");
        framework.assert_contains(output, "counts[word]++;", 
                                 "STL container operation extracted");
        
        // Test template function (may be challenging)
        framework.assert_contains(output, "find_maximum", 
                                 "Template function name found");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_function_with_control_structures() {
        std::string testCode = R"(
#include <iostream>

int classify_number(int n) {
    if (n < 0) {
        return -1;  // negative
    } else if (n == 0) {
        return 0;   // zero
    } else if (n < 10) {
        return 1;   // single digit positive
    } else {
        return 2;   // multi digit positive
    }
}

void print_pattern(int rows) {
    for (int i = 1; i <= rows; i++) {
        for (int j = 1; j <= i; j++) {
            std::cout << "* ";
        }
        std::cout << std::endl;
        
        if (i > 5) {
            break;  // Limit pattern size
        }
    }
}

int search_value(int arr[], int size, int target) {
    for (int i = 0; i < size; i++) {
        if (arr[i] == target) {
            return i;
        }
    }
    return -1;  // not found
}

int main() {
    int result = classify_number(15);
    print_pattern(3);
    
    int data[] = {10, 20, 30, 40, 50};
    int index = search_value(data, 5, 30);
    
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_control_structures.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test complex if-else extraction
        framework.assert_contains(output, "if (n < 0)", 
                                 "If condition extracted");
        framework.assert_contains(output, "} else if (n == 0) {", 
                                 "Else-if structure extracted");
        framework.assert_contains(output, "return -1;  // negative", 
                                 "Comments in function preserved");
        
        // Test nested loops with break
        framework.assert_contains(output, "for (int i = 1; i <= rows; i++)", 
                                 "Outer loop extracted");
        framework.assert_contains(output, "for (int j = 1; j <= i; j++)", 
                                 "Inner loop extracted");
        framework.assert_contains(output, "if (i > 5) {", 
                                 "Break condition extracted");
        framework.assert_contains(output, "break;  // Limit pattern size", 
                                 "Break statement with comment extracted");
        
        // Test array parameter function
        framework.assert_contains(output, "int search_value(int arr[], int size, int target)", 
                                 "Array parameter function extracted");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_function_completeness() {
        std::string testCode = R"(
#include <cmath>

double calculate_distance(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    return sqrt(dx*dx + dy*dy);
}

bool is_prime(int n) {
    if (n < 2) return false;
    if (n == 2) return true;
    if (n % 2 == 0) return false;
    
    for (int i = 3; i * i <= n; i += 2) {
        if (n % i == 0) {
            return false;
        }
    }
    return true;
}

int main() {
    double dist = calculate_distance(0, 0, 3, 4);
    bool prime = is_prime(17);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_completeness.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test that entire function is extracted (opening brace to closing brace)
        size_t distFuncStart = output.find("double calculate_distance");
        size_t distFuncEnd = output.find("}", distFuncStart);
        framework.assert_true(distFuncStart != std::string::npos && distFuncEnd != std::string::npos, 
                             "Complete function structure found");
        
        // Test that function body includes all statements
        std::string distFunc = output.substr(distFuncStart, distFuncEnd - distFuncStart + 1);
        framework.assert_contains(distFunc, "double dx = x2 - x1;", 
                                 "First statement in function");
        framework.assert_contains(distFunc, "double dy = y2 - y1;", 
                                 "Second statement in function");
        framework.assert_contains(distFunc, "return sqrt(dx*dx + dy*dy);", 
                                 "Return statement in function");
        
        // Test complex function with multiple returns
        framework.assert_contains(output, "if (n < 2) return false;", 
                                 "Early return extracted");
        framework.assert_contains(output, "for (int i = 3; i * i <= n; i += 2)", 
                                 "Complex loop condition extracted");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void run_all_tests() {
        framework.run_test("Simple Function Extraction", 
                          [this]() { test_simple_function_extraction(); });
        
        framework.run_test("Complex Function with Loops", 
                          [this]() { test_complex_function_with_loops(); });
        
        framework.run_test("Function with Complex Types", 
                          [this]() { test_function_with_complex_types(); });
        
        framework.run_test("Function with Control Structures", 
                          [this]() { test_function_with_control_structures(); });
        
        framework.run_test("Function Completeness", 
                          [this]() { test_function_completeness(); });
    }
};