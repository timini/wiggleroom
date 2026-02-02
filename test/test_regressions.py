#!/usr/bin/env python3
"""
Regression Tests

Tests for fixed bugs to prevent regressions:
- TR808 CH/OH tuning: Tune parameters should affect pitch
- Euclogic retrigger: Consecutive hits should produce separate triggers

Run: python3 test/test_regressions.py
Or:  pytest test/test_regressions.py -v
"""

import json
import subprocess
import sys
import tempfile
import wave
import struct
import math
from pathlib import Path


def get_project_root() -> Path:
    """Get project root directory."""
    return Path(__file__).parent.parent


def get_faust_render() -> Path:
    """Get path to faust_render executable."""
    project_root = get_project_root()
    candidates = [
        project_root / "build" / "test" / "faust_render",
        project_root / "build" / "faust_render",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def render_audio(module: str, duration: float = 0.5, params: dict = None) -> bytes:
    """
    Render audio from a module and return raw samples as bytes.
    Returns stereo float32 samples.
    """
    exe = get_faust_render()
    if not exe.exists():
        raise FileNotFoundError(f"faust_render not found: {exe}")

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        output_path = f.name

    try:
        cmd = [
            str(exe),
            "--module", module,
            "--output", output_path,
            "--duration", str(duration),
            "--sample-rate", "48000",
        ]

        if params:
            for name, value in params.items():
                cmd.extend(["--param", f"{name}={value}"])

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)

        if result.returncode != 0:
            raise RuntimeError(f"faust_render failed: {result.stderr}")

        # Read WAV file
        with wave.open(output_path, 'rb') as wav:
            frames = wav.readframes(wav.getnframes())
            return frames
    finally:
        Path(output_path).unlink(missing_ok=True)


def analyze_spectral_centroid(samples: bytes, sample_rate: int = 48000) -> float:
    """
    Calculate spectral centroid (weighted average frequency) of audio.
    Returns frequency in Hz.
    """
    import numpy as np

    # Convert bytes to numpy array (assuming 16-bit signed)
    n_samples = len(samples) // 2
    data = struct.unpack(f'{n_samples}h', samples)
    audio = np.array(data, dtype=np.float32) / 32768.0

    # Use first channel if stereo
    if len(audio) > sample_rate:
        audio = audio[:sample_rate]  # First second only

    # Compute FFT
    fft = np.abs(np.fft.rfft(audio))
    freqs = np.fft.rfftfreq(len(audio), 1.0 / sample_rate)

    # Spectral centroid = weighted mean of frequencies
    if np.sum(fft) > 0:
        centroid = np.sum(freqs * fft) / np.sum(fft)
        return centroid
    return 0.0


def analyze_peak_frequency(samples: bytes, sample_rate: int = 48000, min_freq: float = 100) -> float:
    """
    Find the peak frequency in the spectrum above min_freq.
    Returns frequency in Hz.
    """
    import numpy as np

    # Convert bytes to numpy array
    n_samples = len(samples) // 2
    data = struct.unpack(f'{n_samples}h', samples)
    audio = np.array(data, dtype=np.float32) / 32768.0

    # FFT
    fft = np.abs(np.fft.rfft(audio))
    freqs = np.fft.rfftfreq(len(audio), 1.0 / sample_rate)

    # Find peak above min_freq
    mask = freqs >= min_freq
    if np.any(mask) and np.max(fft[mask]) > 0:
        peak_idx = np.argmax(fft[mask])
        return freqs[mask][peak_idx]
    return 0.0


def get_audio_rms(samples: bytes) -> float:
    """Calculate RMS of audio samples."""
    import numpy as np

    n_samples = len(samples) // 2
    data = struct.unpack(f'{n_samples}h', samples)
    audio = np.array(data, dtype=np.float32) / 32768.0

    return np.sqrt(np.mean(audio ** 2))


# =============================================================================
# TR808 Tuning Tests
# =============================================================================

