#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
faust -i -a faust/vcvrack.cpp -I src/modules/ACID9Voice/lib src/modules/ACID9Voice/acid9voice.dsp -o src/modules/ACID9Voice/acid9voice.hpp
python3 scripts/initialize_faust_members.py src/modules/ACID9Voice/acid9voice.hpp
