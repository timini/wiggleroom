#!/usr/bin/env python3
"""
VCV Rack Library Manifest Validator

Validates plugin.json against VCV Library submission requirements.
Catches common issues that cause rejection:
- Invalid tags
- Trademark/brand names in module names or descriptions
- Modules in manifest missing from code
- Modules in code missing from manifest
- Invalid slug format
- Missing required fields

Based on analysis of VCV Library submission issues:
https://github.com/orgs/VCVRack/projects/1
"""

import json
import re
import sys
from pathlib import Path
from typing import List, Dict, Tuple

# Official VCV Rack valid tags (case-insensitive)
# Source: https://vcvrack.com/manual/Manifest
VALID_TAGS = {
    "arpeggiator",
    "attenuator",
    "blank",
    "chorus",
    "clock generator",
    "clock modulator",
    "compressor",
    "controller",
    "delay",
    "digital",
    "distortion",
    "drum",
    "dual",
    "dynamics",
    "effect",
    "envelope follower",
    "envelope generator",
    "equalizer",
    "expander",
    "external",
    "filter",
    "flanger",
    "function generator",
    "granular",
    "hardware clone",
    "limiter",
    "logic",
    "low-frequency oscillator",
    "low-pass gate",
    "midi",
    "mixer",
    "multiple",
    "noise",
    "oscillator",
    "panning",
    "phaser",
    "physical modeling",
    "polyphonic",
    "quad",
    "quantizer",
    "random",
    "recording",
    "reverb",
    "ring modulator",
    "sample and hold",
    "sampler",
    "sequencer",
    "slew limiter",
    "speech",
    "switch",
    "synth voice",
    "tuner",
    "utility",
    "visual",
    "vocoder",
    "voltage-controlled amplifier",
    "waveshaper",
}

# Common invalid tag variations that people use
INVALID_TAG_CORRECTIONS = {
    "lfo": "Low-frequency oscillator",
    "vco": "Oscillator",
    "vca": "Voltage-controlled amplifier",
    "vcf": "Filter",
    "adsr": "Envelope generator",
    "env": "Envelope generator",
    "lowpass gate": "Low-pass gate",
    "lowpass": "Filter",
    "highpass": "Filter",
    "bandpass": "Filter",
    "instrument": "Synth voice",
    "voice": "Synth voice",
    "synth": "Synth voice",
    "percussion": "Drum",
    "clock": "Clock generator",
    "seq": "Sequencer",
    "fx": "Effect",
    "mod": "Modulator",  # Not valid - use specific type
}

# Trademark names to avoid (case-insensitive patterns)
# VCV Ethics Guidelines prohibit using brand/product names without permission
TRADEMARK_PATTERNS = [
    r"\bmoog\b",
    r"\bsolina\b",
    r"\broland\b",
    r"\bkorg\b",
    r"\byamaha\b",
    r"\barp\b",  # ARP synthesizers
    r"\boberheim\b",
    r"\bprophet\b",
    r"\bjuno\b",
    r"\bjupiter\b",
    r"\bminimoog\b",
    r"\btb-?303\b",
    r"\btr-?808\b",
    r"\btr-?909\b",
    r"\bsh-?101\b",
    r"\bms-?20\b",
    r"\bdx7\b",
    r"\bnord\b",
    r"\baccess\b",  # Access Virus
    r"\bvirus\b",
    r"\bwaldorf\b",
    r"\bclavia\b",
    r"\bbuchla\b",
    r"\bserge\b",
    r"\bemu\b",
    r"\bensoniq\b",
    r"\bkurzweil\b",
    r"\bfairlight\b",
    r"\bppg\b",
    r"\bsequential\b",
    r"\bdave smith\b",
]

# Compile patterns for efficiency
TRADEMARK_REGEXES = [re.compile(p, re.IGNORECASE) for p in TRADEMARK_PATTERNS]


class ValidationError:
    def __init__(self, severity: str, message: str, module: str = None, field: str = None):
        self.severity = severity  # "error" or "warning"
        self.message = message
        self.module = module
        self.field = field

    def __str__(self):
        loc = ""
        if self.module:
            loc = f"[{self.module}] "
        if self.field:
            loc += f"({self.field}) "
        return f"{self.severity.upper()}: {loc}{self.message}"


