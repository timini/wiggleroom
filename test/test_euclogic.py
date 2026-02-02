#!/usr/bin/env python3
"""
Euclogic Test Suite

TDD-style tests for the Euclogic module components:
- EuclideanEngine: Rhythm generation (Bjorklund algorithm)
- TruthTable: Logic operations with 16-state mapping
- ProbabilityGate: Probability filtering with seeded RNG

Run: python3 test/test_euclogic.py
Or:  pytest test/test_euclogic.py -v
"""

import json
import subprocess
import sys
from pathlib import Path


def get_project_root() -> Path:
    """Get project root directory."""
    return Path(__file__).parent.parent


def get_test_executable() -> Path:
    """Get path to the euclogic_test executable."""
    project_root = get_project_root()
    candidates = [
        project_root / "build" / "test" / "euclogic_test",
        project_root / "build" / "euclogic_test",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def run_test(args: list[str]) -> dict:
    """
    Run the euclogic_test executable with given arguments.
    Returns parsed JSON output.
    """
    exe = get_test_executable()
    if not exe.exists():
        raise FileNotFoundError(
            f"Test executable not found: {exe}\n"
            f"Run 'just build' first to compile the test executable."
        )

    result = subprocess.run(
        [str(exe)] + args,
        capture_output=True,
        text=True,
        timeout=30
    )

    if result.returncode != 0 and not result.stdout:
        raise AssertionError(
            f"Test executable failed with code {result.returncode}\n"
            f"stderr: {result.stderr}\n"
            f"stdout: {result.stdout}"
        )

    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as e:
        raise AssertionError(
            f"Failed to parse JSON output: {e}\n"
            f"stdout: {result.stdout}"
        )


# =============================================================================
# Euclidean Rhythm Tests
# =============================================================================

class TestEuclideanRhythms:
    """Test Euclidean rhythm generation (Bjorklund algorithm)"""

    def test_euclidean_E3_8_tresillo(self):
        """E(3,8) should produce the tresillo pattern [1,0,0,1,0,0,1,0]"""
        result = run_test(["--test-euclidean", "--steps=8", "--hits=3"])
        expected = [True, False, False, True, False, False, True, False]
        assert result["pattern"] == expected, f"Expected tresillo, got {result['pattern']}"

    def test_euclidean_E5_8_cinquillo(self):
        """E(5,8) should produce the cinquillo pattern [1,0,1,1,0,1,1,0]"""
        result = run_test(["--test-euclidean", "--steps=8", "--hits=5"])
        expected = [True, False, True, True, False, True, True, False]
        assert result["pattern"] == expected, f"Expected cinquillo, got {result['pattern']}"

    def test_euclidean_E0_8_empty(self):
        """E(0,8) should produce all zeros"""
        result = run_test(["--test-euclidean", "--steps=8", "--hits=0"])
        expected = [False] * 8
        assert result["pattern"] == expected, f"Expected all False, got {result['pattern']}"

    def test_euclidean_E8_8_full(self):
        """E(8,8) should produce all ones"""
        result = run_test(["--test-euclidean", "--steps=8", "--hits=8"])
        expected = [True] * 8
        assert result["pattern"] == expected, f"Expected all True, got {result['pattern']}"

    def test_euclidean_E1_4(self):
        """E(1,4) should produce [1,0,0,0]"""
        result = run_test(["--test-euclidean", "--steps=4", "--hits=1"])
        expected = [True, False, False, False]
        assert result["pattern"] == expected, f"Got {result['pattern']}"

    def test_euclidean_E2_4(self):
        """E(2,4) should produce [1,0,1,0]"""
        result = run_test(["--test-euclidean", "--steps=4", "--hits=2"])
        expected = [True, False, True, False]
        assert result["pattern"] == expected, f"Got {result['pattern']}"

    def test_euclidean_E4_12(self):
        """E(4,12) should distribute 4 hits evenly across 12 steps"""
        result = run_test(["--test-euclidean", "--steps=12", "--hits=4"])
        expected = [True, False, False, True, False, False, True, False, False, True, False, False]
        assert result["pattern"] == expected, f"Got {result['pattern']}"

    def test_euclidean_known_patterns(self):
        """Test all known patterns via the built-in test"""
        result = run_test(["--test-euclidean-known"])
        assert result["failed"] == 0, f"Failed patterns: {result.get('failures', [])}"

    def test_euclidean_rotation(self):
        """Rotation should shift the pattern"""
        # E(3,8) = [1,0,0,1,0,0,1,0]
        # Rotated by 1 = [0,0,1,0,0,1,0,1] (reading from index 1)
        result = run_test(["--test-euclidean", "--steps=8", "--hits=3", "--rotation=1"])
        # The pattern should start from index 1 of the original
        original = [True, False, False, True, False, False, True, False]
        expected = original[1:] + original[:1]
        assert result["pattern"] == expected, f"Expected {expected}, got {result['pattern']}"

    def test_euclidean_tick_sequence(self):
        """tick() should return correct sequence"""
        result = run_test(["--test-euclidean-tick", "--steps=8", "--hits=3", "--ticks=8"])
        expected = [True, False, False, True, False, False, True, False]
        assert result["sequence"] == expected, f"Got {result['sequence']}"

    def test_euclidean_tick_wraps(self):
        """tick() should wrap around after steps"""
        result = run_test(["--test-euclidean-tick", "--steps=4", "--hits=2", "--ticks=8"])
        # E(2,4) = [1,0,1,0], repeated twice
        expected = [True, False, True, False, True, False, True, False]
        assert result["sequence"] == expected, f"Got {result['sequence']}"


# =============================================================================
# Truth Table Tests
# =============================================================================

class TestTruthTable:
    """Test 4-input boolean logic with truth table"""

    def test_preset_pass_through(self):
        """PASS preset should output same as input"""
        for i in range(16):
            result = run_test(["--test-truth-table", "--preset=PASS", f"--inputs={i}"])
            expected = [(i & 1) != 0, (i & 2) != 0, (i & 4) != 0, (i & 8) != 0]
            assert result["outputs"] == expected, f"Input {i}: expected {expected}, got {result['outputs']}"

    def test_preset_or(self):
        """OR preset: all outputs true if any input true"""
        # Input 0 (0000) -> all false
        result = run_test(["--test-truth-table", "--preset=OR", "--inputs=0"])
        assert result["outputs"] == [False, False, False, False], f"Got {result['outputs']}"

        # Input 1 (0001) -> all true
        result = run_test(["--test-truth-table", "--preset=OR", "--inputs=1"])
        assert result["outputs"] == [True, True, True, True], f"Got {result['outputs']}"

        # Input 15 (1111) -> all true
        result = run_test(["--test-truth-table", "--preset=OR", "--inputs=15"])
        assert result["outputs"] == [True, True, True, True], f"Got {result['outputs']}"

    def test_preset_and(self):
        """AND preset: all outputs true only if all inputs true"""
        # Input 14 (1110) -> all false
        result = run_test(["--test-truth-table", "--preset=AND", "--inputs=14"])
        assert result["outputs"] == [False, False, False, False], f"Got {result['outputs']}"

        # Input 15 (1111) -> all true
        result = run_test(["--test-truth-table", "--preset=AND", "--inputs=15"])
        assert result["outputs"] == [True, True, True, True], f"Got {result['outputs']}"

    def test_preset_xor(self):
        """XOR preset: all outputs true if odd number of inputs"""
        # 1 input (odd) -> true
        result = run_test(["--test-truth-table", "--preset=XOR", "--inputs=1"])
        assert result["outputs"] == [True, True, True, True], f"Got {result['outputs']}"

        # 2 inputs (even) -> false
        result = run_test(["--test-truth-table", "--preset=XOR", "--inputs=3"])
        assert result["outputs"] == [False, False, False, False], f"Got {result['outputs']}"

        # 3 inputs (odd) -> true
        result = run_test(["--test-truth-table", "--preset=XOR", "--inputs=7"])
        assert result["outputs"] == [True, True, True, True], f"Got {result['outputs']}"

    def test_preset_majority(self):
        """MAJORITY preset: all outputs true if 2+ inputs true"""
        # 1 input -> false
        result = run_test(["--test-truth-table", "--preset=MAJORITY", "--inputs=1"])
        assert result["outputs"] == [False, False, False, False], f"Got {result['outputs']}"

        # 2 inputs -> true
        result = run_test(["--test-truth-table", "--preset=MAJORITY", "--inputs=3"])
        assert result["outputs"] == [True, True, True, True], f"Got {result['outputs']}"

    def test_randomize_seeded(self):
        """Randomize with same seed should produce same result"""
        result1 = run_test(["--test-truth-random", "--seed=12345"])
        result2 = run_test(["--test-truth-random", "--seed=12345"])
        assert result1["mapping"] == result2["mapping"], "Same seed should produce same mapping"

    def test_randomize_different_seeds(self):
        """Different seeds should produce different results"""
        result1 = run_test(["--test-truth-random", "--seed=12345"])
        result2 = run_test(["--test-truth-random", "--seed=67890"])
        assert result1["mapping"] != result2["mapping"], "Different seeds should produce different mappings"

    def test_mutate_changes_mapping(self):
        """Mutate should change 1-3 entries"""
        result = run_test(["--test-truth-mutate", "--seed=42"])
        assert 1 <= result["differences"] <= 3, f"Expected 1-3 differences, got {result['differences']}"

    def test_undo_restores_state(self):
        """Undo should restore previous state"""
        result = run_test(["--test-truth-undo"])
        assert result["restored"] == True, "Undo should restore previous state"


# =============================================================================
# Probability Gate Tests
# =============================================================================

class TestProbabilityGate:
    """Test probability-based gate with seeded RNG"""

    def test_probability_zero_always_false(self):
        """Probability 0.0 should always return false"""
        result = run_test(["--test-probability", "--prob=0.0", "--seed=42", "--trials=100"])
        assert result["true_count"] == 0, f"Expected 0, got {result['true_count']}"

    def test_probability_one_always_true(self):
        """Probability 1.0 should always return true"""
        result = run_test(["--test-probability", "--prob=1.0", "--seed=42", "--trials=100"])
        assert result["true_count"] == 100, f"Expected 100, got {result['true_count']}"

    def test_probability_half_approximately_half(self):
        """Probability 0.5 should produce ~50% true within 3 sigma"""
        result = run_test(["--test-probability", "--prob=0.5", "--seed=42", "--trials=1000"])
        assert result["within_3sigma"] == True, (
            f"Expected ~500 true, got {result['true_count']} "
            f"(stddev={result['stddev']:.2f})"
        )

    def test_determinism_same_seed(self):
        """Same seed should produce identical sequence"""
        result = run_test(["--test-probability-determinism", "--seed=42"])
        assert result["match"] == True, "Same seed should produce identical results"

    def test_different_probabilities(self):
        """Different probabilities should produce different distributions"""
        result_25 = run_test(["--test-probability", "--prob=0.25", "--seed=99", "--trials=1000"])
        result_75 = run_test(["--test-probability", "--prob=0.75", "--seed=99", "--trials=1000"])
        # 25% should have fewer true than 75%
        assert result_25["true_count"] < result_75["true_count"], (
            f"25% ({result_25['true_count']}) should be less than 75% ({result_75['true_count']})"
        )


# =============================================================================
# Main
# =============================================================================

def run_all_tests():
    """Run all tests and report results."""
    test_classes = [
        TestEuclideanRhythms,
        TestTruthTable,
        TestProbabilityGate,
    ]

    total = 0
    passed = 0
    failed = 0
    errors = []

    for test_class in test_classes:
        print(f"\n{test_class.__name__}:")
        instance = test_class()
        for method_name in dir(instance):
            if method_name.startswith("test_"):
                total += 1
                test_name = f"{test_class.__name__}.{method_name}"
                try:
                    getattr(instance, method_name)()
                    passed += 1
                    print(f"  PASS: {method_name}")
                except AssertionError as e:
                    failed += 1
                    errors.append((test_name, str(e)))
                    print(f"  FAIL: {method_name}")
                except Exception as e:
                    failed += 1
                    errors.append((test_name, f"ERROR: {e}"))
                    print(f"  ERROR: {method_name} - {e}")

    print(f"\n{'='*60}")
    print(f"Results: {passed}/{total} passed, {failed} failed")
    print('='*60)

    if errors:
        print("\nFailures:")
        for test_name, error in errors:
            print(f"\n  {test_name}:")
            print(f"    {error[:200]}...")

    return failed == 0


if __name__ == "__main__":
    print("Euclogic Test Suite")
    print("="*60)

    exe = get_test_executable()
    if not exe.exists():
        print(f"\nERROR: Test executable not found at: {exe}")
        print("Run 'just build' first to compile the test executable.")
        sys.exit(1)

    print(f"Using test executable: {exe}\n")

    success = run_all_tests()
    sys.exit(0 if success else 1)
