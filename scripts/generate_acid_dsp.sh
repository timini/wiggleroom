#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
faust --version | head -1 | grep -F '2.85.9' >/dev/null || { echo 'Faust 2.85.9 is required to regenerate the release DSP.' >&2; exit 1; }
faust -i -a faust/vcvrack.cpp -I src/modules/ACID9Voice/lib src/modules/ACID9Voice/acid9voice.dsp -o src/modules/ACID9Voice/acid9voice.hpp
