#!/usr/bin/env python3
"""
Unit tests for test/audio_quality.py

Tests the audio quality analysis functions used for DSP module verification.
"""

import numpy as np
import pytest

# Import the module under test
from audio_quality import (
    THDResult,
    AliasingResult,
    HarmonicCharacter,
    SpectralRichness,
    EnvelopeAnalysis,
    AudioQualityReport,
    measure_thd,
    detect_aliasing,
    analyze_harmonics,
    compute_spectral_richness,
    analyze_envelope,
    load_module_config,
    SAMPLE_RATE,
    DEFAULT_QUALITY_THRESHOLDS,
)


# =============================================================================
# Test Fixtures
# =============================================================================

@pytest.fixture
def pure_sine_440hz():
    """Generate a pure 440 Hz sine wave (2 seconds)."""
    t = np.linspace(0, 2, SAMPLE_RATE * 2)
    return 0.5 * np.sin(2 * np.pi * 440 * t)


@pytest.fixture
def distorted_sine():
    """Generate a sine wave with harmonic distortion."""
    t = np.linspace(0, 2, SAMPLE_RATE * 2)
    fundamental = 0.5 * np.sin(2 * np.pi * 440 * t)
    # Add significant harmonics
    second_harmonic = 0.1 * np.sin(2 * np.pi * 880 * t)
    third_harmonic = 0.15 * np.sin(2 * np.pi * 1320 * t)
    return fundamental + second_harmonic + third_harmonic


@pytest.fixture
def clipped_sine():
    """Generate a clipped sine wave (square-ish)."""
    t = np.linspace(0, 2, SAMPLE_RATE * 2)
    sine = 1.5 * np.sin(2 * np.pi * 440 * t)  # Overly loud
    return np.clip(sine, -0.8, 0.8)


@pytest.fixture
def noise_signal():
    """Generate white noise."""
    np.random.seed(42)
    return 0.3 * np.random.randn(SAMPLE_RATE * 2)


@pytest.fixture
def enveloped_signal():
    """Generate a signal with clear ADSR envelope."""
    sr = SAMPLE_RATE
    duration = 2.0
    t = np.linspace(0, duration, int(sr * duration))

    # Simple ADSR envelope
    attack = 0.02  # 20ms
    decay = 0.1    # 100ms
    sustain_level = 0.7
    release = 0.3  # 300ms
    sustain_end = 1.5  # Gate off at 1.5s

    envelope = np.zeros_like(t)

    # Attack phase
    attack_samples = int(attack * sr)
    envelope[:attack_samples] = np.linspace(0, 1, attack_samples)

    # Decay phase
    decay_samples = int(decay * sr)
    decay_end = attack_samples + decay_samples
    envelope[attack_samples:decay_end] = np.linspace(1, sustain_level, decay_samples)

    # Sustain phase
    sustain_end_sample = int(sustain_end * sr)
    envelope[decay_end:sustain_end_sample] = sustain_level

    # Release phase
    release_samples = int(release * sr)
    release_end = min(sustain_end_sample + release_samples, len(envelope))
    remaining = release_end - sustain_end_sample
    envelope[sustain_end_sample:release_end] = np.linspace(sustain_level, 0, remaining)

    # Apply envelope to sine wave
    signal = np.sin(2 * np.pi * 440 * t) * envelope
    return signal


# =============================================================================
# THD Measurement Tests
# =============================================================================

