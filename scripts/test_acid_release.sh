#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${RACK_DIR:?Set RACK_DIR to the Rack SDK}"
mkdir -p build/release-tests
c++ -std=c++17 -O3 -funsafe-math-optimizations -fno-finite-math-only -w -I"$RACK_DIR/include" -I"$RACK_DIR/dep/include" -Isrc/common -Isrc/modules/ACID9Voice test/acid9_release_test.cpp -L"$RACK_DIR" -lRack -Wl,-rpath,"$RACK_DIR" -o build/release-tests/acid9_release_test
DYLD_LIBRARY_PATH="$RACK_DIR" LD_LIBRARY_PATH="$RACK_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" build/release-tests/acid9_release_test build/release-tests/acid9-demo.wav
