#!/usr/bin/env python3
"""
Unit tests for report generation infrastructure.

Tests the report generation utilities and functions to ensure they work correctly
before being used in the actual report generation pipeline.
"""

import sys
from pathlib import Path

import numpy as np
import pytest

# Add test directory to path
sys.path.insert(0, str(Path(__file__).parent))

from utils import format_value, generate_param_bar_html, volts_to_note_name


# =============================================================================
# Test format_value
# =============================================================================

class TestFormatValue:
    """Tests for format_value utility function."""

    def test_small_values(self):
        """Small values should show 3 decimal places."""
        assert format_value(0.001) == "0.001"
        assert format_value(0.005) == "0.005"
        assert format_value(-0.003) == "-0.003"

    def test_zero(self):
        """Zero should show 2 decimal places."""
        assert format_value(0) == "0.00"
        assert format_value(0.0) == "0.00"

    def test_medium_values(self):
        """Medium values (< 10) should show 2 decimal places."""
        assert format_value(5.55) == "5.55"
        assert format_value(0.5) == "0.50"
        assert format_value(9.99) == "9.99"
        assert format_value(-3.14) == "-3.14"

    def test_large_values(self):
        """Large values (10-999) should show 1 decimal place."""
        assert format_value(12.5) == "12.5"
        assert format_value(100.25) == "100.2"
        assert format_value(999.9) == "999.9"

    def test_very_large_values(self):
        """Very large values (>= 1000) should show no decimal places."""
        assert format_value(1000) == "1000"
        assert format_value(12345) == "12345"
        assert format_value(12345.67) == "12346"

    def test_negative_values(self):
        """Negative values should be handled correctly."""
        assert format_value(-0.5) == "-0.50"
        assert format_value(-50) == "-50.0"


# =============================================================================
# Test generate_param_bar_html
# =============================================================================

class TestParamBarHTML:
    """Tests for generate_param_bar_html function."""

    def test_generates_valid_html(self):
        """Should generate valid HTML with required elements."""
        html = generate_param_bar_html("cutoff", 0.5, 0.0, 1.0)
        assert 'class="param-row"' in html
        assert 'class="param-bar"' in html
        assert 'cutoff' in html

    def test_param_name_appears(self):
        """Parameter name should appear in output."""
        html = generate_param_bar_html("resonance", 0.5, 0.0, 1.0)
        assert 'resonance' in html

    def test_value_displayed(self):
        """Current value should be displayed."""
        html = generate_param_bar_html("gain", 0.75, 0.0, 1.0)
        assert '0.75' in html

    def test_bar_width_normalized(self):
        """Bar width should be normalized to percentage."""
        html = generate_param_bar_html("level", 0.5, 0.0, 1.0)
        assert 'width: 50.0%' in html

    def test_bar_width_clamped_min(self):
        """Bar width should be clamped to minimum 0%."""
        html = generate_param_bar_html("level", -0.5, 0.0, 1.0)
        assert 'width: 0.0%' in html

    def test_bar_width_clamped_max(self):
        """Bar width should be clamped to maximum 100%."""
        html = generate_param_bar_html("level", 1.5, 0.0, 1.0)
        assert 'width: 100.0%' in html

    def test_zero_marker_for_bipolar(self):
        """Zero marker should appear when range spans zero."""
        html = generate_param_bar_html("pan", 0.0, -1.0, 1.0)
        assert 'class="zero-marker"' in html

    def test_no_zero_marker_for_unipolar(self):
        """No zero marker when range doesn't span zero."""
        html = generate_param_bar_html("gain", 0.5, 0.0, 1.0)
        assert 'zero-marker' not in html

    def test_zero_marker_at_correct_position(self):
        """Zero marker should be at correct position for bipolar range."""
        html = generate_param_bar_html("pan", 0.0, -1.0, 1.0)
        # Zero is at 50% for range [-1, 1]
        assert 'left: 50.0%' in html

    def test_equal_min_max(self):
        """Should handle equal min and max gracefully."""
        html = generate_param_bar_html("fixed", 5.0, 5.0, 5.0)
        assert 'width: 50' in html  # Default to 50%

    def test_min_max_labels(self):
        """Min and max values should appear as labels."""
        html = generate_param_bar_html("freq", 1000, 20, 20000)
        assert '20' in html
        assert '20000' in html


# =============================================================================
# Test volts_to_note_name
# =============================================================================

class TestVoltsToNoteName:
    """Tests for volts_to_note_name function."""

    def test_zero_volts_is_c4(self):
        """0V should be C4 (middle C)."""
        assert volts_to_note_name(0.0) == "C4"

    def test_one_volt_is_c5(self):
        """1V should be C5."""
        assert volts_to_note_name(1.0) == "C5"

    def test_negative_one_volt_is_c3(self):
        """−1V should be C3."""
        assert volts_to_note_name(-1.0) == "C3"

    def test_semitone_above_c4(self):
        """1/12V above C4 should be C#4."""
        assert volts_to_note_name(1/12) == "C#4"

    def test_fifth_above_c4(self):
        """7/12V above C4 should be G4."""
        assert volts_to_note_name(7/12) == "G4"

    def test_two_octaves_up(self):
        """2V should be C6."""
        assert volts_to_note_name(2.0) == "C6"

    def test_two_octaves_down(self):
        """−2V should be C2."""
        assert volts_to_note_name(-2.0) == "C2"


