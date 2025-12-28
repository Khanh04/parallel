#include <iostream>
#include <cmath>
#include <chrono>

int compute_factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

double compute_intensive_sum(int n) {
    double sum = 0.0;
    for (int i = 1; i <= n; i++) {
        sum += sqrt(i) * sin(i * 0.001) * cos(i * 0.001) + log(i);
    }
    return sum;
}

double matrix_computation(int size) {
    double trace = 0.0;
    for (int i = 0; i < size; i++) {
        trace += sqrt(i * i + 1) * exp(-i * 0.0001) + i * 0.5;
    }
    return trace;
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Execute all functions sequentially (as original program would)
    int fact_result = compute_factorial(10);
    double sum_result = compute_intensive_sum(1000000);
    double matrix_result = matrix_computation(800000);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "=== Sequential (Non-MPI) Version ===" << std::endl;
    std::cout << "Factorial result: " << fact_result << std::endl;
    std::cout << "Intensive sum: " << sum_result << std::endl;
    std::cout << "Matrix computation: " << matrix_result << std::endl;
    std::cout << "Sequential execution time: " << duration.count() << " ms" << std::endl;
    
    return 0;
}