#!/usr/bin/env python3
"""
Cross-validate the C++ HPSS median filter against an independent implementation.

The Stems HPSS follows Fitzgerald (DAFx-10): median-filter the magnitude
spectrogram across time to enhance harmonic content, and across frequency to
enhance percussive content. That is a published algorithm with reference
implementations, so it should be checked against something external rather than
only against itself.

Approach: stems_test dumps the magnitude spectrogram AND both median-filtered
versions. This file recomputes the medians from the same magnitudes using
scipy.ndimage.median_filter and compares.

Comparing the filtered spectrogram rather than the final audio is deliberate.
It isolates the median filter, which is the part being validated, from any
difference in STFT framing, windowing or FFT implementation. A whole-signal
audio comparison would fail for reasons that have nothing to do with the
algorithm under test.

scipy is already a dependency of the existing test suite. librosa's
decompose.hpss uses exactly this median filter, so scipy's median_filter with
matching boundary handling is the same reference without the heavier dependency.

Run:  python3 test/test_hpss_reference.py
      pytest test/test_hpss_reference.py -v
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

try:
    import numpy as np
    from scipy.ndimage import median_filter
    HAVE_SCIPY = True
except ImportError:  # pragma: no cover - environment without scipy
    HAVE_SCIPY = False

project_root = Path(__file__).resolve().parent.parent

# The C++ filter reflection-pads exactly as scipy.ndimage does under its default
# "reflect" mode, so the WHOLE array is comparable, edges included.
#
# An earlier version of this file compared only the interior, which hid a real
# defect: dropping out-of-range neighbours left edge windows shorter than the
# kernel, and sometimes even-length, so the first and last kernel/2 frames were
# filtered differently from the interior. With the default kernel of 31 that is
# 15 frames at each end, which is exactly where loop boundaries live.
TOLERANCE = 1e-5


def get_test_executable() -> Path:
    for candidate in (project_root / "build" / "test" / "stems_test",
                      project_root / "build" / "stems_test"):
        if candidate.exists():
            return candidate
    return project_root / "build" / "test" / "stems_test"


def load_dump() -> dict:
    exe = get_test_executable()
    if not exe.exists():
        raise FileNotFoundError(f"stems_test not found at {exe}. Run 'just build' first.")
    result = subprocess.run([str(exe), "--dump-hpss-medians"],
                            capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        raise AssertionError(f"dump failed: {result.stderr}")
    return json.loads(result.stdout.strip().split("\n")[0])


def reshape(dump: dict, key: str) -> np.ndarray:
    return np.asarray(dump[key], dtype=np.float64).reshape(dump["frames"], dump["bins"])


def test_harmonic_median_matches_reference():
    """Median across TIME must match scipy over the whole array, edges included."""
    dump = load_dump()
    mag = reshape(dump, "magnitude")
    ours = reshape(dump, "harmonic_median")
    kernel = dump["kernel"]

    # Median along the frame (time) axis, full array including edges.
    expected = median_filter(mag, size=(kernel, 1), mode="reflect")

    worst = float(np.abs(ours - expected).max())
    assert worst < TOLERANCE, f"harmonic median differs from scipy by up to {worst}"

    # Report the edges separately so a boundary-only regression is obvious.
    half = kernel // 2
    edge = np.concatenate([ours[:half, :].ravel(), ours[-half:, :].ravel()])
    edge_expected = np.concatenate([expected[:half, :].ravel(), expected[-half:, :].ravel()])
    worst_edge = float(np.abs(edge - edge_expected).max())
    assert worst_edge < TOLERANCE, f"harmonic median edges differ by up to {worst_edge}"


def test_percussive_median_matches_reference():
    """Median across FREQUENCY must match scipy over the whole array, edges included."""
    dump = load_dump()
    mag = reshape(dump, "magnitude")
    ours = reshape(dump, "percussive_median")
    kernel = dump["kernel"]

    expected = median_filter(mag, size=(1, kernel), mode="reflect")

    worst = float(np.abs(ours - expected).max())
    assert worst < TOLERANCE, f"percussive median differs from scipy by up to {worst}"

    half = kernel // 2
    edge = np.concatenate([ours[:, :half].ravel(), ours[:, -half:].ravel()])
    edge_expected = np.concatenate([expected[:, :half].ravel(), expected[:, -half:].ravel()])
    worst_edge = float(np.abs(edge - edge_expected).max())
    assert worst_edge < TOLERANCE, f"percussive median edges differ by up to {worst_edge}"


def test_the_two_medians_are_actually_different():
    """Guard against both axes being filtered the same way.

    If a refactor made both calls filter the same axis, every other test here
    would still pass while the separation was meaningless.
    """
    dump = load_dump()
    harm = reshape(dump, "harmonic_median")
    perc = reshape(dump, "percussive_median")
    rel = float(np.abs(harm - perc).mean() / (np.abs(harm).mean() + 1e-20))
    assert rel > 0.01, (
        f"harmonic and percussive medians are nearly identical (rel diff {rel}); "
        "both axes may be filtered the same way"
    )


def main() -> int:
    print("HPSS Reference Cross-Check (scipy)")
    print("=" * 60)
    if not HAVE_SCIPY:
        # Skip rather than fail: this is a cross-validation against an external
        # reference, not a correctness gate on the build itself. CI installs
        # scipy so it does run there.
        print("  SKIP: numpy/scipy not installed; cross-check not run")
        return 0
    tests = [
        ("harmonic median vs scipy", test_harmonic_median_matches_reference),
        ("percussive median vs scipy", test_percussive_median_matches_reference),
        ("medians differ from each other", test_the_two_medians_are_actually_different),
    ]
    failed = []
    for name, fn in tests:
        try:
            fn()
            print(f"  PASS: {name}")
        except Exception as exc:  # noqa: BLE001
            failed.append((name, str(exc)))
            print(f"  FAIL: {name}")

    print("\n" + "=" * 60)
    print(f"Results: {len(tests) - len(failed)}/{len(tests)} passed, {len(failed)} failed")
    print("=" * 60)
    for name, err in failed:
        print(f"\n  {name}:\n    {err[:400]}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
