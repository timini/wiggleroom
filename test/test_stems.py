#!/usr/bin/env python3
"""
Stems core test driver.

Shells out to the stems_test executable and asserts on its JSON output. The
Stems core in src/common/stems/ is framework-free, so stems_test links nothing,
not even the Rack SDK, and these tests run anywhere the repo builds.

Run:  python3 test/test_stems.py
      pytest test/test_stems.py -v
"""

import json
import subprocess
import sys
from pathlib import Path

project_root = Path(__file__).resolve().parent.parent


def get_test_executable() -> Path:
    for candidate in (project_root / "build" / "test" / "stems_test",
                      project_root / "build" / "stems_test"):
        if candidate.exists():
            return candidate
    return project_root / "build" / "test" / "stems_test"


def run_test(args: list[str]) -> dict:
    exe = get_test_executable()
    if not exe.exists():
        raise FileNotFoundError(
            f"stems_test not found at {exe}. Run 'just build' first."
        )
    result = subprocess.run(
        [str(exe)] + args, capture_output=True, text=True, timeout=60
    )
    if result.returncode != 0:
        raise AssertionError(
            f"stems_test {' '.join(args)} exited {result.returncode}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return json.loads(result.stdout.strip().split("\n")[0])


class TestFftBackend:
    """The FftBackend contract, exercised through ReferenceFft.

    ReferenceFft is what stems_test uses in place of the Rack adapter's
    RealFFT, so these tests also define the contract any adapter must meet.
    """

    def test_roundtrip_reconstructs_input(self):
        """inverse(forward(x)) must reproduce x to 1e-5."""
        result = run_test(["--test-fft-roundtrip", "--size=2048"])
        assert result["failed"] == 0
        assert result["max_error"] < 1e-5, f"round-trip error {result['max_error']}"

    def test_roundtrip_across_sizes(self):
        """Round-trip must hold for every supported transform size."""
        result = run_test(["--test-fft-sizes"])
        assert result["failed"] == 0
        assert result["worst_error"] < 1e-5, (
            f"worst error {result['worst_error']} at size {result['worst_size']}"
        )

    def test_impulse_is_flat(self):
        """A unit impulse must give unit magnitude in every bin."""
        result = run_test(["--test-fft-impulse", "--size=2048"])
        assert result["failed"] == 0
        assert result["max_error"] < 1e-5

    def test_bin_centred_sine_does_not_leak(self):
        """A sine at exactly bin k must put its energy in bin k alone.

        Guards against off-by-one errors in the bin indexing and against a
        wrong normalisation convention, both of which would silently corrupt
        the STFT built on top of this.
        """
        for bin_index in (1, 64, 512):
            result = run_test(["--test-fft-sine", "--size=2048", f"--bin={bin_index}"])
            assert result["failed"] == 0
            assert result["leakage_ratio"] < 1e-6, (
                f"bin {bin_index} leaked {result['leakage_ratio']}"
            )

    def test_self_test_passes(self):
        """The aggregate self-test must report no failures."""
        result = run_test(["--self-test"])
        assert result["failed"] == 0, f"self-test reported {result['failed']} failures"
        assert result["passed"] > 0


class TestRingBuffer:
    """The capture buffer.

    Not rack::dsp::RingBuffer, whose capacity is a template parameter and so
    cannot depend on sample rate. This one allocates once at construction and
    never reallocates, which is what makes the audio-thread write path safe.
    """

    def test_written_samples_read_back_bit_exact(self):
        result = run_test(["--test-buffer-roundtrip"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_wraparound_overwrites_oldest(self):
        """Writing past capacity must drop the oldest frames, not corrupt or grow.

        Index 0 stays the oldest surviving frame so callers see a stable
        timeline however far the write head has wrapped.
        """
        result = run_test(["--test-buffer-wraparound"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_no_reallocation_after_construction(self):
        """Capacity and storage address must be stable for the object's life.

        A reallocation on the audio thread would be a real-time violation, and
        would invalidate any pointer a reader was holding.
        """
        result = run_test(["--test-buffer-no-alloc"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_fractional_reads_interpolate(self):
        """Vari-speed playback reads at fractional positions."""
        result = run_test(["--test-buffer-interpolate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_non_finite_positions_are_rejected(self):
        """inf and NaN playback positions must give silence, not NaN audio.

        Casting inf or NaN to size_t is undefined behaviour, and NaN slips past
        a bare `position < 0` guard because every NaN comparison is false. NaN
        audio would then propagate through the entire patch.
        """
        result = run_test(["--test-buffer-non-finite"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_clear_does_not_touch_storage(self):
        """clear() must be O(1) and audio-thread safe.

        Zeroing the allocation is up to 24.6 MB at 96 kHz stereo over the
        32 second cap, written synchronously exactly when a new take starts.
        Resetting the counters already makes old samples unreachable.
        """
        result = run_test(["--test-buffer-clear-cheap"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_capacity_follows_rate_and_duration_cap(self):
        """Capacity is sampleRate * maxSeconds, which is how the spec's 32
        second cap is enforced across 44.1k, 48k and 96k."""
        result = run_test(["--test-buffer-capacity"])
        assert result["failed"] == 0, result.get("detail", "")


class TestTransport:
    """Clock tracking and repitch playback.

    Phase-locking rather than free-running estimation: the playhead advances at
    the measured rate but snaps to the grid on every clock edge, so error is
    bounded by one clock interval and never accumulates.
    """

    def test_stays_locked_over_a_long_run(self):
        """1000 bars at 120 BPM with under one frame of downbeat drift.

        Verified to have teeth: removing the phase snap makes this fail with
        1.17 frames of drift.
        """
        result = run_test(["--test-transport-lock"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_reset_returns_to_downbeat_immediately(self):
        """Reset must land on the downbeat, not wait for the next clock edge."""
        result = run_test(["--test-transport-reset"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_clock_division_scales_rate(self):
        result = run_test(["--test-transport-division"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_loop_bounds_window_the_playhead(self):
        result = run_test(["--test-transport-loop"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_free_runs_safely_without_a_clock(self):
        """No clock must not produce NaN or an out-of-range phase.

        This is the state on patch load, before anything is patched.
        """
        result = run_test(["--test-transport-no-clock"])
        assert result["failed"] == 0, result.get("detail", "")


def run_all_tests() -> bool:
    passed = failed = 0
    errors = []
    for cls in (TestFftBackend, TestRingBuffer, TestTransport):
        instance = cls()
        for name in dir(instance):
            if not name.startswith("test_"):
                continue
            try:
                getattr(instance, name)()
                passed += 1
                print(f"  PASS: {name}")
            except Exception as exc:  # noqa: BLE001 - report and continue
                failed += 1
                errors.append((f"{cls.__name__}.{name}", str(exc)))
                print(f"  FAIL: {name}")

    total = passed + failed
    print(f"\n{'='*60}")
    print(f"Results: {passed}/{total} passed, {failed} failed")
    print("=" * 60)
    if errors:
        print("\nFailures:")
        for name, error in errors:
            print(f"\n  {name}:\n    {error[:300]}")
    return failed == 0


if __name__ == "__main__":
    print("Stems Core Test Suite")
    print("=" * 60)
    exe = get_test_executable()
    if not exe.exists():
        print(f"\nERROR: stems_test not found at: {exe}")
        print("Run 'just build' first to compile the test executable.")
        sys.exit(1)
    print(f"Using test executable: {exe}\n")
    sys.exit(0 if run_all_tests() else 1)
