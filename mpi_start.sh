#!/bin/bash
#lamboot
rm -f b.out
mpicxx testPar.cpp -o test
if [ "$?" = "0" ]; then
  mpiexec -np 4 ./test
fi
rm -f test