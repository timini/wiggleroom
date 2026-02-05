#!/usr/bin/env python3
"""
Shared utilities for Faust module testing.

This module consolidates common functions used across multiple test scripts
to reduce code duplication and provide a consistent interface.
"""

import json
import subprocess
from pathlib import Path
from typing import Any

import numpy as np

# Try to import optional dependencies
try:
    import librosa
    HAS_LIBROSA = True
except ImportError:
    HAS_LIBROSA = False

# Configuration
SAMPLE_RATE = 48000
DEFAULT_DURATION = 2.0

# Default quality thresholds
DEFAULT_QUALITY_THRESHOLDS = {
    "thd_max_percent": 15.0,
    "clipping_max_percent": 1.0,
    "hnr_min_db": 0.0,
    "allow_hot_signal": False
}


# =============================================================================
# Path utilities
# =============================================================================

def get_project_root() -> Path:
    """Get the project root directory."""
    return Path(__file__).parent.parent


def get_render_executable() -> Path:
    """Get path to faust_render executable."""
    project_root = get_project_root()
    exe = project_root / "build" / "test" / "faust_render"
    if not exe.exists():
        # Try without test subdirectory
        exe = project_root / "build" / "faust_render"
    return exe


# =============================================================================
# Faust render utilities
# =============================================================================

def run_faust_render(args: list[str], timeout: int = 60) -> tuple[bool, str]:
    """
    Run faust_render with given arguments.

    Args:
        args: Command line arguments for faust_render
        timeout: Timeout in seconds

    Returns:
        Tuple of (success, output_or_error)
    """
    exe = get_render_executable()
    if not exe.exists():
        return False, f"Executable not found: {exe}"

    cmd = [str(exe)] + args
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        if result.returncode != 0:
            return False, result.stderr
        return True, result.stdout
    except subprocess.TimeoutExpired:
        return False, "Timeout"
    except Exception as e:
        return False, str(e)


def get_modules() -> list[str]:
    """Get list of available Faust modules."""
    success, output = run_faust_render(["--list-modules"])
    if not success:
        return []

    modules = []
    for line in output.strip().split("\n"):
        line = line.strip()
        if line and not line.startswith("Available"):
            modules.append(line)

    return modules


def get_module_params(module_name: str) -> list[dict[str, Any]]:
    """
    Get parameter info for a module.

    Returns list of dicts with keys: index, name, path, min, max, init
    """
    success, output = run_faust_render(["--module", module_name, "--list-params"])
    if not success:
        return []

    params = []
    for line in output.strip().split("\n"):
        line = line.strip()
        if line.startswith("["):
            # Parse: [0] /path/name (min=0, max=1, init=0.5)
            try:
                bracket_end = line.index("]")
                idx = int(line[1:bracket_end])

                rest = line[bracket_end + 1:].strip()
                path_end = rest.index("(")
                path = rest[:path_end].strip()

                # Extract name from path (last component)
                name = path.split("/")[-1]

                # Parse min/max/init
                meta = rest[path_end + 1:-1]
                min_val = float(meta.split("min=")[1].split(",")[0])
                max_val = float(meta.split("max=")[1].split(",")[0])
                init_val = float(meta.split("init=")[1].split(")")[0])

                params.append({
                    "index": idx,
                    "name": name,
                    "path": path,
                    "min": min_val,
                    "max": max_val,
                    "init": init_val,
                })
            except (ValueError, IndexError):
                continue

    return params


def render_audio(
    module_name: str,
    params: dict[str, float],
    output_path: Path,
    duration: float = DEFAULT_DURATION,
    sample_rate: int = SAMPLE_RATE,
    no_auto_gate: bool = False,
) -> tuple[bool, str]:
    """
    Render audio for a module with given parameters.

    Args:
        module_name: Name of the Faust module
        params: Dict of parameter name -> value
        output_path: Path for output WAV file
        duration: Duration in seconds
        sample_rate: Sample rate in Hz
        no_auto_gate: If True, don't auto-trigger gate

    Returns:
        Tuple of (success, error_message_if_failed)
    """
    args = [
        "--module", module_name,
        "--output", str(output_path),
        "--duration", str(duration),
        "--sample-rate", str(sample_rate),
    ]

    if no_auto_gate:
        args.append("--no-auto-gate")

    for name, value in params.items():
        args.extend(["--param", f"{name}={value}"])

    return run_faust_render(args)


