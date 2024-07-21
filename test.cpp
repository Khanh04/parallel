#include <mpi.h>
#include <string>
#include <vector>
#include <array>
#include <iostream>

int f(int aa, int bb){
    return aa*1000 + bb;
}

int main(){
    int a = 1, b = 2, p = 3, i;
    int result, d;
    b = a+p;
    result = f(a,b);
    for(i=0;i<3;++i){ 
        d=p+2;
    }
    b = a*p;
}