#include <iostream>

// Simple functions for basic testing
int getNextValue(int input) {
    return input + 10;
}

int multiply(int a) {
    return a * 2;
}

int add(int x, int y) {
    return x + y;
}

void printHello() {
    std::cout << "Hello from function!" << std::endl;
}

int main() {
    // Test basic local variable dependencies
    int start = 5;
    int next1 = getNextValue(start);     // Call 0: depends on start
    int next2 = getNextValue(next1);     // Call 1: depends on next1 (from Call 0)
    
    // Independent operations
    int result1 = multiply(10);          // Call 2: independent
    int result2 = multiply(20);          // Call 3: independent
    
    // Dependent operation
    int final_result = add(next2, result1);  // Call 4: depends on Call 1 and Call 2
    
    // Independent void function
    printHello();                        // Call 5: independent
    
    return 0;
}