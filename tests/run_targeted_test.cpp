#include "test_framework.h"
#include "test_typedef_extraction.cpp"

int main() {
    std::cout << "🎯 TARGETED PHASE 1 TEST" << std::endl;
    std::cout << "Testing critical typedef extraction functionality" << std::endl;
    std::cout << std::endl;
    
    TestFramework framework;
    
    // Run just the typedef tests to verify they work
    std::cout << "📁 TYPEDEF EXTRACTION TESTS" << std::endl;
    std::cout << "----------------------------" << std::endl;
    {
        TypedefExtractionTests typedefTests(framework);
        typedefTests.test_simple_function_pointer_typedef();
        typedefTests.test_complex_function_pointer_typedef();
    }
    
    framework.print_summary();
    return framework.all_passed() ? 0 : 1;
}