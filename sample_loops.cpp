#include <iostream>
#include <vector>
#include <cmath>
using namespace std;

int main() {
    const int N = 1000;
    vector<double> a(N), b(N), c(N);
    double sum = 0.0, product = 1.0;
    
    // 1. Simple independent loop - parallelizable
    for(int i = 0; i < N; i++) {
        a[i] = i * 2.0;
        b[i] = sqrt(i + 1.0);
    }
    
    // 2. Reduction loop - parallelizable with reduction
    for(int i = 0; i < N; i++) {
        sum += a[i] * b[i];
        product *= (1.0 + a[i] * 0.001);
    }
    
    // 3. Another reduction loop
    for(int i = 0; i < N; i++) {
        product *= (1.0 + a[i] * 0.001);
    }
    
    // 4. Loop with dependency - not parallelizable
    for(int i = 1; i < N; i++) {
        a[i] = a[i-1] + b[i] * 0.5;
    }
    
    // 5. Loop with I/O - not parallelizable
    for(int i = 0; i < 10; i++) {
        cout << "Value " << i << ": " << a[i] << endl;
    }
    
    // 6. Nested loop - parallelizable
    for(int i = 0; i < N; i++) {
        for(int j = 0; j < N; j++) {
            c[i] += a[i] * b[j];
        }
    }
    
    // 7. Loop with function call - check carefully
    for(int i = 0; i < N; i++) {
        c[i] = sin(a[i]) + cos(b[i]);
    }
    
    return 0;
}
