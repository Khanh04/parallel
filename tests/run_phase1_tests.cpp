#include "test_framework.h"
#include "test_typedef_extraction.cpp"
#include "test_function_extraction.cpp" 
#include "test_variable_initialization.cpp"
#include "test_phase1_integration.cpp"

int main() {
    std::cout << "🧪 PHASE 1 UNIT TEST SUITE" << std::endl;
    std::cout << "==========================" << std::endl;
    std::cout << "Testing enhanced code extraction improvements" << std::endl;
    std::cout << std::endl;
    
    TestFramework framework;
    
    // Run all test suites
    std::cout << "📁 TYPEDEF EXTRACTION TESTS" << std::endl;
    std::cout << "----------------------------" << std::endl;
    {
        TypedefExtractionTests typedefTests(framework);
        typedefTests.run_all_tests();
    }
    
    std::cout << "\n📁 FUNCTION BODY EXTRACTION TESTS" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    {
        FunctionExtractionTests functionTests(framework);
        functionTests.run_all_tests();
    }
    
    std::cout << "\n📁 VARIABLE INITIALIZATION TESTS" << std::endl;
    std::cout << "---------------------------------" << std::endl;
    {
        VariableInitializationTests varTests(framework);
        varTests.run_all_tests();
    }
    
    std::cout << "\n📁 PHASE 1 INTEGRATION TESTS" << std::endl;
    std::cout << "-----------------------------" << std::endl;
    {
        Phase1IntegrationTests integrationTests(framework);
        integrationTests.run_all_tests();
    }
    
    // Print final summary
    framework.print_summary();
    
    // Return appropriate exit code
    return framework.all_passed() ? 0 : 1;
}