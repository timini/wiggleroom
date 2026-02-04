#!/usr/bin/env python3
"""
Tests for VCV Rack Library manifest validation.

Run with: python3 -m pytest test/test_manifest_validation.py -v
Or: just test-validation
"""

import json
import tempfile
import pytest
from pathlib import Path
import sys

# Add scripts to path
sys.path.insert(0, str(Path(__file__).parent.parent / "scripts"))
from validate_manifest import (
    validate_tags,
    check_trademark,
    validate_slug,
    find_closest_tag,
    validate_manifest,
    VALID_TAGS,
    ValidationError,
)


class TestValidTags:
    """Test that all official VCV Rack tags are recognized."""

    def test_valid_tags_list_not_empty(self):
        """Ensure we have the valid tags list loaded."""
        assert len(VALID_TAGS) > 50, "Should have 50+ valid tags"

    def test_common_valid_tags(self):
        """Test common valid tags are accepted."""
        valid_examples = [
            "Filter",
            "Oscillator",
            "Sequencer",
            "Effect",
            "Low-frequency oscillator",
            "Low-pass gate",
            "Synth voice",
            "Physical modeling",
            "Voltage-controlled amplifier",
        ]
        for tag in valid_examples:
            errors = validate_tags([tag], "TestModule")
            assert len(errors) == 0, f"Tag '{tag}' should be valid"

    def test_tags_case_insensitive(self):
        """Tags should be validated case-insensitively."""
        errors = validate_tags(["FILTER", "filter", "Filter", "FiLtEr"], "TestModule")
        assert len(errors) == 0, "Tags should be case-insensitive"


class TestInvalidTags:
    """Test that invalid tags are caught."""

    def test_lfo_invalid(self):
        """LFO is not a valid tag - should suggest Low-frequency oscillator."""
        errors = validate_tags(["LFO"], "TestModule")
        assert len(errors) == 1
        assert "Low-frequency oscillator" in errors[0].message

    def test_vco_invalid(self):
        """VCO is not a valid tag - should suggest Oscillator."""
        errors = validate_tags(["VCO"], "TestModule")
        assert len(errors) == 1
        assert "Oscillator" in errors[0].message

    def test_lowpass_gate_typo(self):
        """'Lowpass gate' (no hyphen) should suggest 'Low-pass gate'."""
        errors = validate_tags(["Lowpass gate"], "TestModule")
        assert len(errors) == 1
        assert "Low-pass gate" in errors[0].message

    def test_instrument_invalid(self):
        """'Instrument' is not valid - should suggest 'Synth voice'."""
        errors = validate_tags(["Instrument"], "TestModule")
        assert len(errors) == 1
        assert "Synth voice" in errors[0].message

    def test_completely_invalid_tag(self):
        """Completely made-up tags should be flagged."""
        errors = validate_tags(["FooBarBaz123"], "TestModule")
        assert len(errors) == 1
        assert errors[0].severity == "error"

    def test_multiple_invalid_tags(self):
        """Multiple invalid tags should all be caught."""
        errors = validate_tags(["LFO", "VCO", "InvalidTag"], "TestModule")
        assert len(errors) == 3


class TestFindClosestTag:
    """Test the closest tag suggestion algorithm."""

    def test_finds_suggestion_for_partial_match(self):
        """Partial matches should find suggestions when possible."""
        # "osc" should find "oscillator"
        closest = find_closest_tag("osc")
        assert closest is not None
        assert "oscillator" in closest.lower()

    def test_finds_oscillator_from_osc(self):
        """'osc' should suggest 'oscillator'."""
        closest = find_closest_tag("osc")
        assert closest is not None
        assert "oscillator" in closest.lower()

    def test_no_match_for_random_string(self):
        """Random strings may not have a close match."""
        closest = find_closest_tag("xyzzy12345")
        # May or may not find a match, but shouldn't crash
        assert closest is None or isinstance(closest, str)


