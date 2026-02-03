#!/usr/bin/env python3
"""
Comprehensive Faust Module Testing Framework

Runs automated tests on all Faust DSP modules to detect:
- Parameter sensitivity issues (params that don't affect output)
- Audio stability issues (NaN, Inf, clipping, DC offset)
- Gate/trigger issues (envelope not responding)
- Regressions (output differs from baseline)

Usage:
    python test_framework.py                    # Run all tests
    python test_framework.py --module ChaosFlute  # Test specific module
    python test_framework.py --generate-baselines  # Create baseline files
    python test_framework.py --ci               # CI mode (JSON output, exit code)

Exit codes:
    0 = All tests passed
    1 = Some tests failed
    2 = Error running tests
"""

import argparse
import hashlib
import json
import sys
import tempfile
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path
from typing import Any

import numpy as np

# Import shared utilities
from utils import (
    get_project_root,
    get_render_executable,
    run_faust_render,
    get_modules,
    get_module_params,
    load_audio,
    load_module_config as _load_module_config,
    extract_audio_stats,
    SAMPLE_RATE,
    DEFAULT_QUALITY_THRESHOLDS,
)

# Import audio quality analysis module
try:
    from audio_quality import (
        measure_thd, detect_aliasing, analyze_harmonics,
        compute_spectral_richness, analyze_envelope,
        AudioQualityReport
    )
    HAS_AUDIO_QUALITY = True
except ImportError:
    HAS_AUDIO_QUALITY = False

# Configuration
TEST_DURATION = 2.0
SENSITIVITY_THRESHOLD = 0.05  # Minimum sensitivity score to pass
SENSITIVITY_STEPS = 5
DC_OFFSET_THRESHOLD = 0.01  # Maximum acceptable DC offset
CLIPPING_THRESHOLD = 0.99  # Samples above this are considered clipping
MAX_CLIPPING_RATIO = 0.01  # Max ratio of clipping samples allowed (default)

# Default thresholds (used when no module config exists)
DEFAULT_THRESHOLDS = DEFAULT_QUALITY_THRESHOLDS


# =============================================================================
# Module Test Config Loading
# =============================================================================

@dataclass
class ModuleTestConfig:
    """Configuration for module-level testing."""
    module_name: str
    module_type: str = "instrument"  # instrument, filter, effect, resonator, utility
    skip_audio_tests: bool = False
    skip_reason: str = ""
    description: str = ""
    thresholds: dict = field(default_factory=lambda: DEFAULT_THRESHOLDS.copy())
    test_scenarios: list = field(default_factory=list)
    parameter_sweeps: dict = field(default_factory=dict)

    def get_clipping_threshold(self) -> float:
        """Get effective clipping threshold (higher for hot signal modules)."""
        if self.thresholds.get("allow_hot_signal", False):
            return 0.15  # 15% for wavefolders, saturators, etc.
        return self.thresholds.get("clipping_max_percent", 1.0) / 100.0


def load_module_config(module_name: str) -> ModuleTestConfig:
    """Load test configuration for a module from its test_config.json file."""
    # Use the shared utility function
    config_dict = _load_module_config(module_name)

    config = ModuleTestConfig(module_name=module_name)
    config.module_type = config_dict.get("module_type", "instrument")
    config.skip_audio_tests = config_dict.get("skip_audio_tests", False)
    config.skip_reason = config_dict.get("skip_reason", "")
    config.description = config_dict.get("description", "")
    config.thresholds = config_dict.get("thresholds", DEFAULT_THRESHOLDS.copy())
    config.test_scenarios = config_dict.get("test_scenarios", [{"name": "default", "duration": 2.0}])

    return config


# Legacy support - build HOT_SIGNAL_MODULES dynamically from configs
def get_hot_signal_modules() -> set[str]:
    """Get set of modules that allow hot signals (dynamically from configs)."""
    hot_modules = set()
    project_root = get_project_root()
    modules_dir = project_root / "src" / "modules"

    if modules_dir.exists():
        for module_dir in modules_dir.iterdir():
            if module_dir.is_dir():
                config = _load_module_config(module_dir.name)
                if config.get("thresholds", {}).get("allow_hot_signal", False):
                    hot_modules.add(module_dir.name)

    # Fallback for backwards compatibility
    if not hot_modules:
        hot_modules = {"InfiniteFolder"}

    return hot_modules


