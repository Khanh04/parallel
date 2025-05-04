#include <mpi.h>
#include <string>
#include <vector>
#include <array>
#include <iostream>

int f(int aa, int bb){
    return aa*1000 + bb;
}

int main(){
    int a = 4, b = 2, p = 3, i;
    int result, d=0;
    int arr[5];
    b = a+p;
    result = f(a,b);
    for(i=1;i<5;++i){ 
        d=d+2;
        std::cout << "d =" << d << std::endl;
    }
    b = a*p;
}