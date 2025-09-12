#include "test_framework.h"
#include <fstream>

class TypedefExtractionTests {
private:
    TestFramework& framework;
    
public:
    TypedefExtractionTests(TestFramework& tf) : framework(tf) {}
    
    void test_simple_function_pointer_typedef() {
        std::string testCode = R"(
#include <iostream>

typedef int (*SimpleFunction)(int);

int square(int x) {
    return x * x;
}

int main() {
    SimpleFunction func = square;
    int result = func(5);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_simple_typedef.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test that the typedef is extracted and included
        framework.assert_contains(output, "typedef int (*SimpleFunction)(int)", 
                                 "Simple function pointer typedef extraction");
        
        // Test that the function body is also extracted
        framework.assert_contains(output, "int square(int x)", 
                                 "Function using typedef is extracted");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_complex_function_pointer_typedef() {
        std::string testCode = R"(
#include <vector>

typedef double (*MathOperation)(double, double);
typedef void (*VoidCallback)(int);

double add(double a, double b) {
    return a + b;
}

void print_number(int n) {
    printf("%d\n", n);
}

int main() {
    MathOperation op = add;
    VoidCallback cb = print_number;
    double result = op(3.14, 2.71);
    cb(42);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_complex_typedef.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test that both typedefs are extracted
        framework.assert_contains(output, "typedef double (*MathOperation)(double, double)", 
                                 "Complex function pointer typedef extraction");
        framework.assert_contains(output, "typedef void (*VoidCallback)(int)", 
                                 "Void callback typedef extraction");
        
        // Test that functions are properly extracted
        framework.assert_contains(output, "double add(double a, double b)", 
                                 "Function with complex typedef extracted");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_using_type_alias() {
        std::string testCode = R"(
#include <functional>
#include <vector>

using IntProcessor = std::function<int(int)>;
using StringVector = std::vector<std::string>;

int double_value(int x) {
    return x * 2;
}

int main() {
    IntProcessor processor = double_value;
    StringVector strings = {"hello", "world"};
    int result = processor(21);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_using_alias.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test that using declarations are extracted
        framework.assert_contains(output, "using IntProcessor", 
                                 "Using type alias extraction");
        framework.assert_contains(output, "using StringVector", 
                                 "Using STL alias extraction");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_nested_typedefs() {
        std::string testCode = R"(
#include <iostream>

typedef struct {
    int x, y;
} Point;

typedef Point (*PointGenerator)(int);

Point make_point(int value) {
    Point p;
    p.x = value;
    p.y = value * 2;
    return p;
}

int main() {
    PointGenerator gen = make_point;
    Point p = gen(10);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_nested_typedef.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test struct typedef extraction
        framework.assert_contains(output, "typedef struct", 
                                 "Struct typedef extraction");
        framework.assert_contains(output, "} Point", 
                                 "Named struct typedef");
        
        // Test function pointer using the struct typedef
        framework.assert_contains(output, "typedef Point (*PointGenerator)(int)", 
                                 "Nested typedef extraction");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void test_no_system_header_typedefs() {
        std::string testCode = R"(
#include <iostream>
#include <vector>
#include <functional>

// This should be extracted (user typedef)
typedef int (*UserCallback)(int);

int process(int x) {
    return x + 1;
}

int main() {
    UserCallback cb = process;
    std::vector<int> vec = {1, 2, 3};  // std::vector should NOT be extracted
    int result = cb(42);
    return 0;
}
)";
        
        std::string filepath = create_temp_cpp_file(testCode, "test_system_filter.cpp");
        std::string output = run_parallelizer_on_file(filepath);
        
        // Test that user typedef is extracted
        framework.assert_contains(output, "typedef int (*UserCallback)(int)", 
                                 "User typedef is extracted");
        
        // Test that system typedefs are NOT duplicated
        framework.assert_not_contains(output, "typedef std::vector", 
                                     "System typedefs not extracted");
        
        // Clean up
        remove(filepath.c_str());
    }
    
    void run_all_tests() {
        framework.run_test("Simple Function Pointer Typedef", 
                          [this]() { test_simple_function_pointer_typedef(); });
        
        framework.run_test("Complex Function Pointer Typedef", 
                          [this]() { test_complex_function_pointer_typedef(); });
        
        framework.run_test("Using Type Alias", 
                          [this]() { test_using_type_alias(); });
        
        framework.run_test("Nested Typedefs", 
                          [this]() { test_nested_typedefs(); });
        
        framework.run_test("System Header Filtering", 
                          [this]() { test_no_system_header_typedefs(); });
    }
};