# =============================================================================
# Audio loading utilities
# =============================================================================

def load_audio(path: Path, sr: int = SAMPLE_RATE, mono: bool = True) -> np.ndarray | None:
    """
    Load audio file as numpy array.

    Args:
        path: Path to audio file
        sr: Target sample rate
        mono: If True, convert to mono

    Returns:
        Audio samples as numpy array, or None on failure
    """
    if HAS_LIBROSA:
        try:
            y, _ = librosa.load(str(path), sr=sr, mono=mono)
            return y
        except Exception:
            pass

    # Fallback to scipy
    try:
        from scipy.io import wavfile
        file_sr, data = wavfile.read(str(path))
        if data.dtype == np.int16:
            data = data.astype(np.float32) / 32768.0
        elif data.dtype == np.int32:
            data = data.astype(np.float32) / 2147483648.0
        if mono and data.ndim > 1:
            data = data.mean(axis=1)
        return data
    except Exception:
        return None


# =============================================================================
# Module configuration utilities
# =============================================================================

def load_module_config(module_name: str) -> dict[str, Any]:
    """
    Load test configuration for a module from its test_config.json file.

    Returns dict with keys:
        - module_type: str (instrument, filter, effect, resonator, utility)
        - skip_audio_tests: bool
        - skip_reason: str
        - description: str
        - thresholds: dict with quality thresholds
        - test_scenarios: list of test scenario dicts
    """
    project_root = get_project_root()
    config_path = project_root / "src" / "modules" / module_name / "test_config.json"

    default_config = {
        "module_type": "instrument",
        "skip_audio_tests": False,
        "skip_reason": "",
        "description": "",
        "thresholds": DEFAULT_QUALITY_THRESHOLDS.copy(),
        "test_scenarios": [{"name": "default", "duration": 2.0}],
        "skip_tests": [],
        "skip_tests_reason": "",
    }

    if not config_path.exists():
        return default_config

    try:
        with open(config_path) as f:
            data = json.load(f)

        config = default_config.copy()
        config["module_type"] = data.get("module_type", "instrument")
        config["skip_audio_tests"] = data.get("skip_audio_tests", False)
        config["skip_reason"] = data.get("skip_reason", "")
        config["description"] = data.get("description", "")

        if "quality_thresholds" in data:
            qt = data["quality_thresholds"]
            config["thresholds"] = {
                "thd_max_percent": qt.get("thd_max_percent", 15.0),
                "clipping_max_percent": qt.get("clipping_max_percent", 1.0),
                "hnr_min_db": qt.get("hnr_min_db", 0.0),
                "allow_hot_signal": qt.get("allow_hot_signal", False),
                "dc_offset_max": qt.get("dc_offset_max", 0.01),
            }

        if "test_scenarios" in data:
            config["test_scenarios"] = data["test_scenarios"]

        if "parameter_sweeps" in data:
            config["parameter_sweeps"] = data["parameter_sweeps"]

        if "skip_tests" in data:
            config["skip_tests"] = data["skip_tests"]

        if "skip_tests_reason" in data:
            config["skip_tests_reason"] = data["skip_tests_reason"]

        return config

    except (json.JSONDecodeError, IOError):
        return default_config


def get_dsp_file_for_module(module_name: str) -> Path | None:
    """Find the .dsp file for a module."""
    project_root = get_project_root()
    module_dir = project_root / "src" / "modules" / module_name

    if not module_dir.exists():
        return None

    # Try common naming conventions
    patterns = [
        module_dir / f"{module_name.lower()}.dsp",
        module_dir / f"{_camel_to_snake(module_name)}.dsp",
    ]

    for pattern in patterns:
        if pattern.exists():
            return pattern

    # Search for any .dsp file in the module directory
    dsp_files = list(module_dir.glob("*.dsp"))
    if dsp_files:
        return dsp_files[0]

    return None


def _camel_to_snake(name: str) -> str:
    """Convert CamelCase to snake_case."""
    import re
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()


