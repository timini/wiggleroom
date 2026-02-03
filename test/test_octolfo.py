#!/usr/bin/env python3
"""
OctoLFO Test Suite

TDD-style tests for the OctoLFO module. These tests verify:
1. Existing functionality (regression protection)
2. New functionality (extended divisions, scope zoom)

The tests call a C++ test executable that can instantiate OctoLFO
and inspect its internal state.

Run: python3 test/test_octolfo.py
Or:  pytest test/test_octolfo.py -v
"""

import json
import subprocess
import sys
from pathlib import Path


def get_project_root() -> Path:
    """Get project root directory."""
    return Path(__file__).parent.parent


def get_test_executable() -> Path:
    """Get path to the octolfo_test executable."""
    project_root = get_project_root()
    # Try different possible locations
    candidates = [
        project_root / "build" / "test" / "octolfo_test",
        project_root / "build" / "octolfo_test",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]  # Return first candidate for error message


def run_test(args: list[str]) -> dict:
    """
    Run the octolfo_test executable with given arguments.

    Returns parsed JSON output.
    Raises AssertionError if execution fails.
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

    if result.returncode != 0:
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


def approx(value: float, expected: float, rel: float = 0.001) -> bool:
    """Check if value is approximately equal to expected."""
    if expected == 0:
        return abs(value) < rel
    return abs(value - expected) / abs(expected) < rel


# =============================================================================
# Test Classes
# =============================================================================

class TestMasterRateValues:
    """Tests for master rate division values."""

    def test_master_rate_array_exists(self):
        """Master rate values array should exist and have entries."""
        result = run_test(["--list-master-rate-values"])
        assert "master_rate_values" in result
        assert len(result["master_rate_values"]) > 0

    def test_master_rate_x1_exists(self):
        """Master rate should include x1 (1.0)."""
        result = run_test(["--list-master-rate-values"])
        values = result["master_rate_values"]
        assert any(approx(v, 1.0) for v in values), f"x1 (1.0) not found in {values}"

    def test_master_rate_division_16_exists(self):
        """Master rate should include /16."""
        result = run_test(["--list-master-rate-values"])
        values = result["master_rate_values"]
        expected = 1.0 / 16
        assert any(approx(v, expected) for v in values), f"/16 ({expected}) not found in {values}"

    def test_master_rate_multiplication_16_exists(self):
        """Master rate should include x16."""
        result = run_test(["--list-master-rate-values"])
        values = result["master_rate_values"]
        assert any(approx(v, 16.0) for v in values), f"x16 (16.0) not found in {values}"

    # New functionality tests - these will FAIL initially
    def test_master_rate_division_128_exists(self):
        """Master rate should include /128 (new feature)."""
        result = run_test(["--list-master-rate-values"])
        values = result["master_rate_values"]
        expected = 1.0 / 128
        assert any(approx(v, expected) for v in values), f"/128 ({expected}) not found in {values}"

    def test_master_rate_division_64_exists(self):
        """Master rate should include /64 (new feature)."""
        result = run_test(["--list-master-rate-values"])
        values = result["master_rate_values"]
        expected = 1.0 / 64
        assert any(approx(v, expected) for v in values), f"/64 ({expected}) not found in {values}"

    def test_master_rate_division_32_exists(self):
        """Master rate should include /32 (new feature)."""
        result = run_test(["--list-master-rate-values"])
        values = result["master_rate_values"]
        expected = 1.0 / 32
        assert any(approx(v, expected) for v in values), f"/32 ({expected}) not found in {values}"


class TestChannelRateValues:
    """Tests for channel rate division values."""

    def test_channel_rate_array_exists(self):
        """Channel rate values array should exist and have entries."""
        result = run_test(["--list-channel-rate-values"])
        assert "channel_rate_values" in result
        assert len(result["channel_rate_values"]) > 0

    def test_channel_rate_x1_exists(self):
        """Channel rate should include x1 (1.0)."""
        result = run_test(["--list-channel-rate-values"])
        values = result["channel_rate_values"]
        assert any(approx(v, 1.0) for v in values), f"x1 (1.0) not found in {values}"

    # New functionality tests - prime and odd divisions
    def test_channel_rate_division_5_exists(self):
        """Channel rate should include /5 (new feature)."""
        result = run_test(["--list-channel-rate-values"])
        values = result["channel_rate_values"]
        expected = 1.0 / 5
        assert any(approx(v, expected) for v in values), f"/5 ({expected}) not found in {values}"

    def test_channel_rate_division_7_exists(self):
        """Channel rate should include /7 (new feature)."""
        result = run_test(["--list-channel-rate-values"])
        values = result["channel_rate_values"]
        expected = 1.0 / 7
        assert any(approx(v, expected) for v in values), f"/7 ({expected}) not found in {values}"

    def test_channel_rate_division_9_exists(self):
        """Channel rate should include /9 (new feature)."""
        result = run_test(["--list-channel-rate-values"])
        values = result["channel_rate_values"]
        expected = 1.0 / 9
        assert any(approx(v, expected) for v in values), f"/9 ({expected}) not found in {values}"

    def test_channel_rate_multiplication_5_exists(self):
        """Channel rate should include x5 (new feature)."""
        result = run_test(["--list-channel-rate-values"])
        values = result["channel_rate_values"]
        assert any(approx(v, 5.0) for v in values), f"x5 (5.0) not found in {values}"

    def test_channel_rate_multiplication_7_exists(self):
        """Channel rate should include x7 (new feature)."""
        result = run_test(["--list-channel-rate-values"])
        values = result["channel_rate_values"]
        assert any(approx(v, 7.0) for v in values), f"x7 (7.0) not found in {values}"

    def test_channel_rate_multiplication_9_exists(self):
        """Channel rate should include x9 (new feature)."""
        result = run_test(["--list-channel-rate-values"])
        values = result["channel_rate_values"]
        assert any(approx(v, 9.0) for v in values), f"x9 (9.0) not found in {values}"


class TestScopeBuffer:
    """Tests for scope buffer properties."""

    def test_scope_buffer_size(self):
        """Scope buffer should be 256 samples (4x zoom)."""
        result = run_test(["--test-scope-buffer"])
        assert "buffer_size" in result
        # This test expects the NEW size (256), will FAIL initially
        assert result["buffer_size"] == 256, f"Expected 256, got {result['buffer_size']}"

    def test_scope_downsample_rate(self):
        """Scope downsampling should be 256x (4x zoom)."""
        result = run_test(["--test-scope-buffer"])
        assert "downsample_rate" in result
        # This test expects the NEW rate (256), will FAIL initially
        assert result["downsample_rate"] == 256, f"Expected 256, got {result['downsample_rate']}"


class TestCombinedRate:
    """Tests for combined rate calculation (master * channel)."""

    def test_combined_rate_x1_x1(self):
        """Master x1 and channel x1 should give combined 1.0."""
        result = run_test([
            "--test-combined-rate",
            "--master-index=11",   # x1 (index updated for new arrays)
            "--channel-index=11"   # x1
        ])
        assert approx(result["combined_rate"], 1.0)

    def test_combined_rate_x1_x2(self):
        """Master x1 and channel x2 should give combined 2.0."""
        result = run_test([
            "--test-combined-rate",
            "--master-index=11",   # x1 (index updated for new arrays)
            "--channel-index=13"   # x2
        ])
        assert approx(result["combined_rate"], 2.0)

    def test_combined_rate_x2_x2(self):
        """Master x2 and channel x2 should give combined 4.0."""
        result = run_test([
            "--test-combined-rate",
            "--master-index=13",   # x2 (index updated for new arrays)
            "--channel-index=13"   # x2
        ])
        assert approx(result["combined_rate"], 4.0)


class TestWaveforms:
    """Tests for waveform generation."""

    def test_sine_wave_at_quarter_phase(self):
        """Sine wave at phase 0.25 should be 1.0 (peak)."""
        result = run_test([
            "--test-waveform",
            "--wave=0",
            "--phase=0.25"
        ])
        assert approx(result["output"], 1.0), f"Expected 1.0, got {result['output']}"

    def test_sine_wave_at_zero_phase(self):
        """Sine wave at phase 0 should be 0.0."""
        result = run_test([
            "--test-waveform",
            "--wave=0",
            "--phase=0.0"
        ])
        assert approx(result["output"], 0.0, rel=0.01), f"Expected 0.0, got {result['output']}"

    def test_sine_wave_at_half_phase(self):
        """Sine wave at phase 0.5 should be 0.0."""
        result = run_test([
            "--test-waveform",
            "--wave=0",
            "--phase=0.5"
        ])
        assert approx(result["output"], 0.0, rel=0.01), f"Expected 0.0, got {result['output']}"

    def test_triangle_wave_at_quarter_phase(self):
        """Triangle wave at phase 0.25 should be 0.0."""
        result = run_test([
            "--test-waveform",
            "--wave=1",
            "--phase=0.25"
        ])
        assert approx(result["output"], 0.0, rel=0.01), f"Expected 0.0, got {result['output']}"

    def test_saw_up_at_half_phase(self):
        """Saw up at phase 0.5 should be 0.0."""
        result = run_test([
            "--test-waveform",
            "--wave=2",
            "--phase=0.5"
        ])
        assert approx(result["output"], 0.0, rel=0.01), f"Expected 0.0, got {result['output']}"

    def test_square_wave_at_quarter_phase(self):
        """Square wave at phase 0.25 should be -1.0."""
        result = run_test([
            "--test-waveform",
            "--wave=4",
            "--phase=0.25"
        ])
        assert approx(result["output"], -1.0), f"Expected -1.0, got {result['output']}"

    def test_square_wave_at_three_quarter_phase(self):
        """Square wave at phase 0.75 should be 1.0."""
        result = run_test([
            "--test-waveform",
            "--wave=4",
            "--phase=0.75"
        ])
        assert approx(result["output"], 1.0), f"Expected 1.0, got {result['output']}"


# =============================================================================
# Main
# =============================================================================

def run_all_tests():
    """Run all tests and report results."""
    test_classes = [
        TestMasterRateValues,
        TestChannelRateValues,
        TestScopeBuffer,
        TestCombinedRate,
        TestWaveforms,
    ]

    total = 0
    passed = 0
    failed = 0
    errors = []

    for test_class in test_classes:
        instance = test_class()
        for method_name in dir(instance):
            if method_name.startswith("test_"):
                total += 1
                test_name = f"{test_class.__name__}.{method_name}"
                try:
                    getattr(instance, method_name)()
                    passed += 1
                    print(f"  PASS: {test_name}")
                except AssertionError as e:
                    failed += 1
                    errors.append((test_name, str(e)))
                    print(f"  FAIL: {test_name}")
                except Exception as e:
                    failed += 1
                    errors.append((test_name, f"ERROR: {e}"))
                    print(f"  ERROR: {test_name}")

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
    print("OctoLFO Test Suite")
    print("="*60)

    # Check if executable exists
    exe = get_test_executable()
    if not exe.exists():
        print(f"\nERROR: Test executable not found at: {exe}")
        print("Run 'just build' first to compile the test executable.")
        sys.exit(1)

    print(f"Using test executable: {exe}\n")

    success = run_all_tests()
    sys.exit(0 if success else 1)
