#!/bin/bash
mkdir -p ./build
gcc -g3 -Og -Wall -Wextra -pedantic -march=native -std=c11 -o ./build/Http-C main.c