def extract_module_description(dsp_code: str) -> str:
    """Extract module description from DSP code comments."""
    import re
    lines = dsp_code.split('\n')
    description_lines = []

    for line in lines[:30]:  # Check first 30 lines
        # Skip empty lines and imports
        if not line.strip() or line.strip().startswith('import'):
            continue
        # Capture comments at the start of the file
        if line.strip().startswith('//'):
            desc = line.strip().lstrip('/ ')
            if desc:
                description_lines.append(desc)
        # Also capture declare statements
        declare_match = re.search(r'declare\s+description\s+"([^"]+)"', line)
        if declare_match:
            description_lines.append(declare_match.group(1))
        # Stop at first non-comment, non-declare, non-empty line
        elif not line.strip().startswith('declare') and line.strip():
            break

    return '\n'.join(description_lines) if description_lines else "No description available"


# =============================================================================
# Audio analysis utilities
# =============================================================================

def db_to_linear(db: float) -> float:
    """Convert decibels to linear scale."""
    return 10 ** (db / 20)


def linear_to_db(linear: float) -> float:
    """Convert linear scale to decibels."""
    if linear <= 0:
        return -120.0
    return 20 * np.log10(linear)


def extract_audio_stats(audio: np.ndarray, clipping_threshold: float = 0.99) -> dict[str, Any]:
    """
    Extract statistical features from audio.

    Args:
        audio: Audio samples (mono or stereo)
        clipping_threshold: Threshold above which samples are considered clipping

    Returns:
        Dict with rms, peak, dc_offset, has_nan, has_inf, clipping_ratio, silence_ratio
    """
    if audio.ndim > 1:
        audio = audio.mean(axis=0)  # Mix to mono for stats

    return {
        "rms": float(np.sqrt(np.mean(audio ** 2))),
        "peak": float(np.max(np.abs(audio))),
        "dc_offset": float(np.mean(audio)),
        "has_nan": bool(np.any(np.isnan(audio))),
        "has_inf": bool(np.any(np.isinf(audio))),
        "clipping_ratio": float(np.mean(np.abs(audio) > clipping_threshold)),
        "silence_ratio": float(np.mean(np.abs(audio) < 0.001)),
    }


# =============================================================================
# Report generation utilities
# =============================================================================

def format_value(value: float) -> str:
    """Format a value nicely for display.

    Args:
        value: The numeric value to format

    Returns:
        Formatted string representation
    """
    if abs(value) < 0.01 and value != 0:
        return f"{value:.3f}"
    elif abs(value) < 10:
        return f"{value:.2f}"
    elif abs(value) < 1000:
        return f"{value:.1f}"
    else:
        return f"{value:.0f}"


def generate_param_bar_html(name: str, value: float, min_val: float, max_val: float) -> str:
    """Generate HTML for a parameter visualization bar with range labels and zero marker.

    Args:
        name: Parameter name
        value: Current parameter value
        min_val: Minimum allowed value
        max_val: Maximum allowed value

    Returns:
        HTML string for the parameter bar visualization
    """
    if max_val == min_val:
        pct = 50
    else:
        pct = ((value - min_val) / (max_val - min_val)) * 100
    pct = max(0, min(100, pct))

    # Calculate zero position if range spans zero
    zero_marker = ""
    if min_val < 0 < max_val:
        zero_pct = ((0 - min_val) / (max_val - min_val)) * 100
        zero_marker = f'<div class="zero-marker" style="left: {zero_pct:.1f}%"></div>'

    return f'''<div class="param-row">
        <span class="param-name">{name}</span>
        <span class="param-min">{format_value(min_val)}</span>
        <div class="param-bar-container">
            <div class="param-bar" style="width: {pct:.1f}%"></div>
            <div class="param-marker" style="left: {pct:.1f}%"></div>
            {zero_marker}
        </div>
        <span class="param-max">{format_value(max_val)}</span>
        <span class="param-value">{format_value(value)}</span>
    </div>'''


def volts_to_note_name(volts: float) -> str:
    """Convert V/Oct voltage to note name.

    Args:
        volts: Voltage in V/Oct format (0V = C4)

    Returns:
        Note name string (e.g., "C4", "F#5")
    """
    # 0V = C4 (MIDI 60)
    semitones = round(volts * 12)
    midi_note = 60 + semitones

    note_names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    octave = (midi_note // 12) - 1
    note = note_names[midi_note % 12]
    return f"{note}{octave}"
