// test_hybrid.cpp - Example program to test the hybrid MPI/OpenMP parallelizer
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

// Global variables for demonstrating dependencies
int global_counter = 0;
double global_sum = 0.0;

// Independent function - can run in parallel
int compute_factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

// Independent function - can run in parallel
double compute_sine_sum(int iterations) {
    double sum = 0.0;
    for (int i = 0; i < iterations; i++) {
        sum += sin(i * 0.1);
    }
    return sum;
}

// Function that reads global variable
int process_with_global_read() {
    int local_value = global_counter * 2;
    for (int i = 0; i < 100; i++) {
        local_value += i;
    }
    return local_value;
}

// Function that writes to global variable
void update_global_counter(int increment) {
    global_counter += increment;
}

// Complex computation function
double matrix_computation(int size) {
    std::vector<std::vector<double>> matrix(size, std::vector<double>(size));
    
    // Initialize matrix
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            matrix[i][j] = (i + j) * 0.5;
        }
    }
    
    // Compute sum of diagonal elements
    double diag_sum = 0.0;
    for (int i = 0; i < size; i++) {
        diag_sum += matrix[i][i];
    }
    
    return diag_sum;
}

int main() {
    const int N = 10000;
    const int M = 100;
    
    // Local variables
    std::vector<double> data(N);
    std::vector<double> results(N);
    double sum = 0.0;
    double product = 1.0;
    int max_value = 0;
    
    // ==== SECTION 1: Initialization (Parallelizable loop) ====
    // This loop has no dependencies and can be parallelized
    for (int i = 0; i < N; i++) {
        data[i] = i * 0.01;
    }
    
    // ==== SECTION 2: Independent function calls (MPI parallelizable) ====
    // These function calls are independent and can run on different MPI processes
    int fact_result = compute_factorial(10);
    double sine_result = compute_sine_sum(1000);
    double matrix_result = matrix_computation(50);
    
    // ==== SECTION 3: Reduction operations (OpenMP parallelizable with reduction) ====
    // Sum reduction - parallelizable with reduction clause
    for (int i = 0; i < N; i++) {
        sum += data[i] * data[i];
    }
    
    // Product reduction - parallelizable with reduction clause
    for (int i = 0; i < M; i++) {
        product *= (1.0 + i * 0.001);
    }
    
    // Max reduction - parallelizable with reduction clause
    for (int i = 0; i < N; i++) {
        if (data[i] > max_value) {
            max_value = data[i];
        }
    }
    
    // ==== SECTION 4: Independent computations (OpenMP parallelizable) ====
    // Each iteration is independent
    for (int i = 0; i < N; i++) {
        results[i] = sin(data[i]) + cos(data[i]) * exp(-data[i]);
    }
    
    // ==== SECTION 5: Dependent function calls (Sequential execution required) ====
    // These have dependencies through global variable
    update_global_counter(5);  // Writes to global_counter
    int global_read_result = process_with_global_read();  // Reads global_counter
    
    // ==== SECTION 6: Loop with dependencies (NOT parallelizable) ====
    // This loop has loop-carried dependency and cannot be parallelized
    for (int i = 1; i < N; i++) {
        data[i] = data[i-1] + data[i] * 0.5;
    }
    
    // ==== SECTION 7: Nested loops (Outer loop parallelizable) ====
    double nested_sum = 0.0;
    for (int i = 0; i < M; i++) {
        double inner_sum = 0.0;
        for (int j = 0; j < M; j++) {
            inner_sum += (i * j) * 0.01;
        }
        nested_sum += inner_sum;  // Reduction operation
    }
    
    // ==== SECTION 8: Loop with I/O (NOT parallelizable) ====
    // This loop contains I/O operations and cannot be parallelized
    for (int i = 0; i < 5; i++) {
        std::cout << "Processing item " << i << std::endl;
    }
    
    // ==== Output Results ====
    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Factorial result: " << fact_result << std::endl;
    std::cout << "Sine sum result: " << sine_result << std::endl;
    std::cout << "Matrix result: " << matrix_result << std::endl;
    std::cout << "Sum: " << sum << std::endl;
    std::cout << "Product: " << product << std::endl;
    std::cout << "Max value: " << max_value << std::endl;
    std::cout << "Global counter: " << global_counter << std::endl;
    std::cout << "Global read result: " << global_read_result << std::endl;
    std::cout << "Nested sum: " << nested_sum << std::endl;
    std::cout << "First result: " << results[0] << std::endl;
    std::cout << "Last data: " << data[N-1] << std::endl;
    
    return 0;
}