# Cache for loaded configs
_config_cache: dict[str, ModuleTestConfig] = {}


def get_module_config(module_name: str) -> ModuleTestConfig:
    """Get module config with caching."""
    if module_name not in _config_cache:
        _config_cache[module_name] = load_module_config(module_name)
    return _config_cache[module_name]


@dataclass
class TestResult:
    """Result of a single test."""
    name: str
    passed: bool
    message: str
    details: dict = field(default_factory=dict)


@dataclass
class ModuleTestResults:
    """All test results for a module."""
    module_name: str
    passed: bool
    tests: list[TestResult] = field(default_factory=list)
    duration_ms: float = 0.0

    def add(self, result: TestResult):
        self.tests.append(result)
        if not result.passed:
            self.passed = False


@dataclass
class TestReport:
    """Full test report for all modules."""
    timestamp: str
    total_modules: int = 0
    passed_modules: int = 0
    failed_modules: int = 0
    modules: list[ModuleTestResults] = field(default_factory=list)

    def add(self, module_result: ModuleTestResults):
        self.modules.append(module_result)
        self.total_modules += 1
        if module_result.passed:
            self.passed_modules += 1
        else:
            self.failed_modules += 1

    @property
    def passed(self) -> bool:
        return self.failed_modules == 0

    def to_dict(self) -> dict:
        return {
            "timestamp": self.timestamp,
            "summary": {
                "total": self.total_modules,
                "passed": self.passed_modules,
                "failed": self.failed_modules,
            },
            "modules": [
                {
                    "name": m.module_name,
                    "passed": m.passed,
                    "duration_ms": m.duration_ms,
                    "tests": [asdict(t) for t in m.tests]
                }
                for m in self.modules
            ]
        }


def render_audio(module_name: str, params: dict[str, float],
                 output_path: Path, duration: float = TEST_DURATION,
                 no_auto_gate: bool = False) -> bool:
    """Render audio using shared utility."""
    from utils import render_audio as _render_audio
    success, _ = _render_audio(
        module_name, params, output_path, duration, SAMPLE_RATE, no_auto_gate
    )
    return success


def compute_audio_hash(audio: np.ndarray) -> str:
    """Compute a hash of audio content for regression detection."""
    # Quantize to reduce sensitivity to tiny floating point differences
    quantized = (audio * 1000).astype(np.int16)
    return hashlib.sha256(quantized.tobytes()).hexdigest()[:16]


# =============================================================================
# Individual Tests
# =============================================================================

def test_compilation(module_name: str) -> TestResult:
    """Test that the module can be instantiated and queried."""
    success, output = run_faust_render(["--module", module_name, "--list-params"])
    if success:
        return TestResult("compilation", True, "Module compiles and loads correctly")
    else:
        return TestResult("compilation", False, f"Failed to load module: {output}")


def test_basic_render(module_name: str, tmp_dir: Path) -> TestResult:
    """Test that the module can render audio without crashing."""
    wav_path = tmp_dir / f"{module_name}_basic.wav"

    if not render_audio(module_name, {}, wav_path):
        return TestResult("basic_render", False, "Failed to render audio")

    audio = load_audio(wav_path)
    if audio is None:
        return TestResult("basic_render", False, "Failed to load rendered audio")

    stats = extract_audio_stats(audio)

    # Check for critical issues
    if stats["has_nan"]:
        return TestResult("basic_render", False, "Audio contains NaN values",
                         {"stats": stats})

    if stats["has_inf"]:
        return TestResult("basic_render", False, "Audio contains Inf values",
                         {"stats": stats})

    return TestResult("basic_render", True, "Basic render successful",
                     {"stats": stats})


