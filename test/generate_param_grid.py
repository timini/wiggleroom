#!/usr/bin/env python3
"""
Parameter Grid Analysis for VCV Rack Modules

Generates test clips for ALL permutations of parameter values (3^N for N params)
and analyzes each for quality issues. Creates detailed per-module HTML reports.

Usage:
    python generate_param_grid.py --module ChaosFlute    # Single module
    python generate_param_grid.py --all                   # All modules
    python generate_param_grid.py --module X --no-ai     # Skip AI analysis (faster)
"""

import argparse
import html
import itertools
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path
from typing import Any
import hashlib

import numpy as np

# Load .env file if present
def load_dotenv():
    """Load environment variables from .env file."""
    env_path = Path(__file__).parent.parent / ".env"
    if env_path.exists():
        with open(env_path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    key, value = line.split("=", 1)
                    os.environ.setdefault(key.strip(), value.strip())

load_dotenv()

# Optional dependencies
try:
    from scipy.io import wavfile
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False

try:
    import librosa
    HAS_LIBROSA = True
except ImportError:
    HAS_LIBROSA = False

try:
    from tqdm import tqdm
    HAS_TQDM = True
except ImportError:
    HAS_TQDM = False
    def tqdm(iterable, **kwargs):
        return iterable


# =============================================================================
# Configuration
# =============================================================================

SAMPLE_RATE = 48000
CLIP_DURATION = 2.0  # seconds per test clip
TEST_VALUES = [0.1, 0.5, 0.9]  # low, mid, high (10%, 50%, 90% of range)
VALUE_LABELS = ["low", "mid", "high"]
EXCLUDED_PARAMS = {"gate", "trigger", "volts", "velocity"}  # Control signals
OUTPUT_DIR = Path(__file__).parent / "output" / "param_grids"


# =============================================================================
# Data Classes
# =============================================================================

@dataclass
class ModuleParam:
    """Module parameter information."""
    name: str
    min_val: float
    max_val: float
    init_val: float

    def value_at_percent(self, percent: float) -> float:
        """Get parameter value at given percentage of range."""
        return self.min_val + (self.max_val - self.min_val) * percent


@dataclass
class GridPoint:
    """A single point in the parameter grid."""
    index: int
    param_values: dict[str, float]  # param_name -> actual value
    param_percents: dict[str, float]  # param_name -> percentage (0-1)
    param_labels: dict[str, str]  # param_name -> "low"/"mid"/"high"

    @property
    def hash(self) -> str:
        """Unique hash for this grid point."""
        key = "|".join(f"{k}={v:.4f}" for k, v in sorted(self.param_values.items()))
        return hashlib.md5(key.encode()).hexdigest()[:12]


@dataclass
class QualityMetrics:
    """Audio quality metrics."""
    peak_amplitude: float = 0.0
    rms_level: float = 0.0
    thd_percent: float = 0.0
    hnr_db: float = 0.0
    clipping_percent: float = 0.0
    dc_offset: float = 0.0


@dataclass
class AIAnalysis:
    """AI analysis results."""
    clap_quality_score: float = 0.0
    clap_character: list[str] = field(default_factory=list)
    gemini_summary: str = ""


@dataclass
class GridAnalysisResult:
    """Complete analysis for a single grid point."""
    grid_point: GridPoint
    wav_path: Path | None = None
    quality: QualityMetrics = field(default_factory=QualityMetrics)
    ai: AIAnalysis = field(default_factory=AIAnalysis)
    gemini_analysis: str = ""
    passed: bool = True
    issues: list[str] = field(default_factory=list)
    render_success: bool = True
    render_error: str = ""


@dataclass
class ParameterImpact:
    """Analysis of how a parameter affects quality."""
    param_name: str
    fail_rate_low: float = 0.0  # Fail rate when param is low
    fail_rate_mid: float = 0.0
    fail_rate_high: float = 0.0
    avg_thd_low: float = 0.0
    avg_thd_mid: float = 0.0
    avg_thd_high: float = 0.0
    avg_clipping_low: float = 0.0
    avg_clipping_mid: float = 0.0
    avg_clipping_high: float = 0.0


@dataclass
class GridReport:
    """Complete grid analysis report for a module."""
    module_name: str
    module_type: str
    description: str
    parameters: list[ModuleParam]
    total_points: int
    results: list[GridAnalysisResult]
    param_impacts: dict[str, ParameterImpact]
    passed_count: int = 0
    failed_count: int = 0
    error_count: int = 0
    thresholds: dict[str, float] = field(default_factory=dict)


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


def load_module_config(module_name: str) -> dict:
    """Load test configuration for a module."""
    project_root = get_project_root()
    config_path = project_root / "src" / "modules" / module_name / "test_config.json"

    if not config_path.exists():
        return {"module_type": "instrument", "description": ""}

    try:
        with open(config_path) as f:
            return json.load(f)
    except (json.JSONDecodeError, IOError):
        return {"module_type": "instrument", "description": ""}


def get_available_modules() -> list[str]:
    """Get list of available modules from faust_render."""
    exe = get_render_executable()
    if not exe.exists():
        return []

    try:
        result = subprocess.run(
            [str(exe), "--list-modules"],
            capture_output=True, text=True, timeout=10
        )
        modules = []
        for line in result.stdout.splitlines():
            line = line.strip()
            if line and not line.startswith("Available"):
                modules.append(line)
        return modules
    except Exception:
        return []


def get_module_params(module_name: str) -> list[ModuleParam]:
    """Get all parameters for a module from faust_render."""
    exe = get_render_executable()
    if not exe.exists():
        return []

    try:
        result = subprocess.run(
            [str(exe), "--module", module_name, "--list-params"],
            capture_output=True, text=True, timeout=10
        )
        params = []
        for line in result.stdout.splitlines():
            # Parse: [0] /Module/param (min=0, max=1, init=0.5)
            match = re.search(r'/[^/]+/(\w+)\s+\(min=([^,]+),\s*max=([^,]+),\s*init=([^)]+)\)', line)
            if match:
                params.append(ModuleParam(
                    name=match.group(1),
                    min_val=float(match.group(2)),
                    max_val=float(match.group(3)),
                    init_val=float(match.group(4))
                ))
        return params
    except Exception:
        return []


def get_testable_params(module_name: str, all_params: list[ModuleParam]) -> list[ModuleParam]:
    """Filter parameters to only those that should be tested in the grid."""
    config = load_module_config(module_name)

    # Get config exclusions
    config_exclude = set()
    if "parameter_sweeps" in config:
        config_exclude = set(config["parameter_sweeps"].get("exclude", []))

    # Also check for grid-specific exclusions
    if "parameter_grid" in config:
        grid_exclude = set(config["parameter_grid"].get("exclude_params", []))
        config_exclude.update(grid_exclude)

    # Combine with built-in exclusions
    all_exclude = EXCLUDED_PARAMS | config_exclude

    # Filter params
    testable = []
    for param in all_params:
        if param.name.lower() not in {e.lower() for e in all_exclude}:
            # Also skip params with no range
            if param.max_val > param.min_val:
                testable.append(param)

    return testable


# =============================================================================
# Grid Generation
# =============================================================================

def generate_grid(params: list[ModuleParam],
                  test_values: list[float] = TEST_VALUES) -> list[GridPoint]:
    """Generate all permutations of parameter values."""
    if not params:
        return []

    # Generate all combinations
    param_names = [p.name for p in params]
    value_combinations = list(itertools.product(range(len(test_values)), repeat=len(params)))

    grid_points = []
    for idx, combo in enumerate(value_combinations):
        param_values = {}
        param_percents = {}
        param_labels = {}

        for i, param in enumerate(params):
            value_idx = combo[i]
            percent = test_values[value_idx]
            actual_value = param.value_at_percent(percent)

            param_values[param.name] = actual_value
            param_percents[param.name] = percent
            param_labels[param.name] = VALUE_LABELS[value_idx]

        grid_points.append(GridPoint(
            index=idx,
            param_values=param_values,
            param_percents=param_percents,
            param_labels=param_labels
        ))

    return grid_points


# =============================================================================
# Audio Rendering
# =============================================================================

def render_grid_point(module_name: str, grid_point: GridPoint,
                      output_path: Path, module_type: str = "instrument",
                      duration: float = CLIP_DURATION) -> tuple[bool, str]:
    """Render audio for a single grid point."""
    exe = get_render_executable()
    if not exe.exists():
        return False, f"faust_render not found at {exe}"

    # Build command
    args = [
        str(exe),
        "--module", module_name,
        "--output", str(output_path),
        "--duration", str(duration),
        "--sample-rate", str(SAMPLE_RATE),
    ]

    # Add parameter values
    for param_name, value in grid_point.param_values.items():
        args.extend(["--param", f"{param_name}={value}"])

    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            return False, result.stderr
        return True, ""
    except subprocess.TimeoutExpired:
        return False, "Render timeout"
    except Exception as e:
        return False, str(e)


def render_grid(module_name: str, grid_points: list[GridPoint],
                output_dir: Path, module_type: str = "instrument",
                parallel_workers: int = 8, use_cache: bool = True,
                verbose: bool = False) -> list[tuple[GridPoint, Path, bool, str]]:
    """Render audio for all grid points in parallel."""
    output_dir.mkdir(parents=True, exist_ok=True)

    results = []
    jobs = []

    for gp in grid_points:
        wav_path = output_dir / f"perm_{gp.index:04d}.wav"

        # Check cache
        if use_cache and wav_path.exists():
            results.append((gp, wav_path, True, ""))
            continue

        jobs.append((gp, wav_path))

    if verbose and results:
        print(f"  Skipping {len(results)} cached clips")

    if not jobs:
        return results

    def render_job(job):
        gp, wav_path = job
        success, error = render_grid_point(module_name, gp, wav_path, module_type)
        return gp, wav_path, success, error

    with ThreadPoolExecutor(max_workers=parallel_workers) as executor:
        futures = [executor.submit(render_job, job) for job in jobs]

        iterator = as_completed(futures)
        if HAS_TQDM:
            iterator = tqdm(iterator, total=len(jobs), desc="Rendering",
                          leave=False, ncols=80)

        for future in iterator:
            result = future.result()
            results.append(result)

    return results


# =============================================================================
# Quality Analysis
# =============================================================================

def load_audio(path: Path) -> np.ndarray | None:
    """Load audio file as numpy array."""
    if HAS_LIBROSA:
        try:
            y, sr = librosa.load(str(path), sr=SAMPLE_RATE, mono=True)
            return y
        except Exception:
            pass

    if HAS_SCIPY:
        try:
            sr, data = wavfile.read(str(path))
            if data.dtype == np.int16:
                data = data.astype(np.float32) / 32768.0
            if data.ndim > 1:
                data = data.mean(axis=1)
            return data
        except Exception:
            pass

    return None


def analyze_quality(audio: np.ndarray) -> QualityMetrics:
    """Analyze audio quality metrics."""
    metrics = QualityMetrics()

    if len(audio) == 0:
        return metrics

    if not HAS_SCIPY:
        # Basic metrics without scipy
        metrics.peak_amplitude = float(np.max(np.abs(audio)))
        metrics.rms_level = float(np.sqrt(np.mean(audio ** 2)))
        metrics.dc_offset = float(np.mean(audio))
        clip_count = np.sum(np.abs(audio) >= 0.99)
        metrics.clipping_percent = float(100.0 * clip_count / len(audio))
        return metrics

    from scipy import signal

    # Peak and RMS
    metrics.peak_amplitude = float(np.max(np.abs(audio)))
    metrics.rms_level = float(np.sqrt(np.mean(audio ** 2)))

    # DC offset
    metrics.dc_offset = float(np.mean(audio))

    # Clipping detection (>= 0.99)
    clip_count = np.sum(np.abs(audio) >= 0.99)
    metrics.clipping_percent = float(100.0 * clip_count / len(audio))

    # THD estimation
    try:
        freqs, psd = signal.welch(audio, fs=SAMPLE_RATE, nperseg=4096)
        if np.max(psd) > 0:
            peak_idx = np.argmax(psd)
            fundamental_power = psd[peak_idx]

            harmonic_power = 0.0
            for h in range(2, 8):
                harmonic_idx = int(peak_idx * h)
                if harmonic_idx < len(psd):
                    harmonic_power += psd[harmonic_idx]

            if fundamental_power > 0:
                metrics.thd_percent = float(100.0 * np.sqrt(harmonic_power / fundamental_power))
    except Exception:
        pass

    # HNR estimation
    if metrics.rms_level > 0.01:
        try:
            corr = signal.correlate(audio[:SAMPLE_RATE], audio[:SAMPLE_RATE], mode='full')
            corr = corr[len(corr)//2:]
            peak_idx = np.argmax(corr[100:]) + 100
            if peak_idx > 0 and corr[0] > 0:
                hnr = corr[peak_idx] / (corr[0] - corr[peak_idx] + 1e-10)
                if hnr > 0:
                    metrics.hnr_db = float(10 * np.log10(hnr))
        except Exception:
            pass

    return metrics


def analyze_grid_point(grid_point: GridPoint, wav_path: Path,
                       thresholds: dict) -> GridAnalysisResult:
    """Analyze a single grid point."""
    result = GridAnalysisResult(grid_point=grid_point, wav_path=wav_path)

    audio = load_audio(wav_path)
    if audio is None:
        result.passed = False
        result.issues.append("Failed to load audio")
        return result

    result.quality = analyze_quality(audio)

    # Check thresholds
    thd_max = thresholds.get("thd_max_percent", 15.0)
    clipping_max = thresholds.get("clipping_max_percent", 1.0)
    if thresholds.get("allow_hot_signal", False):
        clipping_max = 15.0

    issues = []

    if result.quality.thd_percent > thd_max:
        issues.append(f"THD {result.quality.thd_percent:.1f}% > {thd_max}%")

    if result.quality.clipping_percent > clipping_max:
        issues.append(f"Clipping {result.quality.clipping_percent:.1f}% > {clipping_max}%")

    if result.quality.peak_amplitude < 0.05:
        issues.append("Very low output (silent)")

    if abs(result.quality.dc_offset) > 0.1:
        issues.append(f"DC offset: {result.quality.dc_offset:.3f}")

    result.issues = issues
    result.passed = len(issues) == 0

    return result


# =============================================================================
# AI Analysis
# =============================================================================

def run_ai_analysis_for_point(wav_path: Path, module_name: str, grid_point: GridPoint,
                               use_clap: bool = True, use_gemini: bool = True,
                               verbose: bool = False) -> tuple[AIAnalysis, str]:
    """Run AI analysis on a single grid point."""
    result = AIAnalysis()
    gemini_text = ""

    try:
        sys.path.insert(0, str(Path(__file__).parent))
        from ai_audio_analysis import HAS_CLAP, HAS_GEMINI, analyze_module

        do_clap = use_clap and HAS_CLAP
        do_gemini = use_gemini and HAS_GEMINI and os.environ.get("GEMINI_API_KEY")

        if not do_clap and not do_gemini:
            return result, gemini_text

        # Build context for Gemini
        param_context = "Parameter values for this test:\n"
        for name, value in grid_point.param_values.items():
            label = grid_point.param_labels.get(name, "")
            param_context += f"  - {name}: {value:.3f} ({label})\n"

        ai_result = analyze_module(
            module_name, wav_path,
            use_gemini=do_gemini,
            use_clap=do_clap,
            showcase_context=param_context,
            verbose=verbose,
            gemini_timeout=120.0
        )

        if ai_result.clap_scores:
            result.clap_quality_score = ai_result.clap_scores.quality_score
            if ai_result.clap_scores.character_scores:
                sorted_chars = sorted(
                    ai_result.clap_scores.character_scores.items(),
                    key=lambda x: x[1], reverse=True
                )
                result.clap_character = [c[0] for c in sorted_chars[:5]]

        if ai_result.gemini_analysis:
            gemini_text = ai_result.gemini_analysis

    except Exception as e:
        if verbose:
            print(f"AI analysis error: {e}")

    return result, gemini_text


# =============================================================================
# Parameter Impact Analysis
# =============================================================================

def analyze_parameter_impacts(params: list[ModuleParam],
                               results: list[GridAnalysisResult]) -> dict[str, ParameterImpact]:
    """Analyze how each parameter affects quality metrics."""
    impacts = {}

    for param in params:
        impact = ParameterImpact(param_name=param.name)

        # Group results by this parameter's value
        low_results = [r for r in results if r.grid_point.param_labels.get(param.name) == "low"]
        mid_results = [r for r in results if r.grid_point.param_labels.get(param.name) == "mid"]
        high_results = [r for r in results if r.grid_point.param_labels.get(param.name) == "high"]

        # Calculate fail rates
        if low_results:
            impact.fail_rate_low = sum(1 for r in low_results if not r.passed) / len(low_results)
            impact.avg_thd_low = np.mean([r.quality.thd_percent for r in low_results])
            impact.avg_clipping_low = np.mean([r.quality.clipping_percent for r in low_results])

        if mid_results:
            impact.fail_rate_mid = sum(1 for r in mid_results if not r.passed) / len(mid_results)
            impact.avg_thd_mid = np.mean([r.quality.thd_percent for r in mid_results])
            impact.avg_clipping_mid = np.mean([r.quality.clipping_percent for r in mid_results])

        if high_results:
            impact.fail_rate_high = sum(1 for r in high_results if not r.passed) / len(high_results)
            impact.avg_thd_high = np.mean([r.quality.thd_percent for r in high_results])
            impact.avg_clipping_high = np.mean([r.quality.clipping_percent for r in high_results])

        impacts[param.name] = impact

    return impacts


# =============================================================================
# HTML Report Generation
# =============================================================================

def generate_grid_html(report: GridReport, output_path: Path):
    """Generate HTML report for parameter grid analysis."""

    # Sort results by index
    results = sorted(report.results, key=lambda r: r.grid_point.index)

    # Build parameter headers
    param_headers = "".join(f"<th>{html.escape(p.name)}</th>" for p in report.parameters)

    # Build rows
    rows_html = ""
    for result in results:
        gp = result.grid_point

        # Parameter value cells
        param_cells = ""
        for param in report.parameters:
            value = gp.param_values.get(param.name, 0)
            label = gp.param_labels.get(param.name, "")
            param_cells += f'<td class="param-{label}">{value:.3f}<br><small>{label}</small></td>'

        # Status
        if not result.render_success:
            status_class = "error"
            status_text = "ERROR"
        elif result.passed:
            status_class = "pass"
            status_text = "PASS"
        else:
            status_class = "fail"
            status_text = "FAIL"

        # Issues
        issues_html = ""
        if result.issues:
            issues_html = "<br>".join(html.escape(i) for i in result.issues)

        # Audio player
        audio_html = ""
        if result.wav_path and result.wav_path.exists():
            # Encode as base64 for inline playback
            try:
                import base64
                with open(result.wav_path, "rb") as f:
                    audio_b64 = base64.b64encode(f.read()).decode('utf-8')
                audio_html = f'<audio controls style="width:120px;height:30px;"><source src="data:audio/wav;base64,{audio_b64}" type="audio/wav"></audio>'
            except:
                audio_html = '<span style="color:#888;">N/A</span>'

        # CLAP score
        clap_html = ""
        if result.ai.clap_quality_score > 0:
            clap_html = f"{result.ai.clap_quality_score:.0f}"

        rows_html += f"""
        <tr class="{status_class}">
            <td>{gp.index}</td>
            {param_cells}
            <td class="metric">{result.quality.thd_percent:.1f}%</td>
            <td class="metric">{result.quality.clipping_percent:.1f}%</td>
            <td class="metric">{result.quality.peak_amplitude:.3f}</td>
            <td class="metric">{result.quality.hnr_db:.1f}</td>
            <td>{clap_html}</td>
            <td>{audio_html}</td>
            <td class="status-{status_class}">{status_text}</td>
            <td class="issues">{issues_html}</td>
        </tr>
        """

    # Parameter impact analysis
    impact_html = ""
    sorted_impacts = sorted(
        report.param_impacts.values(),
        key=lambda i: max(i.fail_rate_low, i.fail_rate_mid, i.fail_rate_high),
        reverse=True
    )
    for impact in sorted_impacts:
        max_fail = max(impact.fail_rate_low, impact.fail_rate_mid, impact.fail_rate_high)
        impact_html += f"""
        <tr>
            <td>{html.escape(impact.param_name)}</td>
            <td class="{'warn' if impact.fail_rate_low > 0.2 else ''}">{impact.fail_rate_low:.0%}</td>
            <td class="{'warn' if impact.fail_rate_mid > 0.2 else ''}">{impact.fail_rate_mid:.0%}</td>
            <td class="{'warn' if impact.fail_rate_high > 0.2 else ''}">{impact.fail_rate_high:.0%}</td>
            <td>{impact.avg_thd_low:.1f}%</td>
            <td>{impact.avg_thd_mid:.1f}%</td>
            <td>{impact.avg_thd_high:.1f}%</td>
            <td>{impact.avg_clipping_low:.1f}%</td>
            <td>{impact.avg_clipping_mid:.1f}%</td>
            <td>{impact.avg_clipping_high:.1f}%</td>
        </tr>
        """

    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    html_content = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{html.escape(report.module_name)} - Parameter Grid Analysis</title>
    <style>
        * {{ box-sizing: border-box; }}
        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            margin: 0;
            padding: 20px;
            background: #1a1a2e;
            color: #eee;
        }}
        .header {{
            text-align: center;
            padding: 20px;
            border-bottom: 1px solid #333;
            margin-bottom: 20px;
        }}
        .header h1 {{ margin: 0; color: #fff; }}
        .header .stats {{ margin-top: 10px; font-size: 14px; color: #aaa; }}
        .back-link {{
            display: inline-block;
            margin-bottom: 20px;
            color: #4ecdc4;
            text-decoration: none;
        }}
        .back-link:hover {{ text-decoration: underline; }}
        .summary {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
            margin-bottom: 30px;
        }}
        .summary-card {{
            background: #16213e;
            border-radius: 8px;
            padding: 15px;
            text-align: center;
        }}
        .summary-card .value {{
            font-size: 28px;
            font-weight: bold;
        }}
        .summary-card .label {{ color: #888; font-size: 12px; }}
        .summary-card.pass .value {{ color: #28a745; }}
        .summary-card.fail .value {{ color: #dc3545; }}
        .summary-card.error .value {{ color: #ffc107; }}

        .section {{
            background: #16213e;
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 20px;
        }}
        .section h2 {{
            margin-top: 0;
            color: #fff;
            border-bottom: 1px solid #333;
            padding-bottom: 10px;
        }}

        table {{
            width: 100%;
            border-collapse: collapse;
            font-size: 13px;
        }}
        th, td {{
            padding: 8px;
            text-align: left;
            border-bottom: 1px solid #333;
        }}
        th {{
            background: #0f3460;
            color: #fff;
            position: sticky;
            top: 0;
        }}
        tr:hover {{ background: #1a3a5e; }}
        tr.pass {{ }}
        tr.fail {{ background: rgba(220, 53, 69, 0.1); }}
        tr.error {{ background: rgba(255, 193, 7, 0.1); }}

        .param-low {{ color: #4ecdc4; }}
        .param-mid {{ color: #ffc107; }}
        .param-high {{ color: #ff6b6b; }}

        .status-pass {{ color: #28a745; font-weight: bold; }}
        .status-fail {{ color: #dc3545; font-weight: bold; }}
        .status-error {{ color: #ffc107; font-weight: bold; }}

        .metric {{ text-align: right; font-family: monospace; }}
        .issues {{ font-size: 11px; color: #ff8888; max-width: 200px; }}
        .warn {{ background: rgba(255, 193, 7, 0.2); }}

        .filters {{
            margin-bottom: 15px;
        }}
        .filters button {{
            background: #0f3460;
            border: none;
            color: #ccc;
            padding: 6px 12px;
            margin-right: 8px;
            border-radius: 4px;
            cursor: pointer;
        }}
        .filters button:hover {{ background: #1a4a7e; color: #fff; }}
        .filters button.active {{ background: #4ecdc4; color: #000; }}

        audio {{
            vertical-align: middle;
        }}
    </style>
</head>
<body>
    <a href="../showcase_report.html" class="back-link">&larr; Back to Showcase Report</a>

    <div class="header">
        <h1>{html.escape(report.module_name)} - Parameter Grid Analysis</h1>
        <p class="stats">
            {report.total_points} permutations (3^{len(report.parameters)}) |
            {len(report.parameters)} parameters |
            Generated: {timestamp}
        </p>
    </div>

    <div class="summary">
        <div class="summary-card pass">
            <div class="value">{report.passed_count}</div>
            <div class="label">PASSED</div>
        </div>
        <div class="summary-card fail">
            <div class="value">{report.failed_count}</div>
            <div class="label">FAILED</div>
        </div>
        <div class="summary-card error">
            <div class="value">{report.error_count}</div>
            <div class="label">ERRORS</div>
        </div>
        <div class="summary-card">
            <div class="value">{report.passed_count * 100 // max(1, report.total_points)}%</div>
            <div class="label">PASS RATE</div>
        </div>
    </div>

    <div class="section">
        <h2>Parameter Impact Analysis</h2>
        <p style="color:#888;font-size:13px;">Shows how each parameter value (low/mid/high) affects failure rate and quality metrics.</p>
        <table>
            <thead>
                <tr>
                    <th>Parameter</th>
                    <th>Fail% Low</th>
                    <th>Fail% Mid</th>
                    <th>Fail% High</th>
                    <th>THD Low</th>
                    <th>THD Mid</th>
                    <th>THD High</th>
                    <th>Clip Low</th>
                    <th>Clip Mid</th>
                    <th>Clip High</th>
                </tr>
            </thead>
            <tbody>
                {impact_html}
            </tbody>
        </table>
    </div>

    <div class="section">
        <h2>Full Parameter Matrix</h2>
        <div class="filters">
            <button onclick="filterRows('all')" class="active">All</button>
            <button onclick="filterRows('pass')">Passed Only</button>
            <button onclick="filterRows('fail')">Failed Only</button>
        </div>
        <div style="overflow-x:auto;max-height:600px;overflow-y:auto;">
            <table id="grid-table">
                <thead>
                    <tr>
                        <th>#</th>
                        {param_headers}
                        <th>THD%</th>
                        <th>Clip%</th>
                        <th>Peak</th>
                        <th>HNR</th>
                        <th>CLAP</th>
                        <th>Audio</th>
                        <th>Status</th>
                        <th>Issues</th>
                    </tr>
                </thead>
                <tbody>
                    {rows_html}
                </tbody>
            </table>
        </div>
    </div>

    <script>
        function filterRows(filter) {{
            const rows = document.querySelectorAll('#grid-table tbody tr');
            const buttons = document.querySelectorAll('.filters button');

            buttons.forEach(b => b.classList.remove('active'));
            event.target.classList.add('active');

            rows.forEach(row => {{
                if (filter === 'all') {{
                    row.style.display = '';
                }} else if (filter === 'pass') {{
                    row.style.display = row.classList.contains('pass') ? '' : 'none';
                }} else if (filter === 'fail') {{
                    row.style.display = (row.classList.contains('fail') || row.classList.contains('error')) ? '' : 'none';
                }}
            }});
        }}
    </script>
</body>
</html>
"""

    output_path.write_text(html_content)


def save_grid_json(report: GridReport, output_path: Path):
    """Save complete grid analysis data as JSON for agent loop review."""
    from datetime import datetime

    data = {
        "module_name": report.module_name,
        "module_type": report.module_type,
        "description": report.description,
        "generated_at": datetime.now().isoformat(),
        "summary": {
            "total_points": report.total_points,
            "passed": report.passed_count,
            "failed": report.failed_count,
            "errors": report.error_count,
            "pass_rate": report.passed_count / max(1, report.total_points)
        },
        "thresholds": report.thresholds,
        "parameters": [
            {
                "name": p.name,
                "min": p.min_val,
                "max": p.max_val,
                "init": p.init_val
            }
            for p in report.parameters
        ],
        "parameter_impacts": {
            name: {
                "fail_rate_low": impact.fail_rate_low,
                "fail_rate_mid": impact.fail_rate_mid,
                "fail_rate_high": impact.fail_rate_high,
                "avg_thd_low": impact.avg_thd_low,
                "avg_thd_mid": impact.avg_thd_mid,
                "avg_thd_high": impact.avg_thd_high,
                "avg_clipping_low": impact.avg_clipping_low,
                "avg_clipping_mid": impact.avg_clipping_mid,
                "avg_clipping_high": impact.avg_clipping_high
            }
            for name, impact in report.param_impacts.items()
        },
        "results": [
            {
                "index": r.grid_point.index,
                "param_values": r.grid_point.param_values,
                "param_labels": r.grid_point.param_labels,
                "passed": r.passed,
                "issues": r.issues,
                "render_success": r.render_success,
                "render_error": r.render_error if not r.render_success else None,
                "quality": {
                    "peak_amplitude": r.quality.peak_amplitude,
                    "rms_level": r.quality.rms_level,
                    "thd_percent": r.quality.thd_percent,
                    "hnr_db": r.quality.hnr_db,
                    "clipping_percent": r.quality.clipping_percent,
                    "dc_offset": r.quality.dc_offset
                },
                "ai": {
                    "clap_quality_score": r.ai.clap_quality_score,
                    "clap_character": r.ai.clap_character
                },
                "gemini_analysis": r.gemini_analysis if r.gemini_analysis else None,
                "wav_file": r.wav_path.name if r.wav_path else None
            }
            for r in sorted(report.results, key=lambda x: x.grid_point.index)
        ],
        # Summary of failures for quick agent review
        "failure_summary": {
            "by_issue_type": _group_failures_by_issue(report.results),
            "worst_param_combos": _find_worst_combos(report.results, report.parameters)
        }
    }

    with open(output_path, 'w') as f:
        json.dump(data, f, indent=2)


def _group_failures_by_issue(results: list[GridAnalysisResult]) -> dict[str, list[int]]:
    """Group failing results by issue type."""
    issue_groups = {}
    for r in results:
        if not r.passed:
            for issue in r.issues:
                # Extract issue type (first word before number/value)
                issue_type = issue.split()[0] if issue else "unknown"
                if issue_type not in issue_groups:
                    issue_groups[issue_type] = []
                issue_groups[issue_type].append(r.grid_point.index)
    return issue_groups


def _find_worst_combos(results: list[GridAnalysisResult],
                       params: list[ModuleParam]) -> list[dict]:
    """Find parameter combinations with worst quality metrics."""
    failed = [r for r in results if not r.passed]
    # Sort by severity (clipping first, then THD)
    sorted_fails = sorted(failed,
                         key=lambda r: (r.quality.clipping_percent, r.quality.thd_percent),
                         reverse=True)

    return [
        {
            "index": r.grid_point.index,
            "param_labels": r.grid_point.param_labels,
            "thd": r.quality.thd_percent,
            "clipping": r.quality.clipping_percent,
            "issues": r.issues
        }
        for r in sorted_fails[:10]  # Top 10 worst
    ]


# =============================================================================
# Main Pipeline
# =============================================================================

def process_module(module_name: str, output_dir: Path,
                   use_ai: bool = True, use_clap: bool = True, use_gemini: bool = True,
                   parallel_workers: int = 16, ai_parallel_workers: int = 10,
                   use_cache: bool = True, verbose: bool = False) -> GridReport | None:
    """Process a single module through the full grid analysis pipeline."""

    # Load config
    config = load_module_config(module_name)
    module_type = config.get("module_type", "instrument")
    description = config.get("description", "")
    skip_audio = config.get("skip_audio_tests", False)

    if skip_audio:
        if verbose:
            print(f"  Skipping {module_name}: audio tests disabled")
        return None

    # Get testable parameters
    all_params = get_module_params(module_name)
    testable_params = get_testable_params(module_name, all_params)

    if not testable_params:
        if verbose:
            print(f"  Skipping {module_name}: no testable parameters")
        return None

    # Generate grid
    grid_points = generate_grid(testable_params)
    total_points = len(grid_points)

    if verbose:
        print(f"  {module_name}: {len(testable_params)} params, {total_points} permutations (3^{len(testable_params)})")

    if total_points > 2000:
        print(f"  ERROR: Grid too large ({total_points} clips). Max is 2000.")
        print(f"  Use test_config.json 'parameter_grid.exclude_params' to reduce parameters.")
        return None

    if total_points > 500:
        print(f"  WARNING: Large grid ({total_points} clips). This will take a while.")

    # Create module output directory
    module_dir = output_dir / module_name
    module_dir.mkdir(parents=True, exist_ok=True)

    # Render all clips
    if verbose:
        print(f"  Rendering {total_points} clips...")
    render_results = render_grid(
        module_name, grid_points, module_dir,
        module_type=module_type,
        parallel_workers=parallel_workers,
        use_cache=use_cache,
        verbose=verbose
    )

    # Get thresholds
    thresholds = config.get("quality_thresholds", {})

    # Analyze quality
    if verbose:
        print(f"  Analyzing quality...")

    analysis_results = []

    def analyze_job(render_result):
        gp, wav_path, success, error = render_result
        if not success:
            result = GridAnalysisResult(grid_point=gp)
            result.render_success = False
            result.render_error = error
            result.passed = False
            result.issues.append(f"Render failed: {error}")
            return result
        return analyze_grid_point(gp, wav_path, thresholds)

    with ThreadPoolExecutor(max_workers=parallel_workers) as executor:
        futures = [executor.submit(analyze_job, rr) for rr in render_results]

        iterator = as_completed(futures)
        if HAS_TQDM:
            iterator = tqdm(iterator, total=len(futures), desc="Analyzing",
                          leave=False, ncols=80)

        for future in iterator:
            analysis_results.append(future.result())

    # AI analysis - CLAP batch + Gemini parallel
    if use_ai and (use_clap or use_gemini):
        # Build list of valid results with wav files
        valid_indices = [i for i, r in enumerate(analysis_results)
                        if r.wav_path and r.wav_path.exists()]

        # Phase 1: CLAP batch analysis (efficient batching)
        if use_clap and valid_indices:
            if verbose:
                print(f"  Running CLAP batch analysis ({len(valid_indices)} files)...")
            try:
                from ai_audio_analysis import analyze_with_clap_batch, HAS_CLAP
                if HAS_CLAP:
                    wav_paths = [analysis_results[i].wav_path for i in valid_indices]
                    clap_results = analyze_with_clap_batch(wav_paths, batch_size=8)

                    # Store CLAP results
                    for i, clap_scores in zip(valid_indices, clap_results):
                        if clap_scores:
                            analysis_results[i].ai.clap_quality_score = clap_scores.quality_score
                            if clap_scores.character_scores:
                                sorted_chars = sorted(
                                    clap_scores.character_scores.items(),
                                    key=lambda x: x[1], reverse=True
                                )
                                analysis_results[i].ai.clap_character = [c[0] for c in sorted_chars[:5]]
            except Exception as e:
                if verbose:
                    print(f"  CLAP batch error: {e}")

        # Phase 2: Gemini parallel analysis (I/O bound)
        if use_gemini and valid_indices:
            if verbose:
                print(f"  Running Gemini analysis (parallel workers: {ai_parallel_workers})...")

            def run_gemini_task(idx):
                result = analysis_results[idx]
                try:
                    sys.path.insert(0, str(Path(__file__).parent))
                    from ai_audio_analysis import HAS_GEMINI, analyze_with_gemini

                    if not HAS_GEMINI or not os.environ.get("GEMINI_API_KEY"):
                        return idx, ""

                    # Build context for Gemini
                    param_context = "Parameter values for this test:\n"
                    for name, value in result.grid_point.param_values.items():
                        label = result.grid_point.param_labels.get(name, "")
                        param_context += f"  - {name}: {value:.3f} ({label})\n"

                    gemini_result = analyze_with_gemini(
                        result.wav_path, module_name,
                        showcase_context=param_context,
                        verbose=False,
                        timeout=120.0
                    )
                    return idx, gemini_result.get("analysis", "")
                except Exception as e:
                    return idx, ""

            with ThreadPoolExecutor(max_workers=ai_parallel_workers) as executor:
                futures = [executor.submit(run_gemini_task, i) for i in valid_indices]

                iterator = as_completed(futures)
                if HAS_TQDM:
                    iterator = tqdm(iterator, total=len(futures), desc="Gemini Analysis",
                                  leave=False, ncols=80)

                for future in iterator:
                    idx, gemini_text = future.result()
                    analysis_results[idx].gemini_analysis = gemini_text

    # Compute parameter impacts
    param_impacts = analyze_parameter_impacts(testable_params, analysis_results)

    # Count results
    passed_count = sum(1 for r in analysis_results if r.passed)
    failed_count = sum(1 for r in analysis_results if not r.passed and r.render_success)
    error_count = sum(1 for r in analysis_results if not r.render_success)

    # Build report
    report = GridReport(
        module_name=module_name,
        module_type=module_type,
        description=description,
        parameters=testable_params,
        total_points=total_points,
        results=analysis_results,
        param_impacts=param_impacts,
        passed_count=passed_count,
        failed_count=failed_count,
        error_count=error_count,
        thresholds=thresholds
    )

    # Generate HTML
    html_path = output_dir / f"{module_name}.html"
    generate_grid_html(report, html_path)

    # Save JSON data for agent loop review
    json_path = output_dir / f"{module_name}.json"
    save_grid_json(report, json_path)

    if verbose:
        print(f"  Report: {html_path}")
        print(f"  JSON:   {json_path}")
        print(f"  Results: {passed_count} pass, {failed_count} fail, {error_count} error")

    return report


def main():
    parser = argparse.ArgumentParser(description="Parameter grid analysis for VCV modules")
    parser.add_argument("--module", "-m", help="Process specific module only")
    parser.add_argument("--all", "-a", action="store_true", help="Process all modules")
    parser.add_argument("--no-ai", action="store_true", help="Skip AI analysis")
    parser.add_argument("--no-clap", action="store_true", help="Skip CLAP analysis")
    parser.add_argument("--no-gemini", action="store_true", help="Skip Gemini analysis")
    parser.add_argument("--no-cache", action="store_true", help="Don't use cached clips")
    parser.add_argument("--parallel", "-p", type=int, default=16,
                        help="Number of parallel workers for rendering/analysis (default: 16)")
    parser.add_argument("--ai-parallel", type=int, default=10,
                        help="Number of parallel workers for AI analysis (default: 10)")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    args = parser.parse_args()

    if not args.module and not args.all:
        parser.error("Must specify --module or --all")

    # Ensure output directory exists
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Get modules to process
    if args.module:
        modules = [args.module]
    else:
        modules = get_available_modules()
        if not modules:
            print("No modules found. Ensure faust_render is built.")
            sys.exit(1)

    print(f"Processing {len(modules)} module(s)...")

    reports = []
    for module_name in modules:
        print(f"\n=== {module_name} ===")
        report = process_module(
            module_name,
            OUTPUT_DIR,
            use_ai=not args.no_ai,
            use_clap=not args.no_clap,
            use_gemini=not args.no_gemini,
            parallel_workers=args.parallel,
            ai_parallel_workers=args.ai_parallel,
            use_cache=not args.no_cache,
            verbose=args.verbose
        )
        if report:
            reports.append(report)

    # Summary
    print("\n" + "="*50)
    print("SUMMARY")
    print("="*50)

    total_passed = sum(r.passed_count for r in reports)
    total_failed = sum(r.failed_count for r in reports)
    total_error = sum(r.error_count for r in reports)
    total_points = sum(r.total_points for r in reports)

    print(f"Modules processed: {len(reports)}")
    print(f"Total permutations: {total_points}")
    print(f"Passed: {total_passed} ({total_passed * 100 // max(1, total_points)}%)")
    print(f"Failed: {total_failed}")
    print(f"Errors: {total_error}")
    print(f"\nReports written to: {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