def validate_tags(tags: List[str], module_slug: str) -> List[ValidationError]:
    """Validate module tags against official VCV Rack tag list."""
    errors = []

    for tag in tags:
        tag_lower = tag.lower().strip()

        if tag_lower not in VALID_TAGS:
            # Check if it's a known invalid variation
            if tag_lower in INVALID_TAG_CORRECTIONS:
                suggestion = INVALID_TAG_CORRECTIONS[tag_lower]
                errors.append(ValidationError(
                    "error",
                    f"Invalid tag '{tag}'. Did you mean '{suggestion}'?",
                    module_slug,
                    "tags"
                ))
            else:
                # Find closest match
                closest = find_closest_tag(tag_lower)
                if closest:
                    errors.append(ValidationError(
                        "error",
                        f"Invalid tag '{tag}'. Did you mean '{closest}'?",
                        module_slug,
                        "tags"
                    ))
                else:
                    errors.append(ValidationError(
                        "error",
                        f"Invalid tag '{tag}'. See https://vcvrack.com/manual/Manifest for valid tags.",
                        module_slug,
                        "tags"
                    ))

    return errors


def find_closest_tag(invalid_tag: str) -> str:
    """Find the closest matching valid tag using simple substring matching."""
    invalid_lower = invalid_tag.lower()

    # First try exact substring match
    for valid in VALID_TAGS:
        if invalid_lower in valid or valid in invalid_lower:
            return valid.title()

    # Try word-level matching
    invalid_words = set(invalid_lower.split())
    best_match = None
    best_score = 0

    for valid in VALID_TAGS:
        valid_words = set(valid.split())
        overlap = len(invalid_words & valid_words)
        if overlap > best_score:
            best_score = overlap
            best_match = valid

    if best_match and best_score > 0:
        return best_match.title()

    return None


def check_trademark(text: str, module_slug: str, field: str) -> List[ValidationError]:
    """Check text for trademark/brand names."""
    errors = []

    for regex in TRADEMARK_REGEXES:
        match = regex.search(text)
        if match:
            # Trademark issues are warnings - VCV may still accept if used generically
            # (e.g., "808-style" kicks are commonly accepted)
            errors.append(ValidationError(
                "warning",
                f"Possible trademark name '{match.group()}' found. "
                f"VCV Ethics Guidelines prohibit using brand names without permission. "
                f"Consider using generic terms.",
                module_slug,
                field
            ))

    return errors


def validate_slug(slug: str, context: str = "plugin") -> List[ValidationError]:
    """Validate slug format (letters, numbers, -, _ only)."""
    errors = []

    if not slug:
        errors.append(ValidationError("error", f"{context} slug is empty"))
        return errors

    if not re.match(r'^[a-zA-Z0-9_-]+$', slug):
        errors.append(ValidationError(
            "error",
            f"{context} slug '{slug}' contains invalid characters. "
            f"Only letters, numbers, '-', and '_' are allowed."
        ))

    return errors


def find_code_modules(src_dir: Path) -> set:
    """Find all module directories in src/modules/."""
    modules = set()
    modules_dir = src_dir / "modules"

    if modules_dir.exists():
        for item in modules_dir.iterdir():
            if item.is_dir() and not item.name.startswith('.'):
                # Check if it has a CMakeLists.txt (indicates it's a real module)
                if (item / "CMakeLists.txt").exists():
                    modules.add(item.name)

    return modules


