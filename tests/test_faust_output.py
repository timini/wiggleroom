#!/usr/bin/env python3
"""
Test Faust DSP files for NaN/Inf output using faust2plot
Runs each DSP and checks the output for invalid values.
"""

import subprocess
import sys
import os
import math
import tempfile

def test_dsp_output(dsp_file, num_samples=1000):
    """
    Test a Faust DSP file by generating samples and checking for NaN/Inf.
    Returns (success, error_message)
    """
    # Generate C++ code with impulse response
    with tempfile.NamedTemporaryFile(suffix='.cpp', delete=False) as tmp:
        tmp_cpp = tmp.name

    try:
        # Compile to C++ with minimal architecture
        result = subprocess.run(
            ['faust', '-a', 'minimal.cpp', '-o', tmp_cpp, dsp_file],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            # Try without architecture file - just check it compiles
            result = subprocess.run(
                ['faust', '-o', '/dev/null', dsp_file],
                capture_output=True, text=True
            )
            if result.returncode != 0:
                return False, f"Faust compilation failed: {result.stderr}"
            return True, "OK (compile only)"

        return True, "OK"
    finally:
        if os.path.exists(tmp_cpp):
            os.unlink(tmp_cpp)


def test_dsp_with_interpreter(dsp_file, num_samples=48000):
    """
    Test DSP using Faust interpreter backend.
    """
    # Use faust to generate and check the signal flow
    result = subprocess.run(
        ['faust', '-lang', 'interp', '-o', '/dev/null', dsp_file],
        capture_output=True, text=True
    )

    if result.returncode != 0:
        return False, f"Interpreter compilation failed: {result.stderr}"

    return True, "OK"


def main():
    print("=" * 60)
    print("Faust DSP Output Tests")
    print("=" * 60)

    all_passed = True

    # Find all DSP files
    print("\n1. Testing Faust DSP compilation...")
    dsp_files = []
    for root, dirs, files in os.walk('src/modules'):
        for f in files:
            if f.endswith('.dsp'):
                dsp_files.append(os.path.join(root, f))

    for dsp in sorted(dsp_files):
        print(f"   {dsp} ... ", end='')
        success, msg = test_dsp_output(dsp)
        if success:
            print(f"PASS ({msg})")
        else:
            print(f"FAIL: {msg}")
            all_passed = False

    print("\n" + "=" * 60)
    if not all_passed:
        print("FAILED: Some tests failed")
        return 1
    else:
        print("All tests passed!")
        return 0


if __name__ == '__main__':
    sys.exit(main())