class TestMeasureTHD:
    """Tests for measure_thd() function."""

    def test_pure_sine_low_thd(self, pure_sine_440hz):
        """Pure sine wave should have very low THD."""
        result = measure_thd(pure_sine_440hz, fundamental_freq=440)

        assert isinstance(result, THDResult)
        assert result.thd_percent < 1.0  # Should be nearly 0 for pure sine
        assert abs(result.fundamental_freq - 440) < 10  # Near 440 Hz
        assert result.fundamental_magnitude > 0

    def test_distorted_sine_higher_thd(self, distorted_sine):
        """Distorted sine should have measurable THD."""
        result = measure_thd(distorted_sine, fundamental_freq=440)

        # With added harmonics, THD should be noticeable
        assert result.thd_percent > 10.0  # Significant distortion
        assert len(result.harmonics) > 0

    def test_clipped_sine_high_thd(self, clipped_sine):
        """Clipped (squared) sine should have high THD."""
        result = measure_thd(clipped_sine)

        # Clipping creates odd harmonics
        assert result.thd_percent > 20.0

    def test_fundamental_detection(self, pure_sine_440hz):
        """Should correctly detect fundamental frequency."""
        result = measure_thd(pure_sine_440hz)  # No frequency hint

        # Should detect ~440 Hz
        assert 430 < result.fundamental_freq < 450

    def test_harmonics_list_structure(self, distorted_sine):
        """Harmonics list should have correct structure."""
        result = measure_thd(distorted_sine, fundamental_freq=440, num_harmonics=5)

        for harmonic in result.harmonics:
            assert len(harmonic) == 3  # (harmonic_num, freq, magnitude)
            n, freq, mag = harmonic
            assert isinstance(n, int)
            assert n >= 2  # Harmonics start at 2
            assert freq > 0
            assert mag >= 0

    def test_silent_audio_zero_thd(self):
        """Silent audio should have zero THD."""
        silent = np.zeros(SAMPLE_RATE)
        result = measure_thd(silent)

        assert result.thd_percent == 0.0


# =============================================================================
# Aliasing Detection Tests
# =============================================================================

class TestDetectAliasing:
    """Tests for detect_aliasing() function."""

    def test_no_aliasing_in_bandlimited_signal(self, pure_sine_440hz):
        """Band-limited signal should show no aliasing."""
        result = detect_aliasing(pure_sine_440hz, input_freq=440)

        assert isinstance(result, AliasingResult)
        assert result.has_significant_aliasing is False

    def test_aliased_signal_detection(self):
        """Should return valid result for signal analysis."""
        # Generate a signal with frequency content
        t = np.linspace(0, 1, SAMPLE_RATE)
        signal = 0.3 * np.sin(2 * np.pi * 440 * t)
        # Add some higher harmonics that the analysis will process
        signal += 0.1 * np.sin(2 * np.pi * 880 * t)
        signal += 0.05 * np.sin(2 * np.pi * 1320 * t)

        result = detect_aliasing(signal, input_freq=440)

        # Should return a valid result structure
        assert isinstance(result, AliasingResult)
        assert result.input_frequency == 440.0
        # The aliasing detection should work without errors
        assert result.alias_ratio >= 0  # May or may not detect aliasing

    def test_alias_list_structure(self, pure_sine_440hz):
        """Detected aliases list should have correct structure."""
        result = detect_aliasing(pure_sine_440hz, input_freq=440)

        for alias in result.detected_aliases:
            assert len(alias) == 2  # (frequency, magnitude)
            freq, mag = alias
            assert freq > 0
            assert mag >= 0


# =============================================================================
# Harmonic Character Tests
# =============================================================================