class TestTrademarkDetection:
    """Test trademark/brand name detection."""

    def test_moog_detected(self):
        """'Moog' should be detected as a trademark."""
        errors = check_trademark("MoogLPF", "TestModule", "name")
        assert len(errors) == 1
        assert "trademark" in errors[0].message.lower()

    def test_solina_detected(self):
        """'Solina' should be detected as a trademark."""
        errors = check_trademark("Solina Ensemble", "TestModule", "name")
        assert len(errors) == 1

    def test_roland_detected(self):
        """'Roland' should be detected as a trademark."""
        errors = check_trademark("Roland style filter", "TestModule", "description")
        assert len(errors) == 1

    def test_tr808_detected(self):
        """'TR-808' and 'TR808' should be detected."""
        errors1 = check_trademark("TR-808", "TestModule", "name")
        errors2 = check_trademark("TR808", "TestModule", "name")
        assert len(errors1) == 1
        assert len(errors2) == 1

    def test_tb303_detected(self):
        """'TB-303' and 'TB303' should be detected."""
        errors = check_trademark("TB-303 style acid", "TestModule", "description")
        assert len(errors) == 1

    def test_generic_names_ok(self):
        """Generic names should not trigger trademark warnings."""
        safe_names = [
            "Ladder Filter",
            "String Ensemble",
            "Drum Machine",
            "Acid Bass",
            "Classic Filter",
        ]
        for name in safe_names:
            errors = check_trademark(name, "TestModule", "name")
            assert len(errors) == 0, f"'{name}' should not trigger trademark warning"

    def test_trademark_is_warning_not_error(self):
        """Trademark issues should be warnings, not errors."""
        errors = check_trademark("MoogFilter", "TestModule", "name")
        assert len(errors) == 1
        assert errors[0].severity == "warning"


class TestSlugValidation:
    """Test slug format validation."""

    def test_valid_slugs(self):
        """Valid slugs should pass."""
        valid_slugs = [
            "MyModule",
            "my-module",
            "my_module",
            "Module123",
            "ACID9Voice",
            "TR808",
        ]
        for slug in valid_slugs:
            errors = validate_slug(slug, "Module")
            assert len(errors) == 0, f"Slug '{slug}' should be valid"

    def test_slug_with_spaces_invalid(self):
        """Slugs with spaces should be invalid."""
        errors = validate_slug("My Module", "Module")
        assert len(errors) == 1
        assert "invalid characters" in errors[0].message.lower()

    def test_slug_with_special_chars_invalid(self):
        """Slugs with special characters should be invalid."""
        invalid_slugs = ["Module!", "Module@Name", "Module.Name", "Module/Name"]
        for slug in invalid_slugs:
            errors = validate_slug(slug, "Module")
            assert len(errors) == 1, f"Slug '{slug}' should be invalid"

    def test_empty_slug_invalid(self):
        """Empty slugs should be invalid."""
        errors = validate_slug("", "Module")
        assert len(errors) == 1


class TestFullManifestValidation:
    """Test complete manifest validation with temp files."""

    def create_temp_manifest(self, manifest_data: dict) -> Path:
        """Create a temporary plugin.json file."""
        fd, path = tempfile.mkstemp(suffix=".json")
        with open(path, "w") as f:
            json.dump(manifest_data, f)
        return Path(path)

    def create_temp_module_dir(self, module_name: str) -> Path:
        """Create a temporary module directory structure."""
        temp_dir = Path(tempfile.mkdtemp())
        modules_dir = temp_dir / "modules" / module_name
        modules_dir.mkdir(parents=True)
        (modules_dir / "CMakeLists.txt").touch()
        return temp_dir

    def test_valid_manifest(self):
        """A properly configured manifest should pass."""
        temp_src = self.create_temp_module_dir("TestModule")
        manifest = {
            "slug": "TestPlugin",
            "name": "Test Plugin",
            "version": "2.0.0",
            "license": "GPL-3.0-or-later",
            "modules": [
                {
                    "slug": "TestModule",
                    "name": "Test Module",
                    "description": "A test module",
                    "tags": ["Filter", "Effect"],
                }
            ],
        }
        manifest_path = self.create_temp_manifest(manifest)

        errors, summary = validate_manifest(manifest_path, temp_src)

        # Clean up
        manifest_path.unlink()

        assert summary["valid"], f"Should be valid, but got errors: {errors}"
        assert summary["errors"] == 0

    def test_missing_module_in_code(self):
        """Module in manifest but not in code should be an error."""
        temp_src = Path(tempfile.mkdtemp())
        (temp_src / "modules").mkdir()

        manifest = {
            "slug": "TestPlugin",
            "name": "Test Plugin",
            "version": "2.0.0",
            "license": "GPL-3.0-or-later",
            "modules": [
                {
                    "slug": "MissingModule",
                    "name": "Missing Module",
                    "tags": ["Filter"],
                }
            ],
        }
        manifest_path = self.create_temp_manifest(manifest)

        errors, summary = validate_manifest(manifest_path, temp_src)
        manifest_path.unlink()

        assert not summary["valid"]
        assert "MissingModule" in summary["missing_from_code"]

    def test_missing_module_in_manifest(self):
        """Module in code but not in manifest should be an error."""
        temp_src = self.create_temp_module_dir("OrphanModule")

        manifest = {
            "slug": "TestPlugin",
            "name": "Test Plugin",
            "version": "2.0.0",
            "license": "GPL-3.0-or-later",
            "modules": [],
        }
        manifest_path = self.create_temp_manifest(manifest)

        errors, summary = validate_manifest(manifest_path, temp_src)
        manifest_path.unlink()

        assert not summary["valid"]
        assert "OrphanModule" in summary["missing_from_manifest"]

    def test_invalid_tag_in_manifest(self):
        """Invalid tags should cause validation failure."""
        temp_src = self.create_temp_module_dir("TestModule")

        manifest = {
            "slug": "TestPlugin",
            "name": "Test Plugin",
            "version": "2.0.0",
            "license": "GPL-3.0-or-later",
            "modules": [
                {
                    "slug": "TestModule",
                    "name": "Test Module",
                    "tags": ["LFO"],  # Invalid!
                }
            ],
        }
        manifest_path = self.create_temp_manifest(manifest)

        errors, summary = validate_manifest(manifest_path, temp_src)
        manifest_path.unlink()

        assert not summary["valid"]
        assert any("LFO" in str(e) for e in errors)

    def test_missing_required_fields(self):
        """Missing required plugin fields should be errors."""
        temp_src = Path(tempfile.mkdtemp())
        (temp_src / "modules").mkdir()

        manifest = {
            "slug": "TestPlugin",
            # Missing: name, version, license
            "modules": [],
        }
        manifest_path = self.create_temp_manifest(manifest)

        errors, summary = validate_manifest(manifest_path, temp_src)
        manifest_path.unlink()

        assert not summary["valid"]
        assert summary["errors"] >= 3  # At least name, version, license