# =============================================================================
# Test quality analysis (from generate_unified_report)
# =============================================================================

class TestQualityAnalysis:
    """Tests for audio quality analysis functions."""

    def test_clipping_detection_no_clipping(self):
        """Audio with no clipping should report 0% clipping."""
        from generate_unified_report import analyze_quality
        audio = np.array([0.5, 0.6, 0.7, 0.5, 0.4])
        metrics = analyze_quality(audio)
        assert metrics.clipping_percent == 0.0

    def test_clipping_detection_with_clipping(self):
        """Audio with clipping should report correct clipping percentage."""
        from generate_unified_report import analyze_quality
        # 2 out of 5 samples are clipping (0.99 or above)
        audio = np.array([0.5, 0.99, 1.0, 1.0, 0.5])
        metrics = analyze_quality(audio)
        # 3 samples >= 0.99 out of 5
        expected = 100.0 * 3 / 5
        assert abs(metrics.clipping_percent - expected) < 0.1

    def test_dc_offset_detection(self):
        """Should correctly detect DC offset."""
        from generate_unified_report import analyze_quality
        audio = np.ones(1000) * 0.5  # DC offset of 0.5
        metrics = analyze_quality(audio)
        assert abs(metrics.dc_offset - 0.5) < 0.01

    def test_dc_offset_zero(self):
        """Centered audio should have near-zero DC offset."""
        from generate_unified_report import analyze_quality
        audio = np.sin(2 * np.pi * 440 * np.arange(48000) / 48000)
        metrics = analyze_quality(audio)
        assert abs(metrics.dc_offset) < 0.01

    def test_silent_audio(self):
        """Silent audio should have zero peak amplitude."""
        from generate_unified_report import analyze_quality
        audio = np.zeros(1000)
        metrics = analyze_quality(audio)
        assert metrics.peak_amplitude == 0.0
        assert metrics.rms_level == 0.0

    def test_peak_amplitude(self):
        """Peak amplitude should be correctly measured."""
        from generate_unified_report import analyze_quality
        audio = np.array([0.3, -0.8, 0.5, 0.2])
        metrics = analyze_quality(audio)
        assert abs(metrics.peak_amplitude - 0.8) < 0.01

    def test_rms_level(self):
        """RMS level should be correctly calculated."""
        from generate_unified_report import analyze_quality
        # For a sine wave, RMS = peak / sqrt(2)
        t = np.arange(48000) / 48000
        audio = 0.5 * np.sin(2 * np.pi * 440 * t)
        metrics = analyze_quality(audio)
        expected_rms = 0.5 / np.sqrt(2)
        assert abs(metrics.rms_level - expected_rms) < 0.01

    def test_empty_audio(self):
        """Empty audio should return default metrics."""
        from generate_unified_report import analyze_quality
        audio = np.array([])
        metrics = analyze_quality(audio)
        assert metrics.peak_amplitude == 0.0


# =============================================================================
# Test data classes
# =============================================================================

class TestModuleReport:
    """Tests for ModuleReport data class."""

    def test_to_dict(self):
        """to_dict should convert report to dictionary."""
        from generate_unified_report import ModuleReport
        report = ModuleReport(
            module_name="TestModule",
            module_type="instrument",
            description="Test description",
            status="pass"
        )
        d = report.to_dict()
        assert d["module_name"] == "TestModule"
        assert d["module_type"] == "instrument"
        assert d["description"] == "Test description"
        assert d["status"] == "pass"

    def test_to_dict_with_issues(self):
        """to_dict should include issues list."""
        from generate_unified_report import ModuleReport
        report = ModuleReport(
            module_name="TestModule",
            module_type="instrument",
            description="Test",
            status="needs_work"
        )
        report.issues = ["Issue 1", "Issue 2"]
        d = report.to_dict()
        assert d["issues"] == ["Issue 1", "Issue 2"]


class TestQualityMetrics:
    """Tests for QualityMetrics data class."""

    def test_defaults(self):
        """Default values should be zero."""
        from generate_unified_report import QualityMetrics
        metrics = QualityMetrics()
        assert metrics.peak_amplitude == 0.0
        assert metrics.rms_level == 0.0
        assert metrics.thd_percent == 0.0
        assert metrics.hnr_db == 0.0
        assert metrics.clipping_percent == 0.0
        assert metrics.dc_offset == 0.0


# =============================================================================
# Test showcase config parsing
# =============================================================================

