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
    int result, d;
    int arr[5];
    b = a+p;
    result = f(a,b);
    for(i=1;i<5;++i){ 
        d=p+2;
        arr[i] = arr[i-1];
        arr[i-1] = p + 1;
    }
    while (a < 10) {
        a=a+1;
    }
    b = a*p;
}