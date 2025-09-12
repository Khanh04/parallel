#include "test_framework.h"
#include "test_phase2_function_deduplication.cpp"
#include "test_phase2_loop_parallelization.cpp" 
#include "test_phase2_typedef_handling.cpp"
#include "test_phase2_integration.cpp"

int main() {
    std::cout << "🧪 PHASE 2 COMPREHENSIVE TEST SUITE" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << "Testing Phase 2 major improvements:" << std::endl;
    std::cout << "✅ Function deduplication fixes" << std::endl;
    std::cout << "✅ Enhanced loop parallelization" << std::endl; 
    std::cout << "✅ Typedef semicolon and extraction" << std::endl;
    std::cout << "✅ End-to-end integration" << std::endl;
    std::cout << std::endl;
    
    TestFramework framework;
    
    // Test Phase 2 Function Deduplication
    std::cout << "📁 PHASE 2 FUNCTION DEDUPLICATION TESTS" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    {
        Phase2FunctionDeduplicationTests dedupTests(framework);
        dedupTests.run_all_tests();
    }
    
    // Test Phase 2 Loop Parallelization  
    std::cout << "\n📁 PHASE 2 LOOP PARALLELIZATION TESTS" << std::endl;
    std::cout << "--------------------------------------" << std::endl;
    {
        Phase2LoopParallelizationTests loopTests(framework);
        loopTests.run_all_tests();
    }
    
    // Test Phase 2 Typedef Handling
    std::cout << "\n📁 PHASE 2 TYPEDEF HANDLING TESTS" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    {
        Phase2TypedefHandlingTests typedefTests(framework);
        typedefTests.run_all_tests();
    }
    
    // Test Phase 2 Integration
    std::cout << "\n📁 PHASE 2 INTEGRATION TESTS" << std::endl;
    std::cout << "-----------------------------" << std::endl;
    {
        Phase2IntegrationTests integrationTests(framework);
        integrationTests.run_all_tests();
    }
    
    // Print final summary
    std::cout << "\n" << std::endl;
    std::cout << "🎯 PHASE 2 TEST RESULTS SUMMARY" << std::endl;
    std::cout << "================================" << std::endl;
    framework.print_summary();
    
    if (framework.all_passed()) {
        std::cout << "\n🎉 ALL PHASE 2 TESTS PASSED! ✅" << std::endl;
        std::cout << "Phase 2 improvements are working correctly:" << std::endl;
        std::cout << "  • Function duplication eliminated" << std::endl;
        std::cout << "  • Loop parallelization enhanced" << std::endl;
        std::cout << "  • Typedef handling fixed" << std::endl;
        std::cout << "  • End-to-end integration validated" << std::endl;
        std::cout << "  • Compilation and execution success" << std::endl;
        std::cout << std::endl;
        std::cout << "Phase 2 is ready for production! 🚀" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ SOME PHASE 2 TESTS FAILED" << std::endl;
        std::cout << "Please review the failures above and fix issues before deploying Phase 2." << std::endl;
        return 1;
    }
}