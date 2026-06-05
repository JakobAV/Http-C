#!/bin/bash
mkdir -p ./build
gcc -g3 -Wall -Wextra -pedantic -march=native -std=c11 -o ./build/Http-C main.c
