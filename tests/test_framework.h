#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <cstdlib>

// Simple test framework for Phase 1 validation
class TestFramework {
private:
    struct TestResult {
        std::string name;
        bool passed;
        std::string message;
    };
    
    std::vector<TestResult> results;
    int totalTests = 0;
    int passedTests = 0;
    
public:
    // Test assertion macros
    void assert_true(bool condition, const std::string& testName, const std::string& message = "") {
        totalTests++;
        TestResult result;
        result.name = testName;
        result.passed = condition;
        result.message = message;
        
        if (condition) {
            passedTests++;
            std::cout << "✅ PASS: " << testName << std::endl;
        } else {
            std::cout << "❌ FAIL: " << testName;
            if (!message.empty()) {
                std::cout << " - " << message;
            }
            std::cout << std::endl;
        }
        
        results.push_back(result);
    }
    
    void assert_equals(const std::string& expected, const std::string& actual, 
                      const std::string& testName, const std::string& message = "") {
        bool equal = (expected == actual);
        std::string fullMessage = message;
        if (!equal && fullMessage.empty()) {
            fullMessage = "Expected: '" + expected + "', Got: '" + actual + "'";
        }
        assert_true(equal, testName, fullMessage);
    }
    
    void assert_contains(const std::string& text, const std::string& substring, 
                        const std::string& testName, const std::string& message = "") {
        bool contains = (text.find(substring) != std::string::npos);
        std::string fullMessage = message;
        if (!contains && fullMessage.empty()) {
            fullMessage = "Text doesn't contain: '" + substring + "'";
        }
        assert_true(contains, testName, fullMessage);
    }
    
    void assert_not_contains(const std::string& text, const std::string& substring, 
                            const std::string& testName, const std::string& message = "") {
        bool notContains = (text.find(substring) == std::string::npos);
        std::string fullMessage = message;
        if (!notContains && fullMessage.empty()) {
            fullMessage = "Text unexpectedly contains: '" + substring + "'";
        }
        assert_true(notContains, testName, fullMessage);
    }
    
    // Run a test function
    void run_test(const std::string& testName, std::function<void()> testFunction) {
        std::cout << "\n🧪 Running: " << testName << std::endl;
        try {
            testFunction();
        } catch (const std::exception& e) {
            assert_true(false, testName, "Exception: " + std::string(e.what()));
        }
    }
    
    // Print final results
    void print_summary() {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "📊 TEST SUMMARY" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "Total Tests: " << totalTests << std::endl;
        std::cout << "Passed: " << passedTests << " ✅" << std::endl;
        std::cout << "Failed: " << (totalTests - passedTests) << " ❌" << std::endl;
        
        double successRate = totalTests > 0 ? (double)passedTests / totalTests * 100.0 : 0.0;
        std::cout << "Success Rate: " << successRate << "%" << std::endl;
        
        if (passedTests == totalTests) {
            std::cout << "\n🎉 ALL TESTS PASSED!" << std::endl;
        } else {
            std::cout << "\n⚠️  Some tests failed. See details above." << std::endl;
        }
        std::cout << std::string(60, '=') << std::endl;
    }
    
    bool all_passed() const {
        return passedTests == totalTests;
    }
};

// Helper function to create temporary test files
std::string create_temp_cpp_file(const std::string& content, const std::string& filename) {
    std::string filepath = "/home/khanh/parallel/tests/" + filename;
    std::ofstream file(filepath);
    file << content;
    file.close();
    return filepath;
}

// Helper function to run MPI parallelizer on a test file
std::string run_parallelizer_on_file(const std::string& filepath) {
    std::string command = "cd /home/khanh/parallel && ./build/mpi-parallelizer " + filepath + " 2>/dev/null";
    system(command.c_str());
    
    // Read the generated output
    std::ifstream outputFile("/home/khanh/parallel/enhanced_hybrid_mpi_openmp_output.cpp");
    std::stringstream buffer;
    buffer << outputFile.rdbuf();
    outputFile.close();
    
    return buffer.str();
}

#endif // TEST_FRAMEWORK_H