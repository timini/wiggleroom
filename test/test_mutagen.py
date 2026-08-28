#!/usr/bin/env python3
"""
Mutagen Test Suite

Drives the mutagen_test executable, which exercises the framework-free
sequencer core in src/common/mutagen/:

- StepAddresser: clock advance, CV addressing, broadcast follow
- StepSequence:  randomise, mutation rate, undo/redo
- BankStore:     save and recall round trip

Run: python3 test/test_mutagen.py
Or:  pytest test/test_mutagen.py -v
"""

import json
import subprocess
import sys
from pathlib import Path


def get_project_root() -> Path:
    return Path(__file__).parent.parent


def get_test_executable() -> Path:
    root = get_project_root()
    for candidate in (root / "build" / "test" / "mutagen_test",
                      root / "build" / "mutagen_test"):
        if candidate.exists():
            return candidate
    return root / "build" / "test" / "mutagen_test"


def run_test(args: list) -> dict:
    exe = get_test_executable()
    if not exe.exists():
        raise FileNotFoundError(
            f"Test executable not found: {exe}\n"
            f"Run 'just build' first to compile the test executable."
        )
    result = subprocess.run([str(exe)] + args, capture_output=True,
                            text=True, timeout=60)
    if result.returncode != 0 and not result.stdout:
        raise AssertionError(
            f"Test executable failed with code {result.returncode}\n"
            f"stderr: {result.stderr}"
        )
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as e:
        raise AssertionError(f"Failed to parse JSON output: {e}\nstdout: {result.stdout}")


def assert_clean(result: dict):
    """Every command reports its own pass/fail tally."""
    assert result["failed"] == 0, (
        f"{result['test']}: {result['failed']} failed - "
        f"{result.get('failures', [])}"
    )
    assert result["passed"] > 0, f"{result['test']} asserted nothing"


class TestAddressing:
    """The step position only ever moves on a clock tick."""

    def test_clock_advance_and_wrap(self):
        assert_clean(run_test(["--test-address-advance", "--steps=8"]))

    def test_clock_advance_odd_length(self):
        assert_clean(run_test(["--test-address-advance", "--steps=5"]))

    def test_cv_addressing(self):
        """0V picks step 0, full scale picks the last step, mapping is monotonic."""
        assert_clean(run_test(["--test-address-cv", "--steps=8"]))

    def test_cv_addressing_long_pattern(self):
        assert_clean(run_test(["--test-address-cv", "--steps=64"]))

    def test_cv_addressing_single_step(self):
        """A one-step pattern must not index out of range."""
        assert_clean(run_test(["--test-address-cv", "--steps=1"]))


class TestChain:
    """A slave lane follows the address broadcast by the master."""

    def test_equal_lengths_lockstep(self):
        assert_clean(run_test(["--test-broadcast-follow",
                               "--master-steps=8", "--slave-steps=8"]))

    def test_shorter_slave_wraps(self):
        assert_clean(run_test(["--test-broadcast-follow",
                               "--master-steps=8", "--slave-steps=5"]))

    def test_longer_slave_wraps(self):
        assert_clean(run_test(["--test-broadcast-follow",
                               "--master-steps=5", "--slave-steps=16"]))

    def test_message_survives_relay(self):
        assert_clean(run_test(["--test-chain-relay", "--hops=4"]))


class TestMutation:
    """The mutation rate is the probability of one step changing per clock."""

    def test_rate_zero_never_fires(self):
        assert_clean(run_test(["--test-mutation-bounds", "--steps=8"]))

    def test_observed_rate_matches_requested(self):
        """Over 20k ticks the observed rate should track the knob closely."""
        for rate in (0.1, 0.25, 0.5, 0.9):
            result = run_test(["--test-mutation-rate", f"--rate={rate}",
                               "--ticks=20000", "--seed=42"])
            observed = result["observed"]
            assert abs(observed - rate) < 0.02, (
                f"rate {rate}: observed {observed:.4f}, off by "
                f"{abs(observed - rate):.4f}"
            )

    def test_rate_one_fires_every_tick(self):
        result = run_test(["--test-mutation-rate", "--rate=1.0",
                           "--ticks=5000", "--seed=1"])
        assert result["mutations"] == 5000, (
            f"expected 5000 mutations at rate 1.0, got {result['mutations']}"
        )

    def test_rate_zero_reports_no_mutations(self):
        result = run_test(["--test-mutation-rate", "--rate=0.0",
                           "--ticks=5000", "--seed=1"])
        assert result["mutations"] == 0, (
            f"expected 0 mutations at rate 0.0, got {result['mutations']}"
        )

    def test_seed_is_deterministic(self):
        a = run_test(["--test-mutation-rate", "--rate=0.3", "--ticks=5000", "--seed=7"])
        b = run_test(["--test-mutation-rate", "--rate=0.3", "--ticks=5000", "--seed=7"])
        assert a["mutations"] == b["mutations"], (
            f"same seed gave {a['mutations']} then {b['mutations']}"
        )

    def test_undo_covers_edits_not_mutation(self):
        assert_clean(run_test(["--test-undo-redo"]))


class TestLength:
    def test_shrink_and_grow(self):
        assert_clean(run_test(["--test-length-change"]))


class TestBanks:
    """Save and recall must both work, unlike EucBank's Load."""

    def test_round_trip(self):
        assert_clean(run_test(["--test-bank-round-trip", "--bank=3"]))

    def test_first_and_last_slots(self):
        assert_clean(run_test(["--test-bank-round-trip", "--bank=0"]))
        assert_clean(run_test(["--test-bank-round-trip", "--bank=15"]))


def main() -> int:
    classes = [TestAddressing, TestChain, TestMutation, TestLength, TestBanks]
    passed = failed = 0
    failures = []

    for cls in classes:
        print(f"\n{cls.__name__}:")
        instance = cls()
        for name in sorted(n for n in dir(instance) if n.startswith("test_")):
            try:
                getattr(instance, name)()
                print(f"  PASS: {name}")
                passed += 1
            except Exception as exc:  # noqa: BLE001 - report and continue
                print(f"  FAIL: {name}: {exc}")
                failures.append(f"{cls.__name__}.{name}: {exc}")
                failed += 1

    print("\n" + "=" * 60)
    print(f"Results: {passed}/{passed + failed} passed, {failed} failed")
    print("=" * 60)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
