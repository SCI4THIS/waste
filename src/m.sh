#!/bin/bash

rm *.o

gcc -g -c leb128.c
gcc -g -c validate.c
gcc -g -c execute.c
gcc -g -c lookup.c
gcc -g -c dis.c
gcc -g -c instr.c
gcc -g -c main.c

gcc -g -o wasm-interp *.o
