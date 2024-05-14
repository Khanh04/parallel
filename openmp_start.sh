#!/bin/bash
#lamboot

#g++ -g -w -std=c++14 aPar.cpp -o aPar.out
#./aPar.out

rm -f b.out
mpiCC -fopenmp testPar.cpp -o test
#g++ -fopenmp testPar.cpp -o test
if [ "$?" = "0" ]; then
  #mpiexec -np 4 ./test
  ./test
fi
#rm -f test