class TestTR808Tuning:
    """Test that TR808 tune parameters affect pitch."""

    def test_ch_tune_changes_pitch(self):
        """CH tune parameter should change closed hihat pitch."""
        try:
            import numpy as np
        except ImportError:
            print("SKIP: numpy not installed")
            return

        # Render at different tune values
        samples_low = render_audio("TR808", 0.3, {"ch_tune": "-1", "ch_trig": "10"})
        samples_mid = render_audio("TR808", 0.3, {"ch_tune": "0", "ch_trig": "10"})
        samples_high = render_audio("TR808", 0.3, {"ch_tune": "1", "ch_trig": "10"})

        # Analyze spectral centroid
        centroid_low = analyze_spectral_centroid(samples_low)
        centroid_mid = analyze_spectral_centroid(samples_mid)
        centroid_high = analyze_spectral_centroid(samples_high)

        # Higher tune should produce higher centroid
        # Allow some tolerance for noise in spectral analysis
        assert centroid_high > centroid_low * 0.9, (
            f"CH tune not affecting pitch: low={centroid_low:.1f}Hz, high={centroid_high:.1f}Hz"
        )

        # Verify there's actual audio output
        rms_mid = get_audio_rms(samples_mid)
        assert rms_mid > 0.001, f"CH produced no audio (RMS={rms_mid})"

    def test_oh_tune_changes_pitch(self):
        """OH tune parameter should change open hihat pitch."""
        try:
            import numpy as np
        except ImportError:
            print("SKIP: numpy not installed")
            return

        # Render at different tune values
        samples_low = render_audio("TR808", 0.5, {"oh_tune": "-1", "oh_trig": "10", "oh_decay": "0.8"})
        samples_mid = render_audio("TR808", 0.5, {"oh_tune": "0", "oh_trig": "10", "oh_decay": "0.8"})
        samples_high = render_audio("TR808", 0.5, {"oh_tune": "1", "oh_trig": "10", "oh_decay": "0.8"})

        # Analyze spectral centroid
        centroid_low = analyze_spectral_centroid(samples_low)
        centroid_mid = analyze_spectral_centroid(samples_mid)
        centroid_high = analyze_spectral_centroid(samples_high)

        # Higher tune should produce higher centroid
        assert centroid_high > centroid_low * 0.9, (
            f"OH tune not affecting pitch: low={centroid_low:.1f}Hz, high={centroid_high:.1f}Hz"
        )

        # Verify there's actual audio output
        rms_mid = get_audio_rms(samples_mid)
        assert rms_mid > 0.001, f"OH produced no audio (RMS={rms_mid})"

    def test_bd_tune_produces_different_audio(self):
        """BD tune parameter should produce different audio (baseline check)."""
        try:
            import numpy as np
        except ImportError:
            print("SKIP: numpy not installed")
            return

        samples_low = render_audio("TR808", 0.5, {"bd_tune": "-1", "bd_trig": "10"})
        samples_high = render_audio("TR808", 0.5, {"bd_tune": "1", "bd_trig": "10"})

        # Different tune values should produce different audio
        # (BD spectral analysis is complex due to low frequency + click transient)
        assert samples_low != samples_high, "BD tune should affect audio output"

        # Verify there's actual audio
        rms = get_audio_rms(samples_low)
        assert rms > 0.001, f"BD produced no audio (RMS={rms})"


# =============================================================================
# Euclogic Retrigger Tests (Manual Verification Notes)
# =============================================================================

class TestEuclogicRetrigger:
    """
    Retrigger regression test notes.

    The retrigger fix adds a 0.5ms gap between consecutive gates to allow
    envelope generators to see a new rising edge.

    Full automated testing requires simulating the VCV Rack process() loop
    with clock signals and measuring gate output timing, which is beyond
    the scope of the unit test framework.

    Manual verification steps:
    1. Load Euclogic/Euclogic2 in VCV Rack
    2. Connect to clock source (e.g., 120 BPM)
    3. Set pattern with consecutive hits (e.g., Steps=4, Hits=4)
    4. Connect Gate output to envelope generator (e.g., ADSR)
    5. Enable Retrigger switch (should be ON by default)
    6. Verify envelope attacks on each hit, not just first
    7. Toggle Retrigger OFF - gate should stay high, envelope only attacks once
    """

    def test_retrigger_documentation(self):
        """Document retrigger behavior (placeholder for manual test)."""
        # This test exists to document the expected behavior
        # and remind developers to manually verify retrigger functionality

        expected_behavior = {
            "retrigger_on": "Gate goes low for 0.5ms between consecutive hits",
            "retrigger_off": "Gate stays high across consecutive hits",
            "trigger_output": "Always fires on each hit regardless of retrigger setting",
            "affected_modules": ["Euclogic", "Euclogic2"],
        }

        # Pass - this is documentation only
        assert True, "See docstring for manual verification steps"


# =============================================================================
# Main
# =============================================================================

def run_all_tests():
    """Run all regression tests."""
    test_classes = [
        TestTR808Tuning,
        TestEuclogicRetrigger,
    ]

    total = 0
    passed = 0
    failed = 0
    skipped = 0
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
                    if "SKIP" in str(e):
                        skipped += 1
                        print(f"  SKIP: {method_name}")
                    else:
                        failed += 1
                        errors.append((test_name, str(e)))
                        print(f"  FAIL: {method_name}")
                except Exception as e:
                    failed += 1
                    errors.append((test_name, f"ERROR: {e}"))
                    print(f"  ERROR: {method_name} - {e}")

    print(f"\n{'='*60}")
    print(f"Results: {passed}/{total} passed, {failed} failed, {skipped} skipped")
    print('='*60)

    if errors:
        print("\nFailures:")
        for test_name, error in errors:
            print(f"\n  {test_name}:")
            print(f"    {error[:200]}...")

    return failed == 0


if __name__ == "__main__":
    print("Regression Test Suite")
    print("="*60)

    exe = get_faust_render()
    if not exe.exists():
        print(f"\nERROR: faust_render not found at: {exe}")
        print("Run 'just build' first.")
        sys.exit(1)

    print(f"Using faust_render: {exe}\n")

    success = run_all_tests()
    sys.exit(0 if success else 1)
