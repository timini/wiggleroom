#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${RACK_DIR:?Set RACK_DIR to a Rack SDK (Linux SDK for reviewer parity)}"
revision=e73bf44c3e49686b7495fab352d03a6c6075516b
checker="$PWD/build/tools/cppcheck-2.21.0"
if [[ ! -x "$checker/cppcheck" ]]; then
  mkdir -p "$checker"
  curl -fLsS "https://api.github.com/repos/cppcheck-opensource/cppcheck/tarball/$revision" -o build/tools/cppcheck.tar.gz
  tar -xzf build/tools/cppcheck.tar.gz --strip-components=1 -C "$checker"
  make -C "$checker" -j2 MATCHCOMPILER=yes
fi
python3 scripts/release_static_checks.py --cppcheck "$checker/cppcheck" --rack-dir "$RACK_DIR"