def validate_manifest(plugin_json_path: Path, src_dir: Path) -> Tuple[List[ValidationError], Dict]:
    """
    Validate plugin.json against VCV Library requirements.

    Returns:
        Tuple of (errors list, summary dict)
    """
    errors = []

    # Load plugin.json
    try:
        with open(plugin_json_path) as f:
            manifest = json.load(f)
    except json.JSONDecodeError as e:
        errors.append(ValidationError("error", f"Invalid JSON: {e}"))
        return errors, {"valid": False}
    except FileNotFoundError:
        errors.append(ValidationError("error", f"plugin.json not found at {plugin_json_path}"))
        return errors, {"valid": False}

    # Validate plugin-level fields
    plugin_slug = manifest.get("slug", "")
    errors.extend(validate_slug(plugin_slug, "Plugin"))

    # Check required plugin fields
    required_fields = ["slug", "name", "version", "license"]
    for field in required_fields:
        if field not in manifest or not manifest[field]:
            errors.append(ValidationError("error", f"Missing required plugin field: {field}"))

    # Check version format (must start with major version matching Rack)
    version = manifest.get("version", "")
    if version and not version.startswith("2."):
        errors.append(ValidationError(
            "warning",
            f"Version '{version}' should start with '2.' for VCV Rack 2.x compatibility"
        ))

    # Get modules from code
    code_modules = find_code_modules(src_dir)

    # Get modules from manifest
    manifest_modules = {}
    for module in manifest.get("modules", []):
        slug = module.get("slug", "")
        manifest_modules[slug] = module

    # Check each module in manifest
    for slug, module in manifest_modules.items():
        # Validate slug
        errors.extend(validate_slug(slug, f"Module '{slug}'"))

        # Validate tags
        tags = module.get("tags", [])
        errors.extend(validate_tags(tags, slug))

        # Check for trademark names
        name = module.get("name", "")
        description = module.get("description", "")

        errors.extend(check_trademark(name, slug, "name"))
        errors.extend(check_trademark(description, slug, "description"))

        # Check required module fields
        if not name:
            errors.append(ValidationError("error", "Missing module name", slug))
        if not tags:
            errors.append(ValidationError("warning", "No tags specified", slug))

    # Check for modules in manifest but not in code
    manifest_slugs = set(manifest_modules.keys())
    missing_from_code = manifest_slugs - code_modules
    for slug in missing_from_code:
        errors.append(ValidationError(
            "error",
            f"Module '{slug}' is in plugin.json but not found in src/modules/",
            slug
        ))

    # Check for modules in code but not in manifest
    missing_from_manifest = code_modules - manifest_slugs
    for slug in missing_from_manifest:
        errors.append(ValidationError(
            "error",
            f"Module '{slug}' exists in src/modules/ but is not in plugin.json",
            slug
        ))

    # Summary
    error_count = sum(1 for e in errors if e.severity == "error")
    warning_count = sum(1 for e in errors if e.severity == "warning")

    summary = {
        "valid": error_count == 0,
        "errors": error_count,
        "warnings": warning_count,
        "modules_in_manifest": len(manifest_modules),
        "modules_in_code": len(code_modules),
        "missing_from_code": list(missing_from_code),
        "missing_from_manifest": list(missing_from_manifest),
    }

    return errors, summary


def main():
    import argparse

    parser = argparse.ArgumentParser(description="Validate VCV Rack plugin manifest")
    parser.add_argument("--plugin-json", default="plugin.json", help="Path to plugin.json")
    parser.add_argument("--src-dir", default="src", help="Path to src directory")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    parser.add_argument("--strict", action="store_true", help="Treat warnings as errors")
    args = parser.parse_args()

    # Find project root
    plugin_json = Path(args.plugin_json)
    src_dir = Path(args.src_dir)

    # If relative paths, try to find from current dir or parent dirs
    if not plugin_json.exists():
        for parent in [Path.cwd()] + list(Path.cwd().parents)[:3]:
            candidate = parent / "plugin.json"
            if candidate.exists():
                plugin_json = candidate
                src_dir = parent / "src"
                break

    errors, summary = validate_manifest(plugin_json, src_dir)

    if args.json:
        output = {
            "summary": summary,
            "errors": [{"severity": e.severity, "message": str(e), "module": e.module, "field": e.field} for e in errors]
        }
        print(json.dumps(output, indent=2))
    else:
        print(f"Validating {plugin_json}")
        print(f"=" * 60)

        if errors:
            for error in errors:
                print(f"  {error}")
            print()

        print(f"Modules in manifest: {summary['modules_in_manifest']}")
        print(f"Modules in code: {summary['modules_in_code']}")

        if summary['missing_from_code']:
            print(f"Missing from code: {', '.join(summary['missing_from_code'])}")
        if summary['missing_from_manifest']:
            print(f"Missing from manifest: {', '.join(summary['missing_from_manifest'])}")

        print()
        if summary['valid']:
            print("PASS: No errors found")
        else:
            print(f"FAIL: {summary['errors']} error(s), {summary['warnings']} warning(s)")

    # Exit code
    if args.strict:
        sys.exit(0 if summary['errors'] == 0 and summary['warnings'] == 0 else 1)
    else:
        sys.exit(0 if summary['errors'] == 0 else 1)


if __name__ == "__main__":
    main()