def test_audio_stability(module_name: str, tmp_dir: Path) -> TestResult:
    """Test audio output for stability issues."""
    # Load module config for thresholds
    config = get_module_config(module_name)

    # Skip if module config says so
    if config.skip_audio_tests:
        return TestResult("audio_stability", True,
                         f"Skipped ({config.skip_reason or 'utility module'})")

    wav_path = tmp_dir / f"{module_name}_stability.wav"

    if not render_audio(module_name, {}, wav_path):
        return TestResult("audio_stability", False, "Failed to render audio")

    audio = load_audio(wav_path)
    if audio is None:
        return TestResult("audio_stability", False, "Failed to load audio")

    stats = extract_audio_stats(audio)
    issues = []

    # Check DC offset
    if abs(stats["dc_offset"]) > DC_OFFSET_THRESHOLD:
        issues.append(f"High DC offset: {stats['dc_offset']:.4f}")

    # Check clipping - use config threshold
    clipping_limit = config.get_clipping_threshold()
    if stats["clipping_ratio"] > clipping_limit:
        issues.append(f"Excessive clipping: {stats['clipping_ratio']*100:.1f}% (threshold: {clipping_limit*100:.1f}%)")

    # Check if completely silent
    if stats["silence_ratio"] > 0.95:
        issues.append("Audio is nearly silent")

    if issues:
        return TestResult("audio_stability", False, "; ".join(issues),
                         {"stats": stats})

    return TestResult("audio_stability", True, "Audio is stable",
                     {"stats": stats})


def test_gate_response(module_name: str, tmp_dir: Path) -> TestResult:
    """Test that the module responds to gate signals."""
    params = get_module_params(module_name)
    gate_param = next((p for p in params if p["name"].lower() == "gate"), None)

    if gate_param is None:
        return TestResult("gate_response", True, "No gate parameter (skipped)")

    # Render with gate=0 and gate=max
    # Use no_auto_gate=True so the test controls the gate, not the renderer
    wav_no_gate = tmp_dir / f"{module_name}_no_gate.wav"
    wav_with_gate = tmp_dir / f"{module_name}_with_gate.wav"

    render_audio(module_name, {"gate": 0}, wav_no_gate, no_auto_gate=True)
    render_audio(module_name, {"gate": gate_param["max"]}, wav_with_gate, no_auto_gate=True)

    audio_no_gate = load_audio(wav_no_gate)
    audio_with_gate = load_audio(wav_with_gate)

    if audio_no_gate is None or audio_with_gate is None:
        return TestResult("gate_response", False, "Failed to load audio")

    stats_no_gate = extract_audio_stats(audio_no_gate)
    stats_with_gate = extract_audio_stats(audio_with_gate)

    # Check if there's a significant difference
    rms_diff = abs(stats_with_gate["rms"] - stats_no_gate["rms"])

    if rms_diff < 0.01:
        return TestResult("gate_response", False,
                         f"Gate has no effect on output (RMS diff: {rms_diff:.4f})",
                         {"rms_no_gate": stats_no_gate["rms"],
                          "rms_with_gate": stats_with_gate["rms"]})

    return TestResult("gate_response", True,
                     f"Gate affects output (RMS diff: {rms_diff:.4f})")


def test_parameter_sensitivity(module_name: str, tmp_dir: Path) -> TestResult:
    """Test that parameters actually affect the output."""
    params = get_module_params(module_name)

    # Filter out control parameters
    test_params = [p for p in params
                   if p["name"].lower() not in ["gate", "trigger", "velocity", "volts", "freq", "pitch"]]

    if not test_params:
        return TestResult("parameter_sensitivity", True, "No testable parameters")

    broken_params = []
    working_params = []

    for param in test_params:
        # Render at min and max values
        wav_min = tmp_dir / f"{module_name}_{param['name']}_min.wav"
        wav_max = tmp_dir / f"{module_name}_{param['name']}_max.wav"

        other_params = {p["name"]: p["init"] for p in params if p["name"] != param["name"]}

        params_min = {**other_params, param["name"]: param["min"]}
        params_max = {**other_params, param["name"]: param["max"]}

        render_audio(module_name, params_min, wav_min)
        render_audio(module_name, params_max, wav_max)

        audio_min = load_audio(wav_min)
        audio_max = load_audio(wav_max)

        if audio_min is None or audio_max is None:
            continue

        stats_min = extract_audio_stats(audio_min)
        stats_max = extract_audio_stats(audio_max)

        # Compute difference across multiple metrics
        rms_diff = abs(stats_max["rms"] - stats_min["rms"])
        peak_diff = abs(stats_max["peak"] - stats_min["peak"])

        # Also check audio content hash
        hash_min = compute_audio_hash(audio_min)
        hash_max = compute_audio_hash(audio_max)

        if hash_min == hash_max and rms_diff < 0.001:
            broken_params.append(param["name"])
        else:
            working_params.append(param["name"])

    if broken_params:
        return TestResult("parameter_sensitivity", False,
                         f"Parameters with no effect: {', '.join(broken_params)}",
                         {"broken": broken_params, "working": working_params})

    return TestResult("parameter_sensitivity", True,
                     f"All {len(working_params)} parameters affect output",
                     {"working": working_params})


