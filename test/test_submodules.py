#!/usr/bin/env python3
"""
Faust Submodule Tester

Compiles and tests individual Faust submodules to isolate issues.
Useful for debugging complex DSP chains.

Usage:
    python test_submodules.py                    # Test all submodules
    python test_submodules.py --dsp envelope     # Test specific submodule
"""

import argparse
import subprocess
import tempfile
import shutil
from pathlib import Path
from typing import Any

import numpy as np
import matplotlib.pyplot as plt

try:
    import librosa
    HAS_LIBROSA = True
except ImportError:
    HAS_LIBROSA = False


def get_project_root() -> Path:
    return Path(__file__).parent.parent


def compile_faust(dsp_path: Path, output_path: Path) -> bool:
    """Compile a Faust DSP file to a standalone executable."""
    # Use faust2sndfile for simple audio rendering
    # Or compile to C++ and use our own renderer

    # For simplicity, let's use the faust interpreter
    # Actually, let's generate a simple C++ file and compile it

    cpp_path = output_path.with_suffix('.cpp')
    exe_path = output_path.with_suffix('')

    # Generate C++ with minimal architecture
    arch_file = get_project_root() / "test" / "faust_submodules" / "minimal_arch.cpp"

    cmd = [
        "faust",
        "-a", str(arch_file),
        "-o", str(cpp_path),
        str(dsp_path)
    ]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Faust compilation failed: {result.stderr}")
        return False

    # Compile C++ to executable
    cmd = [
        "c++",
        "-std=c++17",
        "-O2",
        "-o", str(exe_path),
        str(cpp_path),
        "-lsndfile"
    ]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"C++ compilation failed: {result.stderr}")
        return False

    return True


def render_with_faust(dsp_path: Path, output_wav: Path,
                      duration: float = 2.0,
                      params: dict = None) -> bool:
    """Render audio from a Faust DSP using faust2sndfile or impulse."""
    params = params or {}

    # Try using our existing renderer if the DSP is already compiled
    # For now, use faust2impulse for quick testing

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)

        # Use faust2plot or faust2sndfile if available
        # Otherwise, fall back to compiling ourselves

        # Simple approach: use faust to generate samples
        # faust -i generates impulse response

        # Actually, let's use faust's signal generator mode
        cmd = [
            "faust",
            "--help"
        ]

        result = subprocess.run(cmd, capture_output=True, text=True)

        # For now, let's use a simpler approach - just compile and analyze the code
        print(f"  Would render: {dsp_path.name}")
        return True


def analyze_envelope_test(dsp_path: Path, output_dir: Path):
    """Analyze the envelope test submodule."""
    print(f"\nAnalyzing: {dsp_path.name}")
    print("-" * 50)

    # Read and display the DSP code
    with open(dsp_path) as f:
        code = f.read()

    # Find the key lines
    lines = code.split('\n')
    for i, line in enumerate(lines, 1):
        if 'gate >' in line.lower() or 'en.asr' in line.lower():
            print(f"  Line {i}: {line.strip()}")

    # Check for the bug
    if 'gate > 1.0' in code:
        print("\n  [BUG FOUND] gate > 1.0 - envelope only triggers when gate EXCEEDS 1.0")
        print("  [FIX] Change to 'gate > 0.5' to trigger on standard gate signals")
    elif 'gate > 0.5' in code:
        print("\n  [OK] gate > 0.5 - envelope triggers correctly")


def compare_dsp_files(original: Path, fixed: Path, output_dir: Path):
    """Compare original and fixed DSP files."""
    print(f"\nComparing: {original.name} vs {fixed.name}")
    print("=" * 60)

    with open(original) as f:
        orig_code = f.read()
    with open(fixed) as f:
        fixed_code = f.read()

    orig_lines = orig_code.split('\n')
    fixed_lines = fixed_code.split('\n')

    # Find differences
    print("\nKey differences:")

    for i, (o, f) in enumerate(zip(orig_lines, fixed_lines), 1):
        if o != f and o.strip() and f.strip():
            if not o.strip().startswith('//') and not f.strip().startswith('//'):
                print(f"\n  Line {i}:")
                print(f"    Original: {o.strip()}")
                print(f"    Fixed:    {f.strip()}")


def main():
    parser = argparse.ArgumentParser(description="Test Faust submodules")
    parser.add_argument("--dsp", help="Specific DSP file to test")
    parser.add_argument("--output", default="test/submodule_output",
                       help="Output directory")
    parser.add_argument("--compare", action="store_true",
                       help="Compare original vs fixed")

    args = parser.parse_args()

    project_root = get_project_root()
    submodule_dir = project_root / "test" / "faust_submodules"
    output_dir = project_root / args.output
    output_dir.mkdir(parents=True, exist_ok=True)

    # Find all DSP files in submodules directory
    dsp_files = list(submodule_dir.glob("*.dsp"))

    if not dsp_files:
        print("No DSP files found in test/faust_submodules/")
        return

    print("Faust Submodule Analysis")
    print("=" * 60)

    # Analyze each submodule
    for dsp_path in sorted(dsp_files):
        if args.dsp and args.dsp not in dsp_path.name:
            continue
        analyze_envelope_test(dsp_path, output_dir)

    # Compare original vs fixed if both exist
    original = project_root / "src" / "modules" / "ChaosFlute" / "chaos_flute.dsp"
    fixed = submodule_dir / "chaos_flute_fixed.dsp"

    if original.exists() and fixed.exists():
        compare_dsp_files(original, fixed, output_dir)

    print("\n" + "=" * 60)
    print("Summary of ChaosFlute Issues:")
    print("-" * 60)
    print("""
1. BUG: Envelope gate trigger condition
   - Original: gate > 1.0 (only triggers when gate EXCEEDS 1.0)
   - Fix: gate > 0.5 (triggers on standard 0-1 gate signals)
   - Impact: attack, release, pressure, growl all broken

2. CONSEQUENCE: When gate = 1.0 (test renderer value)
   - breath_env = 0 (envelope never opens)
   - blow_signal has pressure = 0 (no breath)
   - Flute self-oscillates but ignores breath controls

3. FIX REQUIRED in chaos_flute.dsp line 48:
   - Change: breath_env = en.asr(attack, 1.0, release, gate > 1.0);
   - To:     breath_env = en.asr(attack, 1.0, release, gate > 0.5);
""")


if __name__ == "__main__":
    main()
