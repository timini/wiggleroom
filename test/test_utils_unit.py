#!/usr/bin/env python3
"""
Unit tests for test/utils.py

Tests the shared utility functions used across verification scripts.
"""

import json
import tempfile
from pathlib import Path
from unittest import mock

import numpy as np
import pytest

# Import the module under test
from utils import (
    get_project_root,
    load_module_config,
    extract_audio_stats,
    db_to_linear,
    linear_to_db,
    _camel_to_snake,
    extract_module_description,
    DEFAULT_QUALITY_THRESHOLDS,
)


# =============================================================================
# Path Utility Tests
# =============================================================================

class TestGetProjectRoot:
    """Tests for get_project_root()."""

    def test_returns_path(self):
        """Should return a Path object."""
        result = get_project_root()
        assert isinstance(result, Path)

    def test_plugin_json_exists(self):
        """Project root should contain plugin.json."""
        root = get_project_root()
        assert (root / "plugin.json").exists()

    def test_test_directory_exists(self):
        """Project root should contain test directory."""
        root = get_project_root()
        assert (root / "test").exists()


# =============================================================================
# Module Config Loading Tests
# =============================================================================

class TestLoadModuleConfig:
    """Tests for load_module_config()."""

    def test_existing_module_with_config(self):
        """Should load config for module with test_config.json."""
        # Use a known module that has a config
        config = load_module_config("PluckedString")
        assert "module_type" in config
        assert "thresholds" in config
        assert "test_scenarios" in config

    def test_nonexistent_module_returns_defaults(self):
        """Should return default config for nonexistent module."""
        config = load_module_config("NonexistentModule123")
        assert config["module_type"] == "instrument"
        assert config["skip_audio_tests"] is False
        assert config["thresholds"] == DEFAULT_QUALITY_THRESHOLDS

    def test_thresholds_structure(self):
        """Thresholds should have expected keys."""
        config = load_module_config("PluckedString")
        thresholds = config["thresholds"]
        assert "thd_max_percent" in thresholds
        assert "clipping_max_percent" in thresholds
        assert "hnr_min_db" in thresholds
        assert "allow_hot_signal" in thresholds

    def test_config_with_custom_thresholds(self):
        """Should properly load custom thresholds from module config."""
        # Create a temporary config to test
        with tempfile.TemporaryDirectory() as tmpdir:
            config_data = {
                "module_type": "filter",
                "quality_thresholds": {
                    "thd_max_percent": 25.0,
                    "clipping_max_percent": 5.0,
                    "allow_hot_signal": True
                },
                "test_scenarios": [
                    {"name": "test1", "duration": 3.0}
                ]
            }

            # Mock get_project_root to point to our temp dir
            with mock.patch('utils.get_project_root') as mock_root:
                mock_root.return_value = Path(tmpdir)

                # Create module directory and config
                module_dir = Path(tmpdir) / "src" / "modules" / "TestModule"
                module_dir.mkdir(parents=True)
                config_path = module_dir / "test_config.json"
                with open(config_path, 'w') as f:
                    json.dump(config_data, f)

                config = load_module_config("TestModule")

                assert config["module_type"] == "filter"
                assert config["thresholds"]["thd_max_percent"] == 25.0
                assert config["thresholds"]["allow_hot_signal"] is True


# =============================================================================
# Audio Stats Tests
# =============================================================================

class TestExtractAudioStats:
    """Tests for extract_audio_stats()."""

    def test_silent_audio(self):
        """Should correctly analyze silent audio."""
        audio = np.zeros(1000)
        stats = extract_audio_stats(audio)

        assert stats["rms"] == 0.0
        assert stats["peak"] == 0.0
        assert stats["dc_offset"] == 0.0
        assert stats["has_nan"] is False
        assert stats["has_inf"] is False
        assert stats["clipping_ratio"] == 0.0
        assert stats["silence_ratio"] == 1.0

    def test_sine_wave(self):
        """Should correctly analyze a sine wave."""
        t = np.linspace(0, 1, 48000)
        audio = 0.5 * np.sin(2 * np.pi * 440 * t)
        stats = extract_audio_stats(audio)

        assert 0.35 < stats["rms"] < 0.36  # RMS of sine = amplitude / sqrt(2)
        assert 0.499 < stats["peak"] < 0.501
        assert abs(stats["dc_offset"]) < 0.01
        assert stats["has_nan"] is False
        assert stats["has_inf"] is False
        assert stats["clipping_ratio"] == 0.0

    def test_clipped_audio(self):
        """Should detect clipping correctly."""
        audio = np.ones(1000)  # All samples at max
        stats = extract_audio_stats(audio, clipping_threshold=0.99)

        assert stats["clipping_ratio"] == 1.0  # 100% clipped
        assert stats["peak"] == 1.0

    def test_dc_offset_detection(self):
        """Should correctly measure DC offset."""
        audio = np.ones(1000) * 0.3  # Constant DC offset
        stats = extract_audio_stats(audio)

        assert abs(stats["dc_offset"] - 0.3) < 0.001

    def test_nan_detection(self):
        """Should detect NaN values."""
        audio = np.array([0.0, 0.5, np.nan, 0.3])
        stats = extract_audio_stats(audio)

        assert stats["has_nan"] is True

    def test_inf_detection(self):
        """Should detect Inf values."""
        audio = np.array([0.0, 0.5, np.inf, 0.3])
        stats = extract_audio_stats(audio)

        assert stats["has_inf"] is True

    def test_stereo_audio(self):
        """Should handle stereo audio (mix to mono for stats)."""
        # Create stereo audio with different left/right
        left = np.ones(1000) * 0.5
        right = np.ones(1000) * 0.3
        stereo = np.vstack([left, right])

        stats = extract_audio_stats(stereo)

        # Should mix to mono: (0.5 + 0.3) / 2 = 0.4
        assert 0.39 < stats["dc_offset"] < 0.41