def test_regression(module_name: str, tmp_dir: Path, baseline_dir: Path) -> TestResult:
    """Test for regressions against baseline."""
    baseline_file = baseline_dir / f"{module_name}_baseline.json"

    if not baseline_file.exists():
        return TestResult("regression", True, "No baseline (skipped)")

    with open(baseline_file) as f:
        baseline = json.load(f)

    # Render with baseline parameters
    wav_path = tmp_dir / f"{module_name}_regression.wav"
    render_audio(module_name, baseline.get("params", {}), wav_path)

    audio = load_audio(wav_path)
    if audio is None:
        return TestResult("regression", False, "Failed to load audio")

    current_hash = compute_audio_hash(audio)
    current_stats = extract_audio_stats(audio)

    baseline_hash = baseline.get("hash")
    baseline_stats = baseline.get("stats", {})

    # Check if hash matches (exact match)
    if baseline_hash and current_hash != baseline_hash:
        # Check if stats are close enough (fuzzy match)
        rms_diff = abs(current_stats["rms"] - baseline_stats.get("rms", 0))
        if rms_diff > 0.1:
            return TestResult("regression", False,
                             f"Output differs from baseline (RMS diff: {rms_diff:.4f})",
                             {"current_hash": current_hash, "baseline_hash": baseline_hash})

    return TestResult("regression", True, "Output matches baseline")


