#!/bin/bash
# Builds the tests in debug and release configurations and runs both
set -e
mkdir -p ./build
gcc -g3 -Wall -Wextra -pedantic -march=native -std=c11 -pthread -o ./build/tests tests/test_main.c
gcc -O2 -DNDEBUG -Wall -Wextra -pedantic -march=native -std=c11 -pthread -o ./build/tests-release tests/test_main.c
echo "== debug"
./build/tests
echo "== release"
./build/tests-release