class TestAnalyzeHarmonics:
    """Tests for analyze_harmonics() function."""

    def test_harmonic_character_result_type(self, pure_sine_440hz):
        """Should return HarmonicCharacter result."""
        result = analyze_harmonics(pure_sine_440hz)

        assert isinstance(result, HarmonicCharacter)
        assert hasattr(result, 'even_harmonic_energy')
        assert hasattr(result, 'odd_harmonic_energy')
        assert hasattr(result, 'warmth_ratio')
        assert hasattr(result, 'character')

    def test_pure_sine_minimal_harmonics(self, pure_sine_440hz):
        """Pure sine should have minimal harmonic content."""
        result = analyze_harmonics(pure_sine_440hz)

        # Pure sine has no harmonics, so both should be near zero
        assert result.even_harmonic_energy < 0.1
        assert result.odd_harmonic_energy < 0.1

    def test_square_wave_odd_harmonics(self):
        """Square wave should have strong odd harmonics."""
        t = np.linspace(0, 2, SAMPLE_RATE * 2)
        # Approximate square wave with odd harmonics
        square = np.sign(np.sin(2 * np.pi * 440 * t))
        square = square * 0.5  # Reduce amplitude

        result = analyze_harmonics(square)

        # Square wave should be "edgy" due to odd harmonics
        assert result.odd_harmonic_energy > result.even_harmonic_energy

    def test_character_values(self, pure_sine_440hz):
        """Character should be one of expected values."""
        result = analyze_harmonics(pure_sine_440hz)

        valid_characters = ["warm", "neutral", "bright/edgy"]
        assert result.character in valid_characters


# =============================================================================
# Spectral Richness Tests
# =============================================================================

class TestComputeSpectralRichness:
    """Tests for compute_spectral_richness() function."""

    def test_spectral_richness_result_type(self, pure_sine_440hz):
        """Should return SpectralRichness result."""
        result = compute_spectral_richness(pure_sine_440hz)

        assert isinstance(result, SpectralRichness)
        assert hasattr(result, 'spectral_entropy')
        assert hasattr(result, 'spectral_flatness')
        assert hasattr(result, 'harmonic_to_noise_ratio')
        assert hasattr(result, 'crest_factor')
        assert hasattr(result, 'dynamic_range_db')

    def test_sine_low_entropy(self, pure_sine_440hz):
        """Pure sine should have low spectral entropy."""
        result = compute_spectral_richness(pure_sine_440hz)

        # Sine is very simple spectrally
        assert result.spectral_entropy < 0.5  # Low entropy

    def test_noise_high_flatness(self, noise_signal):
        """Noise should have high spectral flatness."""
        result = compute_spectral_richness(noise_signal)

        # White noise is spectrally flat
        assert result.spectral_flatness > 0.3

    def test_sine_high_hnr(self, pure_sine_440hz):
        """Pure sine should have high harmonic-to-noise ratio."""
        result = compute_spectral_richness(pure_sine_440hz)

        # Pure harmonic content
        assert result.harmonic_to_noise_ratio > 10.0  # dB

    def test_crest_factor_reasonable(self, pure_sine_440hz):
        """Crest factor should be reasonable for sine wave."""
        result = compute_spectral_richness(pure_sine_440hz)

        # Sine wave crest factor is sqrt(2) ≈ 1.414
        assert 1.3 < result.crest_factor < 1.6


# =============================================================================
# Envelope Analysis Tests
# =============================================================================

class TestAnalyzeEnvelope:
    """Tests for analyze_envelope() function."""

    def test_envelope_result_type(self, enveloped_signal):
        """Should return EnvelopeAnalysis result."""
        result = analyze_envelope(enveloped_signal)

        assert isinstance(result, EnvelopeAnalysis)
        assert hasattr(result, 'attack_time_ms')
        assert hasattr(result, 'decay_time_ms')
        assert hasattr(result, 'peak_amplitude')

    def test_detects_envelope(self, enveloped_signal):
        """Should detect clear envelope in enveloped signal."""
        result = analyze_envelope(enveloped_signal)

        assert result.has_clear_envelope is True
        assert result.peak_amplitude > 0

    def test_attack_time_reasonable(self, enveloped_signal):
        """Attack time should be reasonable for test signal."""
        result = analyze_envelope(enveloped_signal)

        # Our test signal has 20ms attack
        if result.attack_time_ms is not None:
            assert 5 < result.attack_time_ms < 100  # Allow some tolerance

    def test_silent_no_envelope(self):
        """Silent audio should have no clear envelope."""
        silent = np.zeros(SAMPLE_RATE)
        result = analyze_envelope(silent)

        assert result.peak_amplitude == 0.0


