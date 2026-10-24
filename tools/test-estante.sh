#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
mkdir -p build/tests
clang-18 -g -fsanitize=address,undefined -c src/cJSON.c -o build/tests/cjson-host.o
for suite in test_estante test_experience; do
 clang++-18 -std=c++20 -g -fsanitize=address,undefined "tests/$suite.cpp" src/library.cpp src/experience.cpp build/tests/cjson-host.o -o "build/tests/$suite"
 "build/tests/$suite"
done
clang++-18 -std=c++20 -g -fsanitize=address,undefined tests/test_emulator_import.cpp -o build/tests/test-emulator-import
build/tests/test-emulator-import
python3 tests/test_swanstation_cover_names.py
clang-18 -g -fsanitize=address,undefined tests/test_cover_decode.c src/cover_decode.c -lm -o build/tests/test-cover
build/tests/test-cover