class TestRealManifest:
    """Test against the actual WiggleRoom plugin.json."""

    @pytest.fixture
    def project_root(self):
        """Get the project root directory."""
        return Path(__file__).parent.parent

    def test_wiggleroom_manifest_valid(self, project_root):
        """The actual WiggleRoom manifest should pass validation."""
        plugin_json = project_root / "plugin.json"
        src_dir = project_root / "src"

        if not plugin_json.exists():
            pytest.skip("plugin.json not found")

        errors, summary = validate_manifest(plugin_json, src_dir)

        # Filter out warnings - only check errors
        actual_errors = [e for e in errors if e.severity == "error"]

        assert len(actual_errors) == 0, f"Manifest has errors: {actual_errors}"
        assert summary["modules_in_manifest"] == summary["modules_in_code"], \
            f"Module count mismatch: {summary}"

    def test_all_modules_have_valid_tags(self, project_root):
        """All modules in plugin.json should have valid tags."""
        plugin_json = project_root / "plugin.json"

        if not plugin_json.exists():
            pytest.skip("plugin.json not found")

        with open(plugin_json) as f:
            manifest = json.load(f)

        for module in manifest.get("modules", []):
            slug = module.get("slug", "unknown")
            tags = module.get("tags", [])

            errors = validate_tags(tags, slug)
            assert len(errors) == 0, f"Module {slug} has invalid tags: {errors}"

    def test_no_trademark_errors(self, project_root):
        """No module should have trademark issues (errors only, warnings OK)."""
        plugin_json = project_root / "plugin.json"

        if not plugin_json.exists():
            pytest.skip("plugin.json not found")

        with open(plugin_json) as f:
            manifest = json.load(f)

        for module in manifest.get("modules", []):
            slug = module.get("slug", "unknown")
            name = module.get("name", "")
            description = module.get("description", "")

            # Check both name and description
            name_errors = check_trademark(name, slug, "name")
            desc_errors = check_trademark(description, slug, "description")

            # Filter to only actual errors (not warnings)
            actual_errors = [e for e in name_errors + desc_errors if e.severity == "error"]
            assert len(actual_errors) == 0, f"Module {slug} has trademark errors"


class TestValidationErrorClass:
    """Test the ValidationError class."""

    def test_error_string_format(self):
        """ValidationError should format nicely as string."""
        error = ValidationError("error", "Test message", "TestModule", "name")
        error_str = str(error)

        assert "ERROR" in error_str
        assert "TestModule" in error_str
        assert "name" in error_str
        assert "Test message" in error_str

    def test_warning_string_format(self):
        """Warnings should show WARNING prefix."""
        error = ValidationError("warning", "Test warning")
        assert "WARNING" in str(error)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