class TestShowcaseConfig:
    """Tests for showcase configuration handling."""

    def test_default_instrument_config(self):
        """Instrument default config should have notes."""
        from generate_unified_report import get_default_showcase_config
        config = get_default_showcase_config("instrument")
        assert config.duration == 10.0
        assert len(config.notes) > 0

    def test_default_filter_config(self):
        """Filter default config should have automations."""
        from generate_unified_report import get_default_showcase_config
        config = get_default_showcase_config("filter")
        assert config.duration == 8.0
        assert len(config.automations) > 0

    def test_default_effect_config(self):
        """Effect default config should have mix automation."""
        from generate_unified_report import get_default_showcase_config
        config = get_default_showcase_config("effect")
        assert any(a.param == "mix" for a in config.automations)

    def test_default_utility_config(self):
        """Utility default config should have short duration."""
        from generate_unified_report import get_default_showcase_config
        config = get_default_showcase_config("utility")
        assert config.duration == 5.0


# =============================================================================
# Test CLI argument parsing
# =============================================================================

class TestCLIArgs:
    """Tests for command line argument parsing."""

    def test_default_mode(self):
        """Default mode should enable CLAP, disable Gemini."""
        from generate_unified_report import parse_args
        config = parse_args([])
        assert config.skip_clap is False
        assert config.skip_gemini is True
        assert config.include_param_grid is False

    def test_fast_mode_disables_ai(self):
        """--fast should disable all AI analysis."""
        from generate_unified_report import parse_args
        config = parse_args(["--fast"])
        assert config.skip_clap is True
        assert config.skip_gemini is True

    def test_full_mode_enables_all(self):
        """--full should enable Gemini and param grid."""
        from generate_unified_report import parse_args
        config = parse_args(["--full"])
        assert config.skip_clap is False
        assert config.skip_gemini is False
        assert config.include_param_grid is True

    def test_gemini_flag(self):
        """--gemini should enable Gemini analysis."""
        from generate_unified_report import parse_args
        config = parse_args(["--gemini"])
        assert config.skip_gemini is False

    def test_no_clap_flag(self):
        """--no-clap should disable CLAP analysis."""
        from generate_unified_report import parse_args
        config = parse_args(["--no-clap"])
        assert config.skip_clap is True

    def test_verbose_flag(self):
        """--verbose should enable verbose output."""
        from generate_unified_report import parse_args
        config = parse_args(["--verbose"])
        assert config.verbose is True

    def test_parallel_workers(self):
        """--parallel should set worker count."""
        from generate_unified_report import parse_args
        config = parse_args(["--parallel", "8"])
        assert config.parallel_workers == 8


# =============================================================================
# Test HTML generation helpers
# =============================================================================

class TestHTMLGeneration:
    """Tests for HTML generation helper functions."""

    def test_status_badge_pass(self):
        """Pass status should be green."""
        from generate_unified_report import get_status_badge
        html = get_status_badge("pass")
        assert "#28a745" in html  # Green color
        assert "PASS" in html

    def test_status_badge_needs_work(self):
        """Needs work status should be yellow."""
        from generate_unified_report import get_status_badge
        html = get_status_badge("needs_work")
        assert "#ffc107" in html  # Yellow color
        assert "NEEDS WORK" in html

    def test_status_badge_skip(self):
        """Skip status should be gray."""
        from generate_unified_report import get_status_badge
        html = get_status_badge("skip")
        assert "#6c757d" in html  # Gray color
        assert "SKIP" in html

    def test_metric_badge_good_higher_better(self):
        """Good value should be green when higher is better."""
        from generate_unified_report import get_metric_badge
        html = get_metric_badge(0.8, 0.5, 0.2, "", higher_is_better=True)
        assert "#28a745" in html  # Green

    def test_metric_badge_bad_higher_better(self):
        """Bad value should be red when higher is better."""
        from generate_unified_report import get_metric_badge
        html = get_metric_badge(0.1, 0.5, 0.2, "", higher_is_better=True)
        assert "#dc3545" in html  # Red

    def test_metric_badge_good_lower_better(self):
        """Good value should be green when lower is better."""
        from generate_unified_report import get_metric_badge
        html = get_metric_badge(0.5, 1.0, 5.0, "%", higher_is_better=False)
        assert "#28a745" in html  # Green

    def test_metric_badge_bad_lower_better(self):
        """Bad value should be red when lower is better."""
        from generate_unified_report import get_metric_badge
        html = get_metric_badge(10.0, 1.0, 5.0, "%", higher_is_better=False)
        assert "#dc3545" in html  # Red


# =============================================================================
# Integration tests (require faust_render to be built)
# =============================================================================

@pytest.mark.skipif(
    not (Path(__file__).parent.parent / "build" / "test" / "faust_render").exists(),
    reason="faust_render not built"
)
class TestIntegration:
    """Integration tests requiring built faust_render."""

    def test_get_available_modules(self):
        """Should return list of modules."""
        from generate_unified_report import get_available_modules
        modules = get_available_modules()
        assert isinstance(modules, list)
        # Should have at least one module
        assert len(modules) > 0

    def test_get_module_params(self):
        """Should return parameter info for a module."""
        from generate_unified_report import get_available_modules
        from utils import get_module_params
        modules = get_available_modules()
        if modules:
            params = get_module_params(modules[0])
            assert isinstance(params, list)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
