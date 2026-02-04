#!/usr/bin/env python3
"""
Audio Quality Analysis for Faust DSP Modules

Provides functions to detect digital distortion artifacts and analyze
musical characteristics:
- THD (Total Harmonic Distortion)
- Aliasing detection
- Harmonic character (even vs odd harmonics)
- Spectral richness metrics
- Envelope analysis (attack, decay, transients)

Usage:
    python audio_quality.py --module ChaosFlute --report
    python audio_quality.py --module MoogLPF --test-thd
"""

import argparse
import json
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any

import numpy as np
from scipy import signal
from scipy import ndimage

# Try to import optional dependencies
try:
    import librosa
    HAS_LIBROSA = True
except ImportError:
    HAS_LIBROSA = False

# Configuration
SAMPLE_RATE = 48000
TEST_DURATION = 2.0

# Default quality thresholds (can be overridden by module config)
DEFAULT_QUALITY_THRESHOLDS = {
    "thd_max_percent": 15.0,
    "clipping_max_percent": 1.0,
    "hnr_min_db": 0.0,
    "allow_hot_signal": False
}


def load_ci_config() -> dict:
    """Load CI configuration from test/ci_config.json."""
    project_root = get_project_root()
    ci_config_path = project_root / "test" / "ci_config.json"

    if not ci_config_path.exists():
        return {}

    try:
        with open(ci_config_path) as f:
            return json.load(f)
    except (json.JSONDecodeError, IOError):
        return {}


def load_module_config(module_name: str) -> dict:
    """Load test configuration for a module from its test_config.json file."""
    project_root = get_project_root()
    config_path = project_root / "src" / "modules" / module_name / "test_config.json"

    if not config_path.exists():
        return {"thresholds": DEFAULT_QUALITY_THRESHOLDS.copy()}

    try:
        with open(config_path) as f:
            data = json.load(f)

        thresholds = DEFAULT_QUALITY_THRESHOLDS.copy()
        if "quality_thresholds" in data:
            qt = data["quality_thresholds"]
            thresholds["thd_max_percent"] = qt.get("thd_max_percent", 15.0)
            thresholds["clipping_max_percent"] = qt.get("clipping_max_percent", 1.0)
            thresholds["hnr_min_db"] = qt.get("hnr_min_db", 0.0)
            thresholds["allow_hot_signal"] = qt.get("allow_hot_signal", False)

        # Get first scenario for audio rendering
        first_scenario = None
        if "test_scenarios" in data and data["test_scenarios"]:
            first_scenario = data["test_scenarios"][0].get("name")

        return {
            "module_type": data.get("module_type", "instrument"),
            "skip_audio_tests": data.get("skip_audio_tests", False),
            "skip_reason": data.get("skip_reason", ""),
            "thresholds": thresholds,
            "first_scenario": first_scenario
        }
    except (json.JSONDecodeError, IOError):
        return {"thresholds": DEFAULT_QUALITY_THRESHOLDS.copy(), "first_scenario": None}


# =============================================================================
# Data Classes
# =============================================================================

@dataclass
class THDResult:
    """Total Harmonic Distortion measurement result."""
    thd_percent: float
    fundamental_freq: float
    fundamental_magnitude: float
    harmonics: list[tuple[int, float, float]]  # (harmonic_num, freq, magnitude)
    noise_floor_db: float


@dataclass
class AliasingResult:
    """Aliasing detection result."""
    alias_ratio: float
    alias_ratio_db: float
    detected_aliases: list[tuple[float, float]]  # (frequency, magnitude)
    input_frequency: float
    has_significant_aliasing: bool


@dataclass
class HarmonicCharacter:
    """Harmonic character analysis result."""
    even_harmonic_energy: float
    odd_harmonic_energy: float
    warmth_ratio: float  # even/odd ratio, higher = warmer
    dominant_harmonics: list[tuple[int, float]]  # (harmonic_num, magnitude)
    character: str  # "warm", "neutral", "bright/edgy"


@dataclass
class SpectralRichness:
    """Spectral richness metrics."""
    spectral_entropy: float  # 0-1, higher = more complex
    spectral_flatness: float  # 0-1, higher = more noise-like
    spectral_spread: float  # Hz, frequency deviation from centroid
    harmonic_to_noise_ratio: float  # dB
    crest_factor: float  # peak/RMS ratio
    dynamic_range_db: float


@dataclass
class EnvelopeAnalysis:
    """Envelope and transient analysis result."""
    attack_time_ms: float | None
    decay_time_ms: float | None
    release_time_ms: float | None
    peak_amplitude: float
    sustain_level: float | None
    onset_strength: float  # transient sharpness
    has_clear_envelope: bool