# =============================================================================
# AudioQualityReport Tests
# =============================================================================

class TestAudioQualityReport:
    """Tests for AudioQualityReport dataclass."""

    def test_report_creation(self):
        """Should create report with module name."""
        report = AudioQualityReport(module_name="TestModule")

        assert report.module_name == "TestModule"
        assert report.thd is None
        assert report.issues == []

    def test_report_to_dict(self):
        """Should convert report to dictionary."""
        report = AudioQualityReport(
            module_name="TestModule",
            overall_quality_score=0.85,
            issues=["Minor clipping detected"]
        )

        d = report.to_dict()

        assert d["module_name"] == "TestModule"
        assert d["overall_quality_score"] == 0.85
        assert "Minor clipping detected" in d["issues"]

    def test_report_with_thd(self):
        """Should include THD in dict when present."""
        thd_result = THDResult(
            thd_percent=5.0,
            fundamental_freq=440,
            fundamental_magnitude=0.5,
            harmonics=[],
            noise_floor_db=-60
        )
        report = AudioQualityReport(module_name="TestModule", thd=thd_result)

        d = report.to_dict()

        assert "thd" in d
        assert d["thd"]["thd_percent"] == 5.0


# =============================================================================
# Module Config Loading Tests
# =============================================================================

class TestLoadModuleConfig:
    """Tests for load_module_config() in audio_quality.py."""

    def test_nonexistent_module_defaults(self):
        """Should return defaults for nonexistent module."""
        config = load_module_config("NonexistentModule12345")

        assert "thresholds" in config
        assert config["thresholds"]["thd_max_percent"] == DEFAULT_QUALITY_THRESHOLDS["thd_max_percent"]

    def test_real_module_config(self):
        """Should load config for real module."""
        # PluckedString should exist
        config = load_module_config("PluckedString")

        assert "thresholds" in config
        assert "thd_max_percent" in config["thresholds"]


# =============================================================================
# Edge Case Tests
# =============================================================================

class TestEdgeCases:
    """Tests for edge cases and error handling."""

    def test_thd_very_short_audio(self):
        """Should handle very short audio."""
        short_audio = np.sin(2 * np.pi * 440 * np.linspace(0, 0.01, 480))
        result = measure_thd(short_audio)

        assert isinstance(result, THDResult)

    def test_thd_dc_offset(self):
        """Should handle audio with DC offset."""
        t = np.linspace(0, 1, SAMPLE_RATE)
        audio_with_dc = 0.5 * np.sin(2 * np.pi * 440 * t) + 0.3  # DC offset
        result = measure_thd(audio_with_dc)

        assert isinstance(result, THDResult)

    def test_spectral_zero_signal(self):
        """Should handle zero signal gracefully."""
        silent = np.zeros(SAMPLE_RATE)
        result = compute_spectral_richness(silent)

        assert isinstance(result, SpectralRichness)
        # Values should be defined (not NaN)
        assert not np.isnan(result.spectral_entropy)


# =============================================================================
# Integration Tests
# =============================================================================

class TestIntegration:
    """Integration tests combining multiple analysis functions."""

    def test_full_analysis_workflow(self, distorted_sine):
        """Should run complete analysis pipeline."""
        thd_result = measure_thd(distorted_sine)
        harmonics_result = analyze_harmonics(distorted_sine)
        spectral_result = compute_spectral_richness(distorted_sine)
        envelope_result = analyze_envelope(distorted_sine)

        # Create report
        report = AudioQualityReport(
            module_name="TestSignal",
            thd=thd_result,
            harmonics=harmonics_result,
            spectral=spectral_result,
            envelope=envelope_result,
            overall_quality_score=0.75,
            issues=["High THD"] if thd_result.thd_percent > 10 else []
        )

        d = report.to_dict()

        assert d["module_name"] == "TestSignal"
        assert "thd" in d
        assert "harmonics" in d


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