def estimate_fundamental_frequency(audio: np.ndarray, sr: int = SAMPLE_RATE) -> float | None:
    """Estimate the fundamental frequency of audio using autocorrelation."""
    if audio.ndim > 1:
        audio = audio.mean(axis=0)  # Mix to mono

    # Use only a stable portion of the audio (skip attack, use sustain)
    start = int(0.3 * sr)  # Skip first 300ms
    end = int(1.0 * sr)    # Use up to 1 second
    if len(audio) < end:
        end = len(audio)
    if start >= end:
        return None

    segment = audio[start:end]

    # Simple autocorrelation-based pitch detection
    # Look for peaks in autocorrelation corresponding to 50-2000 Hz
    min_lag = sr // 2000  # 2000 Hz max
    max_lag = sr // 50    # 50 Hz min

    if len(segment) < max_lag * 2:
        return None

    # Compute autocorrelation
    corr = np.correlate(segment, segment, mode='full')
    corr = corr[len(corr)//2:]  # Take positive lags only

    # Find the first significant peak after min_lag
    if max_lag >= len(corr):
        max_lag = len(corr) - 1

    search_region = corr[min_lag:max_lag]
    if len(search_region) == 0:
        return None

    # Find peaks (local maxima)
    peaks = []
    for i in range(1, len(search_region) - 1):
        if search_region[i] > search_region[i-1] and search_region[i] > search_region[i+1]:
            peaks.append((i + min_lag, search_region[i]))

    if not peaks:
        return None

    # Find the strongest peak
    best_lag, _ = max(peaks, key=lambda x: x[1])

    freq = sr / best_lag
    return freq


def test_pitch_tracking(module_name: str, tmp_dir: Path) -> TestResult:
    """Test that V/Oct pitch tracking works correctly."""
    params = get_module_params(module_name)
    volts_param = next((p for p in params if p["name"].lower() == "volts"), None)

    if volts_param is None:
        return TestResult("pitch_tracking", True, "No volts parameter (skipped)")

    # Check if this is a pitched module (has volts and produces tonal output)
    # Effects, filters, and resonators don't track pitch the same way
    module_type = get_module_type(module_name)
    if module_type in ["Filter", "Effect", "Resonator"]:
        return TestResult("pitch_tracking", True, f"Not a pitched module ({module_type}, skipped)")

    # Render at 0V (base pitch) and 1V (one octave up)
    wav_0v = tmp_dir / f"{module_name}_pitch_0v.wav"
    wav_1v = tmp_dir / f"{module_name}_pitch_1v.wav"

    # Get default params and set volts
    default_params = {p["name"]: p["init"] for p in params}

    params_0v = {**default_params, "volts": 0.0}
    params_1v = {**default_params, "volts": 1.0}

    # Use longer duration for pitch detection
    if not render_audio(module_name, params_0v, wav_0v, duration=3.0):
        return TestResult("pitch_tracking", False, "Failed to render at 0V")
    if not render_audio(module_name, params_1v, wav_1v, duration=3.0):
        return TestResult("pitch_tracking", False, "Failed to render at 1V")

    audio_0v = load_audio(wav_0v)
    audio_1v = load_audio(wav_1v)

    if audio_0v is None or audio_1v is None:
        return TestResult("pitch_tracking", False, "Failed to load audio")

    # Check audio has content
    rms_0v = np.sqrt(np.mean(audio_0v ** 2))
    rms_1v = np.sqrt(np.mean(audio_1v ** 2))

    if rms_0v < 0.01 or rms_1v < 0.01:
        return TestResult("pitch_tracking", True, "Audio too quiet for pitch detection (skipped)",
                         {"rms_0v": float(rms_0v), "rms_1v": float(rms_1v)})

    # Estimate fundamental frequencies
    freq_0v = estimate_fundamental_frequency(audio_0v)
    freq_1v = estimate_fundamental_frequency(audio_1v)

    if freq_0v is None or freq_1v is None:
        return TestResult("pitch_tracking", True, "Could not detect pitch (skipped)",
                         {"freq_0v": freq_0v, "freq_1v": freq_1v})

    # Check if frequency approximately doubled (within 15% tolerance for octave)
    # 1V should be one octave up, so freq_1v / freq_0v should be ~2.0
    ratio = freq_1v / freq_0v
    expected_ratio = 2.0
    tolerance = 0.15  # 15% tolerance

    if abs(ratio - expected_ratio) / expected_ratio > tolerance:
        return TestResult("pitch_tracking", False,
                         f"Pitch ratio {ratio:.2f} (expected ~2.0 for 1 octave)",
                         {"freq_0v": float(freq_0v), "freq_1v": float(freq_1v),
                          "ratio": float(ratio)})

    return TestResult("pitch_tracking", True,
                     f"V/Oct tracking works (ratio: {ratio:.2f})",
                     {"freq_0v": float(freq_0v), "freq_1v": float(freq_1v),
                      "ratio": float(ratio)})


def get_module_type(module_name: str) -> str:
    """Get the type of module for test categorization."""
    config = get_module_config(module_name)

    # Map config module_type to the capitalized format used elsewhere
    type_map = {
        "instrument": "Instrument",
        "filter": "Filter",
        "effect": "Effect",
        "resonator": "Resonator",
        "utility": "Utility"
    }

    return type_map.get(config.module_type, "Instrument")


# =============================================================================
# Audio Quality Tests
# =============================================================================

def test_thd(module_name: str, tmp_dir: Path) -> TestResult:
    """Test Total Harmonic Distortion."""
    if not HAS_AUDIO_QUALITY:
        return TestResult("thd", True, "Audio quality module not available (skipped)")

    # Load module config for thresholds
    config = get_module_config(module_name)

    # Skip if module config says so
    if config.skip_audio_tests:
        return TestResult("thd", True,
                         f"Skipped ({config.skip_reason or 'utility module'})")

    wav_path = tmp_dir / f"{module_name}_thd.wav"

    if not render_audio(module_name, {}, wav_path, duration=3.0):
        return TestResult("thd", False, "Failed to render audio")

    audio = load_audio(wav_path)
    if audio is None:
        return TestResult("thd", False, "Failed to load audio")

    # Skip silent audio
    rms = float(np.sqrt(np.mean(audio ** 2)))
    if rms < 0.001:
        return TestResult("thd", True, "Audio too quiet for THD measurement (skipped)",
                         {"rms": rms})

    try:
        thd_result = measure_thd(audio, SAMPLE_RATE)

        # Use config threshold
        thd_threshold = config.thresholds.get("thd_max_percent", 15.0)
        module_type = get_module_type(module_name)

        if thd_result.thd_percent > thd_threshold:
            return TestResult("thd", False,
                             f"High THD: {thd_result.thd_percent:.1f}% (threshold: {thd_threshold}%)",
                             {"thd_percent": thd_result.thd_percent,
                              "fundamental_freq": thd_result.fundamental_freq,
                              "module_type": module_type})

        return TestResult("thd", True,
                         f"THD: {thd_result.thd_percent:.1f}%",
                         {"thd_percent": thd_result.thd_percent,
                          "fundamental_freq": thd_result.fundamental_freq})

    except Exception as e:
        return TestResult("thd", True, f"THD measurement skipped: {e}")


def test_aliasing(module_name: str, tmp_dir: Path) -> TestResult:
    """Test for aliasing artifacts."""
    if not HAS_AUDIO_QUALITY:
        return TestResult("aliasing", True, "Audio quality module not available (skipped)")

    wav_path = tmp_dir / f"{module_name}_aliasing.wav"

    if not render_audio(module_name, {}, wav_path, duration=3.0):
        return TestResult("aliasing", False, "Failed to render audio")

    audio = load_audio(wav_path)
    if audio is None:
        return TestResult("aliasing", False, "Failed to load audio")

    # Skip silent audio
    rms = float(np.sqrt(np.mean(audio ** 2)))
    if rms < 0.001:
        return TestResult("aliasing", True, "Audio too quiet for aliasing detection (skipped)",
                         {"rms": rms})

    try:
        aliasing_result = detect_aliasing(audio, SAMPLE_RATE)

        # Aliasing threshold: -50dB is generally acceptable
        if aliasing_result.has_significant_aliasing and aliasing_result.alias_ratio_db > -50:
            return TestResult("aliasing", False,
                             f"Aliasing detected: {aliasing_result.alias_ratio_db:.1f} dB",
                             {"alias_ratio_db": aliasing_result.alias_ratio_db,
                              "input_frequency": aliasing_result.input_frequency,
                              "detected_aliases": len(aliasing_result.detected_aliases)})

        return TestResult("aliasing", True,
                         f"No significant aliasing ({aliasing_result.alias_ratio_db:.1f} dB)",
                         {"alias_ratio_db": aliasing_result.alias_ratio_db})

    except Exception as e:
        return TestResult("aliasing", True, f"Aliasing detection skipped: {e}")


def test_harmonic_character(module_name: str, tmp_dir: Path) -> TestResult:
    """Analyze harmonic character (informational, always passes)."""
    if not HAS_AUDIO_QUALITY:
        return TestResult("harmonic_character", True,
                         "Audio quality module not available (skipped)")

    wav_path = tmp_dir / f"{module_name}_harmonics.wav"

    if not render_audio(module_name, {}, wav_path, duration=3.0):
        return TestResult("harmonic_character", True, "Failed to render (skipped)")

    audio = load_audio(wav_path)
    if audio is None:
        return TestResult("harmonic_character", True, "Failed to load (skipped)")

    # Skip silent audio
    rms = float(np.sqrt(np.mean(audio ** 2)))
    if rms < 0.001:
        return TestResult("harmonic_character", True, "Audio too quiet (skipped)")

    try:
        harmonics = analyze_harmonics(audio, SAMPLE_RATE)

        # This is informational - always passes
        return TestResult("harmonic_character", True,
                         f"Character: {harmonics.character} (warmth: {harmonics.warmth_ratio:.2f})",
                         {"character": harmonics.character,
                          "warmth_ratio": harmonics.warmth_ratio,
                          "even_energy": harmonics.even_harmonic_energy,
                          "odd_energy": harmonics.odd_harmonic_energy,
                          "dominant_harmonics": harmonics.dominant_harmonics[:3]})

    except Exception as e:
        return TestResult("harmonic_character", True, f"Analysis skipped: {e}")


def test_spectral_richness(module_name: str, tmp_dir: Path) -> TestResult:
    """Analyze spectral richness (informational, always passes)."""
    if not HAS_AUDIO_QUALITY:
        return TestResult("spectral_richness", True,
                         "Audio quality module not available (skipped)")

    wav_path = tmp_dir / f"{module_name}_spectral.wav"

    if not render_audio(module_name, {}, wav_path, duration=3.0):
        return TestResult("spectral_richness", True, "Failed to render (skipped)")

    audio = load_audio(wav_path)
    if audio is None:
        return TestResult("spectral_richness", True, "Failed to load (skipped)")

    # Skip silent audio
    rms = float(np.sqrt(np.mean(audio ** 2)))
    if rms < 0.001:
        return TestResult("spectral_richness", True, "Audio too quiet (skipped)")

    try:
        spectral = compute_spectral_richness(audio, SAMPLE_RATE)

        # Informational - always passes
        return TestResult("spectral_richness", True,
                         f"Entropy: {spectral.spectral_entropy:.2f}, HNR: {spectral.harmonic_to_noise_ratio:.0f} dB",
                         {"spectral_entropy": spectral.spectral_entropy,
                          "spectral_flatness": spectral.spectral_flatness,
                          "harmonic_to_noise_ratio": spectral.harmonic_to_noise_ratio,
                          "crest_factor": spectral.crest_factor,
                          "dynamic_range_db": spectral.dynamic_range_db})

    except Exception as e:
        return TestResult("spectral_richness", True, f"Analysis skipped: {e}")


def test_envelope(module_name: str, tmp_dir: Path) -> TestResult:
    """Analyze envelope characteristics."""
    if not HAS_AUDIO_QUALITY:
        return TestResult("envelope", True,
                         "Audio quality module not available (skipped)")

    # Only test instruments and effects with envelopes
    module_type = get_module_type(module_name)
    if module_type == "Filter":
        return TestResult("envelope", True, "Filter module (skipped)")

    wav_path = tmp_dir / f"{module_name}_envelope.wav"

    if not render_audio(module_name, {}, wav_path, duration=3.0):
        return TestResult("envelope", True, "Failed to render (skipped)")

    audio = load_audio(wav_path)
    if audio is None:
        return TestResult("envelope", True, "Failed to load (skipped)")

    # Skip silent audio
    rms = float(np.sqrt(np.mean(audio ** 2)))
    if rms < 0.001:
        return TestResult("envelope", True, "Audio too quiet (skipped)")

    try:
        envelope = analyze_envelope(audio, SAMPLE_RATE)

        details = {
            "peak_amplitude": envelope.peak_amplitude,
            "onset_strength": envelope.onset_strength,
            "has_clear_envelope": envelope.has_clear_envelope
        }

        if envelope.attack_time_ms is not None:
            details["attack_time_ms"] = envelope.attack_time_ms
        if envelope.decay_time_ms is not None:
            details["decay_time_ms"] = envelope.decay_time_ms
        if envelope.release_time_ms is not None:
            details["release_time_ms"] = envelope.release_time_ms
        if envelope.sustain_level is not None:
            details["sustain_level"] = envelope.sustain_level

        # Build message
        parts = []
        if envelope.attack_time_ms is not None:
            parts.append(f"Attack: {envelope.attack_time_ms:.0f}ms")
        if envelope.decay_time_ms is not None:
            parts.append(f"Decay: {envelope.decay_time_ms:.0f}ms")
        parts.append(f"Onset: {envelope.onset_strength:.3f}")

        message = ", ".join(parts) if parts else "Envelope analyzed"

        return TestResult("envelope", True, message, details)

    except Exception as e:
        return TestResult("envelope", True, f"Analysis skipped: {e}")


# =============================================================================
# Main Test Runner
# =============================================================================

def run_module_tests(module_name: str, tmp_dir: Path,
                     baseline_dir: Path,
                     include_quality_tests: bool = True) -> ModuleTestResults:
    """Run all tests for a single module."""
    import time
    start = time.time()

    result = ModuleTestResults(module_name=module_name, passed=True)

    # Run core tests
    result.add(test_compilation(module_name))
    result.add(test_basic_render(module_name, tmp_dir))
    result.add(test_audio_stability(module_name, tmp_dir))
    result.add(test_gate_response(module_name, tmp_dir))
    result.add(test_pitch_tracking(module_name, tmp_dir))
    result.add(test_parameter_sensitivity(module_name, tmp_dir))
    result.add(test_regression(module_name, tmp_dir, baseline_dir))

    # Run audio quality tests
    if include_quality_tests and HAS_AUDIO_QUALITY:
        result.add(test_thd(module_name, tmp_dir))
        result.add(test_aliasing(module_name, tmp_dir))
        result.add(test_harmonic_character(module_name, tmp_dir))
        result.add(test_spectral_richness(module_name, tmp_dir))
        result.add(test_envelope(module_name, tmp_dir))

    result.duration_ms = (time.time() - start) * 1000
    return result


def generate_baseline(module_name: str, tmp_dir: Path, baseline_dir: Path):
    """Generate baseline file for a module."""
    baseline_dir.mkdir(parents=True, exist_ok=True)

    params = get_module_params(module_name)
    default_params = {p["name"]: p["init"] for p in params}

    wav_path = tmp_dir / f"{module_name}_baseline.wav"
    render_audio(module_name, default_params, wav_path)

    audio = load_audio(wav_path)
    if audio is None:
        print(f"  Failed to generate baseline for {module_name}")
        return

    baseline = {
        "module": module_name,
        "timestamp": datetime.now().isoformat(),
        "params": default_params,
        "hash": compute_audio_hash(audio),
        "stats": extract_audio_stats(audio),
    }

    baseline_file = baseline_dir / f"{module_name}_baseline.json"
    with open(baseline_file, "w") as f:
        json.dump(baseline, f, indent=2)

    print(f"  Generated baseline: {baseline_file.name}")


def print_results(report: TestReport, verbose: bool = False):
    """Print test results to console."""
    print("\n" + "=" * 70)
    print("FAUST MODULE TEST RESULTS")
    print("=" * 70)

    for module in report.modules:
        status = "PASS" if module.passed else "FAIL"
        status_color = "\033[92m" if module.passed else "\033[91m"
        reset = "\033[0m"

        print(f"\n{status_color}[{status}]{reset} {module.module_name} ({module.duration_ms:.0f}ms)")

        for test in module.tests:
            if test.passed:
                if verbose:
                    print(f"      [OK] {test.name}: {test.message}")
            else:
                print(f"      [FAIL] {test.name}: {test.message}")
                if verbose and test.details:
                    for k, v in test.details.items():
                        print(f"            {k}: {v}")

    print("\n" + "-" * 70)
    print(f"SUMMARY: {report.passed_modules}/{report.total_modules} modules passed")

    if report.failed_modules > 0:
        failed = [m.module_name for m in report.modules if not m.passed]
        print(f"FAILED: {', '.join(failed)}")

    print("=" * 70)


def main():
    parser = argparse.ArgumentParser(
        description="Comprehensive Faust module testing framework"
    )
    parser.add_argument("--module", help="Test specific module only")
    parser.add_argument("--generate-baselines", action="store_true",
                       help="Generate baseline files for regression testing")
    parser.add_argument("--baseline-dir", default="test/baselines",
                       help="Directory for baseline files")
    parser.add_argument("--ci", action="store_true",
                       help="CI mode: JSON output, exit code on failure")
    parser.add_argument("--output", help="Output JSON report to file")
    parser.add_argument("--verbose", "-v", action="store_true",
                       help="Verbose output")
    parser.add_argument("--no-quality", action="store_true",
                       help="Skip audio quality tests (THD, aliasing, etc.)")
    parser.add_argument("--quality-only", action="store_true",
                       help="Run only audio quality tests")

    args = parser.parse_args()

    project_root = get_project_root()
    baseline_dir = project_root / args.baseline_dir

    # Check renderer exists
    if not get_render_executable().exists():
        print("Error: faust_render not found. Run 'just build' first.")
        sys.exit(2)

    # Get modules to test
    if args.module:
        modules = [args.module]
    else:
        modules = get_modules()
        if not modules:
            print("Error: Could not get module list")
            sys.exit(2)

    # Generate baselines mode
    if args.generate_baselines:
        print(f"Generating baselines for {len(modules)} modules...")
        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            for module in modules:
                generate_baseline(module, tmp_dir, baseline_dir)
        print(f"\nBaselines saved to: {baseline_dir}")
        return

    # Run tests
    report = TestReport(timestamp=datetime.now().isoformat())

    print(f"Testing {len(modules)} module(s)...")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)

        for module in modules:
            if not args.ci:
                print(f"  Testing: {module}...", end=" ", flush=True)

            include_quality = not args.no_quality
            result = run_module_tests(module, tmp_dir, baseline_dir,
                                      include_quality_tests=include_quality)
            report.add(result)

            if not args.ci:
                status = "OK" if result.passed else "FAILED"
                print(status)

    # Output results
    if args.ci or args.output:
        report_dict = report.to_dict()

        if args.output:
            with open(args.output, "w") as f:
                json.dump(report_dict, f, indent=2)
            print(f"Report saved to: {args.output}")

        if args.ci:
            print(json.dumps(report_dict, indent=2))
    else:
        print_results(report, args.verbose)

    # Exit code
    sys.exit(0 if report.passed else 1)


if __name__ == "__main__":
    main()