@dataclass
class AudioQualityReport:
    """Complete audio quality analysis report."""
    module_name: str
    thd: THDResult | None = None
    aliasing: AliasingResult | None = None
    harmonics: HarmonicCharacter | None = None
    spectral: SpectralRichness | None = None
    envelope: EnvelopeAnalysis | None = None
    overall_quality_score: float = 0.0
    issues: list[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        result = {"module_name": self.module_name}
        if self.thd:
            result["thd"] = asdict(self.thd)
        if self.aliasing:
            result["aliasing"] = asdict(self.aliasing)
        if self.harmonics:
            result["harmonics"] = asdict(self.harmonics)
        if self.spectral:
            result["spectral"] = asdict(self.spectral)
        if self.envelope:
            result["envelope"] = asdict(self.envelope)
        result["overall_quality_score"] = self.overall_quality_score
        result["issues"] = self.issues
        return result


# =============================================================================
# Utility Functions
# =============================================================================

def get_project_root() -> Path:
    return Path(__file__).parent.parent


def get_render_executable() -> Path:
    project_root = get_project_root()
    exe = project_root / "build" / "test" / "faust_render"
    if not exe.exists():
        exe = project_root / "build" / "faust_render"
    return exe


def run_faust_render(args: list[str]) -> tuple[bool, str]:
    exe = get_render_executable()
    if not exe.exists():
        return False, f"Executable not found: {exe}"

    cmd = [str(exe)] + args
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            return False, result.stderr
        return True, result.stdout
    except Exception as e:
        return False, str(e)


def render_audio(module_name: str, params: dict[str, float],
                 output_path: Path, duration: float = TEST_DURATION,
                 no_auto_gate: bool = False, scenario: str | None = None) -> bool:
    args = [
        "--module", module_name,
        "--output", str(output_path),
        "--duration", str(duration),
        "--sample-rate", str(SAMPLE_RATE),
    ]
    if no_auto_gate:
        args.append("--no-auto-gate")
    if scenario:
        args.extend(["--scenario", scenario])
    for name, value in params.items():
        args.extend(["--param", f"{name}={value}"])

    success, _ = run_faust_render(args)
    return success


def load_audio(path: Path) -> np.ndarray | None:
    """Load audio file as numpy array."""
    if HAS_LIBROSA:
        try:
            y, sr = librosa.load(str(path), sr=SAMPLE_RATE, mono=True)
            return y
        except Exception:
            pass

    # Fallback to scipy
    try:
        from scipy.io import wavfile
        sr, data = wavfile.read(str(path))
        if data.dtype == np.int16:
            data = data.astype(np.float32) / 32768.0
        if data.ndim > 1:
            data = data.mean(axis=1)
        return data
    except Exception:
        return None


def db_to_linear(db: float) -> float:
    return 10 ** (db / 20)


def linear_to_db(linear: float) -> float:
    if linear <= 0:
        return -120.0
    return 20 * np.log10(linear)


# =============================================================================
# THD (Total Harmonic Distortion) Analysis
# =============================================================================

def measure_thd(audio: np.ndarray, sr: int = SAMPLE_RATE,
                fundamental_freq: float | None = None,
                num_harmonics: int = 10) -> THDResult:
    """
    Measure Total Harmonic Distortion of audio signal.

    THD = sqrt(sum(V_n^2 for n=2..N)) / V_1
    where V_1 is fundamental magnitude, V_n are harmonic magnitudes.

    Args:
        audio: Audio samples (mono)
        sr: Sample rate
        fundamental_freq: Known fundamental frequency, or None to detect
        num_harmonics: Number of harmonics to measure

    Returns:
        THDResult with measurements
    """
    # Use a stable portion of audio (skip attack)
    start = int(0.2 * sr)
    end = min(len(audio), int(1.5 * sr))
    if end - start < sr // 4:
        start = 0
        end = len(audio)
    segment = audio[start:end]

    # Apply window to reduce spectral leakage
    window = np.hanning(len(segment))
    windowed = segment * window

    # Compute FFT
    n_fft = len(windowed)
    fft = np.fft.rfft(windowed)
    freqs = np.fft.rfftfreq(n_fft, 1/sr)
    magnitudes = np.abs(fft) / (n_fft / 2)  # Normalize

    # Find fundamental frequency if not provided
    if fundamental_freq is None:
        # Look for strongest peak in 50-2000 Hz range
        min_idx = np.searchsorted(freqs, 50)
        max_idx = np.searchsorted(freqs, 2000)
        search_region = magnitudes[min_idx:max_idx]
        if len(search_region) > 0:
            peak_idx = np.argmax(search_region) + min_idx
            fundamental_freq = freqs[peak_idx]
        else:
            fundamental_freq = 440.0  # Default

    # Get fundamental magnitude (search in window around expected freq)
    freq_resolution = sr / n_fft
    window_bins = max(3, int(10 / freq_resolution))  # ~10 Hz window

    fund_idx = np.argmin(np.abs(freqs - fundamental_freq))
    fund_start = max(0, fund_idx - window_bins)
    fund_end = min(len(magnitudes), fund_idx + window_bins + 1)
    fund_mag = np.max(magnitudes[fund_start:fund_end])

    if fund_mag < 1e-10:
        return THDResult(
            thd_percent=0.0,
            fundamental_freq=fundamental_freq,
            fundamental_magnitude=0.0,
            harmonics=[],
            noise_floor_db=-120.0
        )

    # Measure harmonics
    harmonics = []
    harmonic_sum_sq = 0.0

    for n in range(2, num_harmonics + 1):
        harm_freq = fundamental_freq * n
        if harm_freq > sr / 2 - 100:  # Stay below Nyquist with margin
            break

        harm_idx = np.argmin(np.abs(freqs - harm_freq))
        harm_start = max(0, harm_idx - window_bins)
        harm_end = min(len(magnitudes), harm_idx + window_bins + 1)
        harm_mag = np.max(magnitudes[harm_start:harm_end])

        harmonics.append((n, harm_freq, float(harm_mag)))
        harmonic_sum_sq += harm_mag ** 2

    # Calculate THD
    thd = np.sqrt(harmonic_sum_sq) / fund_mag if fund_mag > 0 else 0.0
    thd_percent = thd * 100

    # Estimate noise floor (median of non-harmonic bins)
    harmonic_indices = set()
    for n in range(1, num_harmonics + 1):
        idx = np.argmin(np.abs(freqs - fundamental_freq * n))
        for i in range(max(0, idx - window_bins), min(len(magnitudes), idx + window_bins + 1)):
            harmonic_indices.add(i)

    noise_bins = [magnitudes[i] for i in range(len(magnitudes)) if i not in harmonic_indices]
    noise_floor = np.median(noise_bins) if noise_bins else 0.0
    noise_floor_db = linear_to_db(noise_floor) if noise_floor > 0 else -120.0

    return THDResult(
        thd_percent=float(thd_percent),
        fundamental_freq=float(fundamental_freq),
        fundamental_magnitude=float(fund_mag),
        harmonics=harmonics,
        noise_floor_db=float(noise_floor_db)
    )


# =============================================================================
# Aliasing Detection
# =============================================================================

def detect_aliasing(audio: np.ndarray, sr: int = SAMPLE_RATE,
                    input_freq: float | None = None) -> AliasingResult:
    """
    Detect aliasing artifacts in audio.

    Aliasing occurs when frequencies above Nyquist (sr/2) fold back into
    the audible range. Common alias frequencies: |n*sr - f| for integer n.

    Args:
        audio: Audio samples
        sr: Sample rate
        input_freq: Known input frequency (for filter/effect testing)

    Returns:
        AliasingResult with detection info
    """
    # Use stable portion
    start = int(0.2 * sr)
    end = min(len(audio), int(1.5 * sr))
    if end - start < sr // 4:
        start = 0
        end = len(audio)
    segment = audio[start:end]

    # FFT analysis
    window = np.hanning(len(segment))
    windowed = segment * window
    fft = np.fft.rfft(windowed)
    freqs = np.fft.rfftfreq(len(windowed), 1/sr)
    magnitudes = np.abs(fft)

    # Find fundamental if not provided
    if input_freq is None:
        min_idx = np.searchsorted(freqs, 50)
        max_idx = np.searchsorted(freqs, 5000)
        if max_idx > min_idx:
            peak_idx = np.argmax(magnitudes[min_idx:max_idx]) + min_idx
            input_freq = freqs[peak_idx]
        else:
            input_freq = 440.0

    # Calculate expected alias frequencies
    # Aliases appear at |k*sr ± f| for integer k
    nyquist = sr / 2
    expected_aliases = []

    for k in range(1, 4):
        # Fold-over aliases
        alias_f1 = abs(k * sr - input_freq)
        alias_f2 = abs(k * sr + input_freq)

        for af in [alias_f1, alias_f2]:
            # Fold into audible range
            while af > nyquist:
                af = abs(sr - af)
            if 20 < af < nyquist - 100:
                expected_aliases.append(af)

    # Also check for harmonic-related aliases
    for harm in range(2, 8):
        harm_freq = input_freq * harm
        if harm_freq > nyquist:
            alias_f = sr - harm_freq
            while alias_f < 0 or alias_f > nyquist:
                if alias_f < 0:
                    alias_f = -alias_f
                if alias_f > nyquist:
                    alias_f = sr - alias_f
            if 20 < alias_f < nyquist - 100:
                expected_aliases.append(alias_f)

    # Measure energy at alias frequencies
    freq_resolution = sr / len(windowed)
    window_bins = max(3, int(20 / freq_resolution))

    detected_aliases = []
    alias_energy = 0.0

    fundamental_idx = np.argmin(np.abs(freqs - input_freq))
    fundamental_mag = magnitudes[fundamental_idx]

    for af in expected_aliases:
        # Skip if too close to fundamental or its harmonics
        is_near_harmonic = False
        for h in range(1, 10):
            if abs(af - input_freq * h) < 50:
                is_near_harmonic = True
                break
        if is_near_harmonic:
            continue

        idx = np.argmin(np.abs(freqs - af))
        start_idx = max(0, idx - window_bins)
        end_idx = min(len(magnitudes), idx + window_bins + 1)
        alias_mag = np.max(magnitudes[start_idx:end_idx])

        # Only count if above noise floor
        noise_threshold = np.median(magnitudes) * 3
        if alias_mag > noise_threshold:
            detected_aliases.append((float(af), float(alias_mag)))
            alias_energy += alias_mag ** 2

    # Calculate alias ratio
    total_energy = np.sum(magnitudes ** 2)
    alias_ratio = np.sqrt(alias_energy) / np.sqrt(total_energy) if total_energy > 0 else 0.0
    alias_ratio_db = linear_to_db(alias_ratio) if alias_ratio > 0 else -120.0

    # Significant aliasing threshold: > -60dB relative to total
    has_significant = alias_ratio_db > -60

    return AliasingResult(
        alias_ratio=float(alias_ratio),
        alias_ratio_db=float(alias_ratio_db),
        detected_aliases=detected_aliases,
        input_frequency=float(input_freq),
        has_significant_aliasing=has_significant
    )


# =============================================================================
# Harmonic Character Analysis
# =============================================================================

def analyze_harmonics(audio: np.ndarray, sr: int = SAMPLE_RATE,
                      fundamental_freq: float | None = None) -> HarmonicCharacter:
    """
    Analyze harmonic character - even vs odd harmonics.

    Even harmonics (2nd, 4th, 6th) produce a "warm" sound (tube-like).
    Odd harmonics (3rd, 5th, 7th) produce a "bright/edgy" sound.

    Args:
        audio: Audio samples
        sr: Sample rate
        fundamental_freq: Known fundamental, or None to detect

    Returns:
        HarmonicCharacter with analysis
    """
    # Use stable portion
    start = int(0.2 * sr)
    end = min(len(audio), int(1.5 * sr))
    if end - start < sr // 4:
        start = 0
        end = len(audio)
    segment = audio[start:end]

    # FFT
    window = np.hanning(len(segment))
    windowed = segment * window
    fft = np.fft.rfft(windowed)
    freqs = np.fft.rfftfreq(len(windowed), 1/sr)
    magnitudes = np.abs(fft)

    # Find fundamental
    if fundamental_freq is None:
        min_idx = np.searchsorted(freqs, 50)
        max_idx = np.searchsorted(freqs, 2000)
        if max_idx > min_idx:
            peak_idx = np.argmax(magnitudes[min_idx:max_idx]) + min_idx
            fundamental_freq = freqs[peak_idx]
        else:
            fundamental_freq = 440.0

    # Measure harmonics
    freq_resolution = sr / len(windowed)
    window_bins = max(3, int(10 / freq_resolution))

    even_energy = 0.0
    odd_energy = 0.0
    dominant_harmonics = []

    fund_idx = np.argmin(np.abs(freqs - fundamental_freq))
    fund_mag = magnitudes[fund_idx]

    for n in range(2, 16):
        harm_freq = fundamental_freq * n
        if harm_freq > sr / 2 - 100:
            break

        harm_idx = np.argmin(np.abs(freqs - harm_freq))
        start_idx = max(0, harm_idx - window_bins)
        end_idx = min(len(magnitudes), harm_idx + window_bins + 1)
        harm_mag = np.max(magnitudes[start_idx:end_idx])

        # Normalize to fundamental
        rel_mag = harm_mag / fund_mag if fund_mag > 0 else 0.0

        if n % 2 == 0:
            even_energy += harm_mag ** 2
        else:
            odd_energy += harm_mag ** 2

        if rel_mag > 0.01:  # > 1% of fundamental
            dominant_harmonics.append((n, float(rel_mag)))

    # Sort by magnitude
    dominant_harmonics.sort(key=lambda x: x[1], reverse=True)
    dominant_harmonics = dominant_harmonics[:5]  # Top 5

    # Calculate warmth ratio
    even_rms = np.sqrt(even_energy) if even_energy > 0 else 0.0
    odd_rms = np.sqrt(odd_energy) if odd_energy > 0 else 1e-10
    warmth_ratio = even_rms / odd_rms

    # Classify character
    if warmth_ratio > 1.5:
        character = "warm"
    elif warmth_ratio < 0.5:
        character = "bright/edgy"
    else:
        character = "neutral"

    return HarmonicCharacter(
        even_harmonic_energy=float(even_rms),
        odd_harmonic_energy=float(odd_rms),
        warmth_ratio=float(warmth_ratio),
        dominant_harmonics=dominant_harmonics,
        character=character
    )


# =============================================================================
# Spectral Richness Metrics
# =============================================================================

def compute_spectral_richness(audio: np.ndarray, sr: int = SAMPLE_RATE) -> SpectralRichness:
    """
    Compute spectral richness metrics.

    Args:
        audio: Audio samples
        sr: Sample rate

    Returns:
        SpectralRichness with metrics
    """
    # Skip silent audio
    rms = np.sqrt(np.mean(audio ** 2))
    if rms < 1e-6:
        return SpectralRichness(
            spectral_entropy=0.0,
            spectral_flatness=0.0,
            spectral_spread=0.0,
            harmonic_to_noise_ratio=0.0,
            crest_factor=0.0,
            dynamic_range_db=0.0
        )

    # Use stable portion
    start = int(0.1 * sr)
    end = min(len(audio), int(1.5 * sr))
    if end - start < sr // 4:
        start = 0
        end = len(audio)
    segment = audio[start:end]

    # Compute spectrogram
    n_fft = 2048
    hop_length = 512

    # STFT
    num_frames = (len(segment) - n_fft) // hop_length + 1
    if num_frames < 1:
        num_frames = 1

    spectrogram = []
    for i in range(num_frames):
        frame_start = i * hop_length
        frame = segment[frame_start:frame_start + n_fft]
        if len(frame) < n_fft:
            frame = np.pad(frame, (0, n_fft - len(frame)))
        windowed = frame * np.hanning(n_fft)
        fft = np.fft.rfft(windowed)
        spectrogram.append(np.abs(fft))

    spectrogram = np.array(spectrogram).T  # (freq_bins, time_frames)
    freqs = np.fft.rfftfreq(n_fft, 1/sr)

    # Average spectrum
    avg_spectrum = np.mean(spectrogram, axis=1)

    # Spectral entropy (normalized)
    # Higher = more uniformly distributed energy = more complex/noisy
    spectrum_norm = avg_spectrum / (np.sum(avg_spectrum) + 1e-10)
    spectral_entropy = -np.sum(spectrum_norm * np.log2(spectrum_norm + 1e-10))
    max_entropy = np.log2(len(avg_spectrum))
    spectral_entropy_norm = spectral_entropy / max_entropy if max_entropy > 0 else 0.0

    # Spectral flatness (Wiener entropy)
    # Higher = more noise-like, Lower = more tonal
    geometric_mean = np.exp(np.mean(np.log(avg_spectrum + 1e-10)))
    arithmetic_mean = np.mean(avg_spectrum)
    spectral_flatness = geometric_mean / (arithmetic_mean + 1e-10)

    # Spectral spread (standard deviation around centroid)
    centroid_idx = np.sum(freqs * avg_spectrum) / (np.sum(avg_spectrum) + 1e-10)
    spectral_spread = np.sqrt(
        np.sum(((freqs - centroid_idx) ** 2) * avg_spectrum) / (np.sum(avg_spectrum) + 1e-10)
    )

    # Harmonic-to-Noise Ratio (simplified estimation)
    # Use autocorrelation to estimate periodicity
    autocorr = np.correlate(segment[:sr], segment[:sr], mode='full')
    autocorr = autocorr[len(autocorr)//2:]
    autocorr = autocorr / (autocorr[0] + 1e-10)

    # Find first significant peak after zero-lag
    min_lag = sr // 2000  # Max 2kHz
    max_lag = sr // 50    # Min 50Hz
    if max_lag < len(autocorr):
        search_region = autocorr[min_lag:max_lag]
        if len(search_region) > 0:
            peak_val = np.max(search_region)
            # HNR estimation: ratio of periodic to aperiodic energy
            hnr_linear = peak_val / (1 - peak_val + 1e-10) if peak_val < 1 else 100
            hnr_db = 10 * np.log10(max(hnr_linear, 1e-10))
        else:
            hnr_db = 0.0
    else:
        hnr_db = 0.0

    # Crest factor (peak/RMS)
    peak = np.max(np.abs(segment))
    segment_rms = np.sqrt(np.mean(segment ** 2))
    crest_factor = peak / segment_rms if segment_rms > 0 else 0.0

    # Dynamic range
    # Use percentile-based estimation
    envelope = np.abs(signal.hilbert(segment))
    envelope_smooth = ndimage.uniform_filter1d(envelope, size=int(sr * 0.05))

    loud_level = np.percentile(envelope_smooth, 95)
    quiet_level = np.percentile(envelope_smooth[envelope_smooth > 0.001], 5) if np.any(envelope_smooth > 0.001) else 0.001
    dynamic_range_db = linear_to_db(loud_level / quiet_level) if quiet_level > 0 else 0.0

    return SpectralRichness(
        spectral_entropy=float(spectral_entropy_norm),
        spectral_flatness=float(spectral_flatness),
        spectral_spread=float(spectral_spread),
        harmonic_to_noise_ratio=float(hnr_db),
        crest_factor=float(crest_factor),
        dynamic_range_db=float(dynamic_range_db)
    )


# =============================================================================
# Envelope Analysis
# =============================================================================

def analyze_envelope(audio: np.ndarray, sr: int = SAMPLE_RATE) -> EnvelopeAnalysis:
    """
    Analyze envelope characteristics (attack, decay, release, transients).

    Args:
        audio: Audio samples
        sr: Sample rate

    Returns:
        EnvelopeAnalysis with measurements
    """
    # Skip silent audio
    rms = np.sqrt(np.mean(audio ** 2))
    if rms < 1e-6:
        return EnvelopeAnalysis(
            attack_time_ms=None,
            decay_time_ms=None,
            release_time_ms=None,
            peak_amplitude=0.0,
            sustain_level=None,
            onset_strength=0.0,
            has_clear_envelope=False
        )

    # Compute amplitude envelope using Hilbert transform
    analytic = signal.hilbert(audio)
    envelope = np.abs(analytic)

    # Smooth envelope (10ms window)
    smooth_samples = int(sr * 0.01)
    envelope_smooth = ndimage.uniform_filter1d(envelope, size=smooth_samples)

    # Find peak
    peak_idx = np.argmax(envelope_smooth)
    peak_amplitude = envelope_smooth[peak_idx]

    if peak_amplitude < 1e-6:
        return EnvelopeAnalysis(
            attack_time_ms=None,
            decay_time_ms=None,
            release_time_ms=None,
            peak_amplitude=0.0,
            sustain_level=None,
            onset_strength=0.0,
            has_clear_envelope=False
        )

    # Attack time (10% to 90% of peak)
    thresh_10 = 0.1 * peak_amplitude
    thresh_90 = 0.9 * peak_amplitude

    attack_region = envelope_smooth[:peak_idx + 1]

    attack_start_indices = np.where(attack_region > thresh_10)[0]
    attack_end_indices = np.where(attack_region > thresh_90)[0]

    attack_time_ms = None
    if len(attack_start_indices) > 0 and len(attack_end_indices) > 0:
        attack_start = attack_start_indices[0]
        attack_end = attack_end_indices[0]
        if attack_end > attack_start:
            attack_time_ms = (attack_end - attack_start) / sr * 1000

    # Decay time (peak to 50% or sustain level)
    decay_region = envelope_smooth[peak_idx:]

    # Estimate sustain level (average of latter portion)
    sustain_start = int(len(decay_region) * 0.3)
    sustain_end = int(len(decay_region) * 0.7)
    if sustain_end > sustain_start:
        sustain_level = np.mean(decay_region[sustain_start:sustain_end])
    else:
        sustain_level = None

    decay_time_ms = None
    if sustain_level is not None and sustain_level < peak_amplitude * 0.9:
        decay_target = sustain_level + (peak_amplitude - sustain_level) * 0.5
        decay_indices = np.where(decay_region < decay_target)[0]
        if len(decay_indices) > 0:
            decay_time_ms = decay_indices[0] / sr * 1000

    # Release time (from 90% sustain to 10% sustain)
    # Look at end of signal
    release_time_ms = None
    if sustain_level is not None and sustain_level > thresh_10:
        release_start_thresh = 0.9 * sustain_level
        release_end_thresh = 0.1 * sustain_level

        # Find where envelope drops below thresholds
        release_region = envelope_smooth[int(len(envelope_smooth) * 0.5):]
        if len(release_region) > 0:
            below_start = np.where(release_region < release_start_thresh)[0]
            below_end = np.where(release_region < release_end_thresh)[0]

            if len(below_start) > 0 and len(below_end) > 0:
                release_start_idx = below_start[0]
                release_end_idx = below_end[0]
                if release_end_idx > release_start_idx:
                    release_time_ms = (release_end_idx - release_start_idx) / sr * 1000

    # Onset strength (transient sharpness)
    # Use derivative of envelope
    envelope_diff = np.diff(envelope_smooth)
    onset_strength = float(np.max(envelope_diff) / peak_amplitude) if peak_amplitude > 0 else 0.0

    # Determine if there's a clear envelope shape
    has_clear_envelope = (
        attack_time_ms is not None and
        peak_amplitude > 0.01 and
        onset_strength > 0.001
    )

    return EnvelopeAnalysis(
        attack_time_ms=attack_time_ms,
        decay_time_ms=decay_time_ms,
        release_time_ms=release_time_ms,
        peak_amplitude=float(peak_amplitude),
        sustain_level=float(sustain_level) if sustain_level is not None else None,
        onset_strength=onset_strength,
        has_clear_envelope=has_clear_envelope
    )


# =============================================================================
# Complete Audio Quality Analysis
# =============================================================================

def analyze_audio_quality(audio: np.ndarray, sr: int = SAMPLE_RATE,
                          module_name: str = "Unknown",
                          thresholds: dict | None = None) -> AudioQualityReport:
    """
    Run complete audio quality analysis.

    Args:
        audio: Audio samples
        sr: Sample rate
        module_name: Name of the module being tested
        thresholds: Optional dict with quality thresholds (thd_max_percent, hnr_min_db, etc.)

    Returns:
        AudioQualityReport with all metrics
    """
    # Use provided thresholds or defaults
    if thresholds is None:
        thresholds = DEFAULT_QUALITY_THRESHOLDS.copy()

    thd_max = thresholds.get("thd_max_percent", 15.0)
    hnr_min = thresholds.get("hnr_min_db", 0.0)

    report = AudioQualityReport(module_name=module_name)
    issues = []
    quality_score = 100.0

    # Skip silent audio
    rms = np.sqrt(np.mean(audio ** 2))
    if rms < 1e-6:
        report.issues = ["Audio is silent"]
        report.overall_quality_score = 0.0
        return report

    # THD Analysis
    try:
        thd_result = measure_thd(audio, sr)
        report.thd = thd_result

        if thd_result.thd_percent > thd_max:
            issues.append(f"High THD: {thd_result.thd_percent:.1f}% (threshold: {thd_max}%)")
            quality_score -= 20
        elif thd_result.thd_percent > thd_max * 0.5:
            issues.append(f"Moderate THD: {thd_result.thd_percent:.1f}%")
            quality_score -= 5
    except Exception as e:
        issues.append(f"THD analysis failed: {e}")

    # Aliasing Detection
    try:
        aliasing_result = detect_aliasing(audio, sr)
        report.aliasing = aliasing_result

        if aliasing_result.has_significant_aliasing:
            issues.append(f"Aliasing detected: {aliasing_result.alias_ratio_db:.1f} dB")
            quality_score -= 15
    except Exception as e:
        issues.append(f"Aliasing detection failed: {e}")

    # Harmonic Character
    try:
        harmonic_result = analyze_harmonics(audio, sr)
        report.harmonics = harmonic_result
    except Exception as e:
        issues.append(f"Harmonic analysis failed: {e}")

    # Spectral Richness
    try:
        spectral_result = compute_spectral_richness(audio, sr)
        report.spectral = spectral_result

        # Check for issues
        if spectral_result.crest_factor > 20:
            issues.append(f"Very high crest factor: {spectral_result.crest_factor:.1f}")

        if spectral_result.harmonic_to_noise_ratio < hnr_min:
            issues.append(f"Low HNR: {spectral_result.harmonic_to_noise_ratio:.1f} dB (threshold: {hnr_min} dB)")
            quality_score -= 5
    except Exception as e:
        issues.append(f"Spectral analysis failed: {e}")

    # Envelope Analysis
    try:
        envelope_result = analyze_envelope(audio, sr)
        report.envelope = envelope_result
    except Exception as e:
        issues.append(f"Envelope analysis failed: {e}")

    # Final score
    report.issues = issues
    report.overall_quality_score = max(0, quality_score)

    return report


# =============================================================================
# Module Testing Interface
# =============================================================================

def get_modules() -> list[str]:
    """Get list of available modules."""
    success, output = run_faust_render(["--list-modules"])
    if not success:
        return []
    return [line.strip() for line in output.strip().split("\n")
            if line.strip() and not line.startswith("Available")]


def test_module_quality(module_name: str, tmp_dir: Path,
                        thresholds: dict | None = None) -> AudioQualityReport:
    """
    Test audio quality for a specific module.

    Args:
        module_name: Name of the module
        tmp_dir: Temporary directory for rendered audio
        thresholds: Optional dict with quality thresholds (overrides module config)

    Returns:
        AudioQualityReport
    """
    # Load module config if no thresholds provided
    config = load_module_config(module_name)
    if thresholds is None:
        thresholds = config.get("thresholds", DEFAULT_QUALITY_THRESHOLDS.copy())

    # Check if module should skip audio tests
    if config.get("skip_audio_tests", False):
        report = AudioQualityReport(module_name=module_name)
        report.overall_quality_score = 100.0
        report.issues = [f"Audio tests skipped: {config.get('skip_reason', 'utility module')}"]
        return report

    wav_path = tmp_dir / f"{module_name}_quality.wav"

    # Get first scenario if available (for trigger-based modules like drums)
    first_scenario = config.get("first_scenario")

    # Render with default parameters and scenario if available
    if not render_audio(module_name, {}, wav_path, duration=3.0, scenario=first_scenario):
        report = AudioQualityReport(module_name=module_name)
        report.issues = ["Failed to render audio"]
        return report

    audio = load_audio(wav_path)
    if audio is None:
        report = AudioQualityReport(module_name=module_name)
        report.issues = ["Failed to load audio"]
        return report

    return analyze_audio_quality(audio, SAMPLE_RATE, module_name, thresholds)


def print_quality_report(report: AudioQualityReport, verbose: bool = False):
    """Print quality report to console."""
    print(f"\n{'=' * 60}")
    print(f"Audio Quality Report: {report.module_name}")
    print(f"{'=' * 60}")

    # Overall score
    score = report.overall_quality_score
    if score >= 90:
        score_color = "\033[92m"  # Green
    elif score >= 70:
        score_color = "\033[93m"  # Yellow
    else:
        score_color = "\033[91m"  # Red
    reset = "\033[0m"

    print(f"\nOverall Quality Score: {score_color}{score:.0f}/100{reset}")

    # THD
    if report.thd:
        print(f"\nTHD (Total Harmonic Distortion):")
        print(f"  THD: {report.thd.thd_percent:.2f}%")
        print(f"  Fundamental: {report.thd.fundamental_freq:.1f} Hz")
        if verbose and report.thd.harmonics:
            print(f"  Harmonics:")
            for n, freq, mag in report.thd.harmonics[:5]:
                print(f"    {n}th: {freq:.0f} Hz ({linear_to_db(mag):.1f} dB)")

    # Aliasing
    if report.aliasing:
        print(f"\nAliasing Detection:")
        status = "DETECTED" if report.aliasing.has_significant_aliasing else "Clean"
        print(f"  Status: {status}")
        print(f"  Alias Ratio: {report.aliasing.alias_ratio_db:.1f} dB")
        if verbose and report.aliasing.detected_aliases:
            print(f"  Detected alias frequencies:")
            for freq, mag in report.aliasing.detected_aliases[:3]:
                print(f"    {freq:.0f} Hz ({linear_to_db(mag):.1f} dB)")

    # Harmonic Character
    if report.harmonics:
        print(f"\nHarmonic Character:")
        print(f"  Character: {report.harmonics.character}")
        print(f"  Warmth Ratio (even/odd): {report.harmonics.warmth_ratio:.2f}")
        if verbose and report.harmonics.dominant_harmonics:
            print(f"  Dominant harmonics:")
            for n, rel_mag in report.harmonics.dominant_harmonics[:3]:
                print(f"    {n}th: {rel_mag*100:.1f}% of fundamental")

    # Spectral Richness
    if report.spectral:
        print(f"\nSpectral Richness:")
        print(f"  Entropy: {report.spectral.spectral_entropy:.3f}")
        print(f"  Flatness: {report.spectral.spectral_flatness:.3f}")
        print(f"  HNR: {report.spectral.harmonic_to_noise_ratio:.1f} dB")
        print(f"  Crest Factor: {report.spectral.crest_factor:.2f}")
        print(f"  Dynamic Range: {report.spectral.dynamic_range_db:.1f} dB")

    # Envelope
    if report.envelope:
        print(f"\nEnvelope Analysis:")
        if report.envelope.attack_time_ms is not None:
            print(f"  Attack: {report.envelope.attack_time_ms:.1f} ms")
        if report.envelope.decay_time_ms is not None:
            print(f"  Decay: {report.envelope.decay_time_ms:.1f} ms")
        if report.envelope.release_time_ms is not None:
            print(f"  Release: {report.envelope.release_time_ms:.1f} ms")
        print(f"  Peak Amplitude: {report.envelope.peak_amplitude:.3f}")
        print(f"  Onset Strength: {report.envelope.onset_strength:.4f}")

    # Issues
    if report.issues:
        print(f"\nIssues:")
        for issue in report.issues:
            print(f"  - {issue}")

    print()


# =============================================================================
# Main CLI
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Audio quality analysis for Faust DSP modules"
    )
    parser.add_argument("--module", help="Specific module to analyze")
    parser.add_argument("--all-modules", action="store_true",
                       help="Analyze all available modules")
    parser.add_argument("--report", action="store_true",
                       help="Generate detailed report")
    parser.add_argument("--json", action="store_true",
                       help="Output as JSON")
    parser.add_argument("--output", help="Output file for JSON report")
    parser.add_argument("-v", "--verbose", action="store_true",
                       help="Verbose output")
    parser.add_argument("--test-thd", action="store_true",
                       help="Run THD test only")
    parser.add_argument("--test-aliasing", action="store_true",
                       help="Run aliasing test only")
    parser.add_argument("--ci", action="store_true",
                       help="CI mode: use ci_config.json thresholds, JSON output, exit code")

    args = parser.parse_args()

    # Load CI config if in CI mode
    ci_config = {}
    if args.ci:
        ci_config = load_ci_config()
        args.json = True  # Force JSON output in CI mode

    # Check executable
    if not get_render_executable().exists():
        print("Error: faust_render not found. Run 'just build' first.")
        sys.exit(1)

    # Get modules
    if args.module:
        modules = [args.module]
    elif args.all_modules or args.ci:
        modules = get_modules()
        if not modules:
            print("Error: No modules found")
            sys.exit(1)
    else:
        modules = get_modules()
        if not modules:
            print("Error: No modules found")
            sys.exit(1)

    import tempfile
    all_reports = []

    # Get CI quality gates if in CI mode
    ci_quality_gates = ci_config.get("quality_gates", {}) if args.ci else {}

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)

        for module_name in modules:
            if not args.json:
                print(f"Analyzing: {module_name}...")

            # In CI mode, use CI thresholds; otherwise use module config
            if args.ci and ci_quality_gates:
                ci_thresholds = {
                    "thd_max_percent": ci_quality_gates.get("thd_max_percent", 20.0),
                    "clipping_max_percent": ci_quality_gates.get("clipping_max_percent", 5.0),
                    "hnr_min_db": ci_quality_gates.get("hnr_min_db", -5.0),
                    "allow_hot_signal": False
                }
                report = test_module_quality(module_name, tmp_dir, thresholds=ci_thresholds)
            else:
                report = test_module_quality(module_name, tmp_dir)

            all_reports.append(report)

            if not args.json:
                print_quality_report(report, args.verbose)

    # JSON output
    if args.json or args.output:
        def numpy_encoder(obj):
            """Handle numpy types for JSON serialization."""
            if hasattr(np, 'integer') and isinstance(obj, np.integer):
                return int(obj)
            elif hasattr(np, 'floating') and isinstance(obj, np.floating):
                return float(obj)
            elif hasattr(np, 'bool_') and isinstance(obj, np.bool_):
                return bool(obj)
            elif hasattr(np, 'ndarray') and isinstance(obj, np.ndarray):
                return obj.tolist()
            raise TypeError(f"Object of type {type(obj)} is not JSON serializable")

        reports_dict = [r.to_dict() for r in all_reports]
        json_str = json.dumps(reports_dict, indent=2, default=numpy_encoder)

        if args.output:
            with open(args.output, "w") as f:
                f.write(json_str)
            print(f"Report saved to: {args.output}")
        else:
            print(json_str)

    # Exit code based on quality
    if args.ci:
        # In CI mode, use CI config threshold
        min_quality_score = ci_quality_gates.get("clap_quality_min", 50)
        min_score = min(r.overall_quality_score for r in all_reports) if all_reports else 0

        # Also check for critical issues
        ci_passed = min_score >= min_quality_score
        for report in all_reports:
            if "Audio is silent" in report.issues:
                if not ci_quality_gates.get("silent_output", False):
                    ci_passed = False
                    break

        sys.exit(0 if ci_passed else 1)
    else:
        min_score = min(r.overall_quality_score for r in all_reports) if all_reports else 0
        sys.exit(0 if min_score >= 70 else 1)


if __name__ == "__main__":
    main()