# =============================================================================
# dB Conversion Tests
# =============================================================================

class TestDbConversions:
    """Tests for dB to linear conversions."""

    def test_db_to_linear_0db(self):
        """0 dB should equal 1.0 linear."""
        assert abs(db_to_linear(0) - 1.0) < 0.0001

    def test_db_to_linear_minus6db(self):
        """−6 dB should approximately equal 0.5 linear."""
        result = db_to_linear(-6)
        assert 0.49 < result < 0.51

    def test_db_to_linear_plus6db(self):
        """+6 dB should approximately equal 2.0 linear."""
        result = db_to_linear(6)
        assert 1.99 < result < 2.01

    def test_linear_to_db_unity(self):
        """1.0 linear should equal 0 dB."""
        assert abs(linear_to_db(1.0) - 0.0) < 0.0001

    def test_linear_to_db_half(self):
        """0.5 linear should approximately equal −6 dB."""
        result = linear_to_db(0.5)
        assert -6.1 < result < -5.9

    def test_linear_to_db_zero(self):
        """0 linear should return very low dB (−120)."""
        result = linear_to_db(0)
        assert result == -120.0

    def test_linear_to_db_negative(self):
        """Negative values should return very low dB."""
        result = linear_to_db(-0.5)
        assert result == -120.0

    def test_roundtrip_conversion(self):
        """db_to_linear(linear_to_db(x)) should equal x."""
        for val in [0.1, 0.5, 1.0, 2.0]:
            db = linear_to_db(val)
            recovered = db_to_linear(db)
            assert abs(recovered - val) < 0.0001


# =============================================================================
# String Utility Tests
# =============================================================================

class TestCamelToSnake:
    """Tests for _camel_to_snake()."""

    def test_simple_camel_case(self):
        """Should convert simple CamelCase."""
        assert _camel_to_snake("CamelCase") == "camel_case"

    def test_multiple_words(self):
        """Should handle multiple words."""
        assert _camel_to_snake("PluckedString") == "plucked_string"

    def test_lowercase(self):
        """Should handle already lowercase."""
        assert _camel_to_snake("lowercase") == "lowercase"

    def test_abbreviations(self):
        """Should handle abbreviations."""
        assert _camel_to_snake("LadderLPF") == "ladder_lpf"

    def test_numbers(self):
        """Should handle numbers."""
        assert _camel_to_snake("ACID9Voice") == "acid9_voice"


class TestExtractModuleDescription:
    """Tests for extract_module_description()."""

    def test_simple_comment_description(self):
        """Should extract description from comments."""
        dsp_code = """// This is a test module
// With multiple lines
import("stdfaust.lib");
"""
        desc = extract_module_description(dsp_code)
        assert "This is a test module" in desc

    def test_declare_description(self):
        """Should extract declare description."""
        dsp_code = """declare description "A cool filter module";
import("stdfaust.lib");
"""
        desc = extract_module_description(dsp_code)
        assert "A cool filter module" in desc

    def test_no_description(self):
        """Should return default message when no description."""
        dsp_code = """import("stdfaust.lib");
process = + : _;
"""
        desc = extract_module_description(dsp_code)
        assert "No description" in desc


# =============================================================================
# Integration Tests
# =============================================================================

class TestIntegration:
    """Integration tests that use real project files."""

    def test_real_module_config_loading(self):
        """Should load actual module configs from project."""
        # These modules should exist
        for module in ["PluckedString", "ChaosFlute", "LadderLPF"]:
            config = load_module_config(module)
            assert "module_type" in config
            assert "thresholds" in config

    def test_project_structure_valid(self):
        """Project should have expected structure."""
        root = get_project_root()
        assert (root / "src" / "modules").exists()
        assert (root / "plugin.json").exists()
        assert (root / "CMakeLists.txt").exists()


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
