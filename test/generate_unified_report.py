#!/usr/bin/env python3
"""
Unified Audio Report Generator for VCV Rack Modules

Generates a comprehensive report for PR reviewers to perform final audio quality checks.
Combines showcase audio, quality metrics, and optional AI analysis in a single report.

Modes:
    --fast:   Showcase + quality metrics only (no AI, fast CI mode)
    default:  Showcase + quality + CLAP analysis (standard PR review)
    --full:   Everything including param grid and Gemini analysis (deep dive)

Usage:
    python generate_unified_report.py              # All modules, default mode
    python generate_unified_report.py --fast       # Fast mode (no AI)
    python generate_unified_report.py --full       # Full mode with Gemini
    python generate_unified_report.py -m ChaosFlute  # Specific module
"""

import argparse
import base64
import html
import json
import os
import subprocess
import sys
import warnings
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path
from typing import Any

import numpy as np

# Import shared utilities
sys.path.insert(0, str(Path(__file__).parent))
from utils import (
    get_project_root, get_render_executable, load_module_config,
    get_module_params, load_audio, format_value, generate_param_bar_html,
    volts_to_note_name, SAMPLE_RATE, DEFAULT_QUALITY_THRESHOLDS
)

# Load .env file if present
def load_dotenv():
    """Load environment variables from .env file."""
    env_path = get_project_root() / ".env"
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
    from scipy import signal
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False


# Configuration
OUTPUT_DIR = Path(__file__).parent / "output"


# =============================================================================
# Data Classes
# =============================================================================

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
    detected_sounds: list[str] = field(default_factory=list)


@dataclass
class ShowcaseNote:
    """A note in the showcase sequence."""
    start: float = 0.0
    duration: float = 2.0
    volts: float = 0.0
    velocity: float = 1.0


@dataclass
class ShowcaseAutomation:
    """Parameter automation in showcase."""
    param: str = ""
    start_time: float = 0.0
    end_time: float = 10.0
    start_value: float = 0.0
    end_value: float = 1.0


@dataclass
class ShowcaseConfig:
    """Showcase configuration for a module."""
    duration: float = 10.0
    notes: list[ShowcaseNote] = field(default_factory=list)
    automations: list[ShowcaseAutomation] = field(default_factory=list)


@dataclass
class ParamPermutation:
    """A single parameter permutation render."""
    params: dict[str, float] = field(default_factory=dict)
    wav_path: Path | None = None
    spectrogram_path: Path | None = None
    quality: QualityMetrics = field(default_factory=QualityMetrics)


@dataclass
class ModuleReport:
    """Complete report for a single module."""
    module_name: str
    module_type: str
    description: str
    showcase_wav: Path | None = None
    spectrogram: Path | None = None
    automation_graph: Path | None = None
    note_score: Path | None = None
    panel_svg: Path | None = None
    showcase_config: ShowcaseConfig | None = None
    quality: QualityMetrics = field(default_factory=QualityMetrics)
    ai: AIAnalysis = field(default_factory=AIAnalysis)
    gemini_analysis: str = ""
    status: str = "pending"  # pass, needs_work, skip, error
    issues: list[str] = field(default_factory=list)
    duration: float = 0.0
    param_permutations: list[ParamPermutation] = field(default_factory=list)

    def to_dict(self) -> dict:
        result = {
            "module_name": self.module_name,
            "module_type": self.module_type,
            "description": self.description,
            "status": self.status,
            "duration": self.duration,
            "issues": self.issues,
            "quality": asdict(self.quality),
            "ai": asdict(self.ai),
            "gemini_analysis": self.gemini_analysis,
        }
        if self.showcase_wav:
            result["showcase_wav"] = str(self.showcase_wav)
        if self.spectrogram:
            result["spectrogram"] = str(self.spectrogram)
        if self.automation_graph:
            result["automation_graph"] = str(self.automation_graph)
        if self.note_score:
            result["note_score"] = str(self.note_score)
        if self.panel_svg:
            result["panel_svg"] = str(self.panel_svg)
        return result


@dataclass
class ReportConfig:
    """Configuration for report generation."""
    skip_clap: bool = False
    skip_gemini: bool = True
    include_param_grid: bool = False
    parallel_workers: int = 4
    verbose: bool = False
    output_path: Path = OUTPUT_DIR / "unified_report.html"


# =============================================================================
# Utility Functions
# =============================================================================

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


def get_panel_svg(module_name: str) -> Path | None:
    """Get path to module's SVG panel."""
    project_root = get_project_root()
    svg_path = project_root / "res" / f"{module_name}.svg"
    if svg_path.exists():
        return svg_path
    return None


def parse_showcase_config(config: dict, module_type: str) -> ShowcaseConfig:
    """Parse showcase config from module config dict, or generate default."""
    showcase = ShowcaseConfig()

    if "showcase" in config:
        sc = config["showcase"]
        showcase.duration = sc.get("duration", 10.0)

        # Parse notes
        for note_data in sc.get("notes", []):
            note = ShowcaseNote(
                start=note_data.get("start", 0.0),
                duration=note_data.get("duration", 2.0),
                volts=note_data.get("volts", 0.0),
                velocity=note_data.get("velocity", 1.0)
            )
            showcase.notes.append(note)

        # Parse automations
        for auto_data in sc.get("automations", []):
            auto = ShowcaseAutomation(
                param=auto_data.get("param", ""),
                start_time=auto_data.get("start_time", 0.0),
                end_time=auto_data.get("end_time", showcase.duration),
                start_value=auto_data.get("start_value", 0.0),
                end_value=auto_data.get("end_value", 1.0)
            )
            showcase.automations.append(auto)
    else:
        # Generate default based on module type
        showcase = get_default_showcase_config(module_type)

    return showcase


def get_default_showcase_config(module_type: str) -> ShowcaseConfig:
    """Generate default showcase config based on module type."""
    showcase = ShowcaseConfig()

    if module_type == "instrument":
        showcase.duration = 10.0
        showcase.notes = [
            ShowcaseNote(0.0, 2.0, -1.0, 1.0),   # C3
            ShowcaseNote(2.5, 2.0, 0.0, 0.9),    # C4
            ShowcaseNote(5.0, 2.0, 1.0, 0.8),    # C5
            ShowcaseNote(7.5, 2.0, 0.583, 0.85)  # G4
        ]
    elif module_type == "filter":
        showcase.duration = 8.0
        showcase.automations = [
            ShowcaseAutomation("cutoff", 0.0, 4.0, 0.1, 0.9),
            ShowcaseAutomation("cutoff", 4.0, 8.0, 0.9, 0.1),
        ]
    elif module_type == "effect":
        showcase.duration = 10.0
        showcase.automations = [
            ShowcaseAutomation("mix", 0.0, 10.0, 0.3, 1.0),
        ]
    elif module_type == "resonator":
        showcase.duration = 10.0
        showcase.automations = [
            ShowcaseAutomation("decay", 0.0, 5.0, 0.3, 0.9),
            ShowcaseAutomation("decay", 5.0, 10.0, 0.9, 0.3),
        ]
    else:
        showcase.duration = 5.0

    return showcase


def format_showcase_context(showcase: ShowcaseConfig, module_type: str) -> str:
    """Format showcase configuration as text for AI analysis."""
    lines = []
    lines.append(f"**Audio Duration:** {showcase.duration:.1f} seconds")

    if showcase.notes:
        lines.append(f"\n**Note Sequence:** ({len(showcase.notes)} notes)")
        for i, note in enumerate(showcase.notes, 1):
            note_name = volts_to_note_name(note.volts)
            lines.append(f"  {i}. {note.start:.1f}s-{note.start + note.duration:.1f}s: "
                        f"{note_name} ({note.volts:+.2f}V), velocity={note.velocity:.0%}")
    else:
        if module_type in ("filter", "effect"):
            lines.append("\n**Input:** Continuous test signal (saw wave)")
        elif module_type == "resonator":
            lines.append("\n**Input:** Repeated impulse excitation")

    if showcase.automations:
        lines.append(f"\n**Parameter Automations:** ({len(showcase.automations)} sweeps)")
        for auto in showcase.automations:
            direction = "rising" if auto.end_value > auto.start_value else "falling"
            lines.append(f"  - {auto.param}: {auto.start_value:.0%} -> {auto.end_value:.0%} "
                        f"({direction}, {auto.start_time:.1f}s-{auto.end_time:.1f}s)")

    return "\n".join(lines)


# =============================================================================
# Audio Rendering
# =============================================================================

def render_showcase(module_name: str, output_path: Path, verbose: bool = False) -> bool:
    """Render showcase audio for a module."""
    exe = get_render_executable()
    if not exe.exists():
        if verbose:
            print(f"Error: faust_render not found at {exe}")
        return False

    args = [
        str(exe),
        "--module", module_name,
        "--showcase",
        "--output", str(output_path),
        "--sample-rate", str(SAMPLE_RATE),
    ]

    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=120)
        if verbose and result.stdout:
            print(result.stdout)
        if result.returncode != 0:
            if verbose:
                print(f"Error rendering {module_name}: {result.stderr}")
            return False
        return True
    except subprocess.TimeoutExpired:
        if verbose:
            print(f"Timeout rendering {module_name}")
        return False
    except Exception as e:
        if verbose:
            print(f"Error rendering {module_name}: {e}")
        return False


# =============================================================================
# Audio Analysis
# =============================================================================

def analyze_quality(audio: np.ndarray) -> QualityMetrics:
    """Analyze audio quality metrics."""
    metrics = QualityMetrics()

    if len(audio) == 0:
        return metrics

    # Peak and RMS
    metrics.peak_amplitude = float(np.max(np.abs(audio)))
    metrics.rms_level = float(np.sqrt(np.mean(audio ** 2)))

    # DC offset
    metrics.dc_offset = float(np.mean(audio))

    # Clipping detection (>= 0.99)
    clip_count = np.sum(np.abs(audio) >= 0.99)
    metrics.clipping_percent = float(100.0 * clip_count / len(audio))

    # THD estimation (simplified - based on spectral analysis)
    if HAS_SCIPY:
        try:
            freqs, psd = signal.welch(audio, fs=SAMPLE_RATE, nperseg=4096)
            if np.max(psd) > 0:
                # Find fundamental peak
                peak_idx = np.argmax(psd)
                fundamental_power = psd[peak_idx]

                # Sum harmonic power (simplified)
                harmonic_power = 0.0
                for h in range(2, 8):
                    harmonic_idx = int(peak_idx * h)
                    if harmonic_idx < len(psd):
                        harmonic_power += psd[harmonic_idx]

                if fundamental_power > 0:
                    metrics.thd_percent = float(100.0 * np.sqrt(harmonic_power / fundamental_power))
        except Exception:
            pass

    # HNR estimation (simplified)
    if HAS_SCIPY and metrics.rms_level > 0.01:
        try:
            # Use autocorrelation-based approach
            corr = signal.correlate(audio[:SAMPLE_RATE], audio[:SAMPLE_RATE], mode='full')
            corr = corr[len(corr)//2:]
            peak_idx = np.argmax(corr[100:]) + 100  # Skip DC area
            if peak_idx > 0 and corr[0] > 0:
                hnr = corr[peak_idx] / (corr[0] - corr[peak_idx] + 1e-10)
                if hnr > 0:
                    metrics.hnr_db = float(10 * np.log10(hnr))
        except Exception:
            pass

    return metrics


# =============================================================================
# Visualization Generation
# =============================================================================

def generate_spectrogram(audio: np.ndarray, output_path: Path, title: str = "") -> bool:
    """Generate spectrogram image."""
    if not HAS_MATPLOTLIB or not HAS_SCIPY:
        return False

    try:
        fig, ax = plt.subplots(figsize=(12, 4))

        # Generate spectrogram
        f, t, Sxx = signal.spectrogram(
            audio, fs=SAMPLE_RATE, nperseg=2048, noverlap=1536
        )

        # Convert to dB scale
        Sxx_db = 10 * np.log10(Sxx + 1e-10)

        # Plot
        im = ax.pcolormesh(t, f, Sxx_db, shading='gouraud', cmap='magma', vmin=-80, vmax=0)
        ax.set_ylabel('Frequency [Hz]')
        ax.set_xlabel('Time [s]')
        ax.set_ylim([0, 8000])  # Focus on audible range
        if title:
            ax.set_title(title)

        fig.colorbar(im, ax=ax, label='dB')
        fig.tight_layout()
        fig.savefig(output_path, dpi=100, bbox_inches='tight')
        plt.close(fig)
        return True
    except Exception as e:
        warnings.warn(f"Error generating spectrogram: {e}")
        return False


def generate_automation_graph(showcase: ShowcaseConfig, output_path: Path,
                               module_params: list[dict] = None, title: str = "") -> bool:
    """Generate parameter values graph showing all parameters over time."""
    if not HAS_MATPLOTLIB:
        return False

    if not showcase.automations and not module_params:
        return False

    try:
        fig, ax = plt.subplots(figsize=(12, 3))
        ax.set_facecolor('#1a1a2e')
        fig.patch.set_facecolor('#1a1a2e')

        # Color palette for different parameters
        colors = ['#ff6b6b', '#4ecdc4', '#ffe66d', '#95e1d3', '#f38181', '#aa96da',
                  '#74b9ff', '#fd79a8', '#a29bfe', '#ffeaa7', '#dfe6e9', '#81ecec']

        # Track which params are automated
        automated_params = {auto.param for auto in showcase.automations}

        # Time axis
        t_full = np.linspace(0, showcase.duration, 500)
        param_idx = 0

        # First, plot non-automated parameters as flat lines (dimmed)
        if module_params:
            # Exclude gate/trigger/volts which are control signals
            exclude = {'gate', 'trigger', 'volts', 'velocity'}
            for param in module_params:
                param_name = param.get('name', '')
                if param_name in exclude or param_name in automated_params:
                    continue

                min_val = param.get('min', 0)
                max_val = param.get('max', 1)
                init_val = param.get('init', 0.5)

                # Normalize to 0-1 range
                if max_val > min_val:
                    norm_value = (init_val - min_val) / (max_val - min_val)
                else:
                    norm_value = 0.5

                color = colors[param_idx % len(colors)]
                ax.plot(t_full, np.full_like(t_full, norm_value), color=color,
                       linewidth=1, alpha=0.4, linestyle='--', label=f"{param_name} (fixed)")
                param_idx += 1

        # Then, plot automated parameters with their sweeps
        processed_params = set()
        for auto in showcase.automations:
            if auto.param in processed_params:
                continue
            processed_params.add(auto.param)

            color = colors[param_idx % len(colors)]

            # Build the full timeline for this parameter
            values = np.zeros_like(t_full)

            # Find automations for this param
            param_autos = [a for a in showcase.automations if a.param == auto.param]

            # Get initial value from module_params if available
            init_val = 0.5
            if module_params:
                for p in module_params:
                    if p.get('name') == auto.param:
                        min_v = p.get('min', 0)
                        max_v = p.get('max', 1)
                        if max_v > min_v:
                            init_val = (p.get('init', 0.5) - min_v) / (max_v - min_v)
                        break

            # Build value array
            for i, t in enumerate(t_full):
                value = init_val  # Start with initial
                for a in param_autos:
                    if a.start_time <= t <= a.end_time:
                        # Linear interpolation within automation range
                        progress = (t - a.start_time) / (a.end_time - a.start_time)
                        value = a.start_value + (a.end_value - a.start_value) * progress
                        break
                    elif t > a.end_time:
                        # After automation, hold final value
                        value = a.end_value
                values[i] = value

            ax.plot(t_full, values, color=color, linewidth=2, label=auto.param)
            ax.fill_between(t_full, 0, values, color=color, alpha=0.15)
            param_idx += 1

        ax.set_xlim(0, showcase.duration)
        ax.set_ylim(0, 1.05)
        ax.set_xlabel('Time [s]', color='#aaa')
        ax.set_ylabel('Value (normalized)', color='#aaa')
        ax.tick_params(colors='#888')
        ax.spines['bottom'].set_color('#333')
        ax.spines['left'].set_color('#333')
        ax.spines['top'].set_visible(False)
        ax.spines['right'].set_visible(False)
        ax.grid(True, alpha=0.2, color='#444')

        if title:
            ax.set_title(title, color='#fff', fontsize=10)

        # Legend outside plot if many params
        if param_idx > 6:
            ax.legend(loc='upper left', bbox_to_anchor=(1.01, 1), fontsize=7,
                     facecolor='#16213e', edgecolor='#333', labelcolor='#ccc')
        else:
            ax.legend(loc='upper right', fontsize=8, facecolor='#16213e',
                     edgecolor='#333', labelcolor='#ccc')

        fig.tight_layout()
        fig.savefig(output_path, dpi=100, bbox_inches='tight', facecolor='#1a1a2e')
        plt.close(fig)
        return True
    except Exception as e:
        warnings.warn(f"Error generating automation graph: {e}")
        return False


def generate_note_score(showcase: ShowcaseConfig, output_path: Path, title: str = "") -> bool:
    """Generate piano roll / note score visualization."""
    if not HAS_MATPLOTLIB:
        return False

    if not showcase.notes:
        return False

    try:
        fig, ax = plt.subplots(figsize=(12, 2.5))
        ax.set_facecolor('#1a1a2e')
        fig.patch.set_facecolor('#1a1a2e')

        # Find pitch range
        all_volts = [note.volts for note in showcase.notes]
        min_volts = min(all_volts) - 0.5
        max_volts = max(all_volts) + 0.5

        # Draw piano roll style
        for note in showcase.notes:
            # Rectangle for each note
            rect = plt.Rectangle(
                (note.start, note.volts - 0.15),
                note.duration,
                0.3,
                facecolor='#4ecdc4',
                edgecolor='#2a9d8f',
                linewidth=1.5,
                alpha=0.7 + 0.3 * note.velocity
            )
            ax.add_patch(rect)

            # Note label
            note_name = volts_to_note_name(note.volts)
            ax.text(
                note.start + note.duration / 2,
                note.volts,
                note_name,
                ha='center', va='center',
                fontsize=9, fontweight='bold',
                color='#fff'
            )

        # Draw horizontal grid lines for common notes
        for v in np.arange(np.floor(min_volts), np.ceil(max_volts) + 1, 1):
            ax.axhline(y=v, color='#333', linewidth=0.5, linestyle='--', alpha=0.5)
            note_name = volts_to_note_name(v)
            ax.text(-0.3, v, note_name, ha='right', va='center', fontsize=8, color='#888')

        ax.set_xlim(0, showcase.duration)
        ax.set_ylim(min_volts, max_volts)
        ax.set_xlabel('Time [s]', color='#aaa')
        ax.set_ylabel('Pitch (V/Oct)', color='#aaa')
        ax.tick_params(colors='#888')
        ax.spines['bottom'].set_color('#333')
        ax.spines['left'].set_color('#333')
        ax.spines['top'].set_visible(False)
        ax.spines['right'].set_visible(False)

        if title:
            ax.set_title(title, color='#fff', fontsize=10)

        fig.tight_layout()
        fig.savefig(output_path, dpi=100, bbox_inches='tight', facecolor='#1a1a2e')
        plt.close(fig)
        return True
    except Exception as e:
        warnings.warn(f"Error generating note score: {e}")
        return False


# =============================================================================
# AI Analysis
# =============================================================================

def run_ai_analysis(wav_path: Path, module_name: str, showcase_context: str,
                    verbose: bool = False, use_clap: bool = True,
                    use_gemini: bool = False) -> tuple[AIAnalysis, str]:
    """Run AI analysis on audio file.

    Args:
        use_clap: Whether to run CLAP analysis
        use_gemini: Whether to run Gemini analysis

    Returns:
        Tuple of (AIAnalysis for CLAP results, gemini_analysis string)
    """
    result = AIAnalysis()
    gemini_text = ""

    try:
        # Lazy import to avoid loading torch/transformers when not needed
        from ai_audio_analysis import HAS_CLAP, HAS_GEMINI, analyze_module

        # Check capabilities and requested options
        do_clap = use_clap and HAS_CLAP
        do_gemini = use_gemini and HAS_GEMINI and os.environ.get("GEMINI_API_KEY")

        if verbose:
            print(f"    CLAP: {'enabled' if do_clap else 'disabled'}")
            print(f"    Gemini: {'enabled' if do_gemini else 'disabled'}")

        if not do_clap and not do_gemini:
            return result, gemini_text

        # Run analysis with requested options
        if do_gemini and verbose:
            print(f"    Running Gemini analysis...")
        ai_result = analyze_module(
            module_name, wav_path,
            use_gemini=do_gemini,
            use_clap=do_clap,
            showcase_context=showcase_context,
            verbose=verbose,
            gemini_timeout=180.0  # 3 minute timeout for longer showcases
        )

        # Extract CLAP results
        if ai_result.clap_scores:
            result.clap_quality_score = ai_result.clap_scores.quality_score
            # Get top character traits
            if ai_result.clap_scores.character_scores:
                sorted_chars = sorted(
                    ai_result.clap_scores.character_scores.items(),
                    key=lambda x: x[1], reverse=True
                )
                result.clap_character = [c[0] for c in sorted_chars[:5]]
            # Get detected sounds from top positive matches
            if ai_result.clap_scores.top_positive:
                result.detected_sounds = [d[0] for d in ai_result.clap_scores.top_positive[:3]]

        # Extract Gemini results
        if ai_result.gemini_analysis:
            gemini_text = ai_result.gemini_analysis

    except ImportError:
        if verbose:
            print("    AI analysis not available (missing dependencies)")
    except Exception as e:
        if verbose:
            print(f"AI analysis error: {e}")

    return result, gemini_text


# =============================================================================
# HTML Report Generation
# =============================================================================

def get_status_badge(status: str) -> str:
    """Get HTML badge for status."""
    colors = {
        "pass": "#28a745",
        "needs_work": "#ffc107",
        "skip": "#6c757d",
        "pending": "#17a2b8",
        "error": "#dc3545",
    }
    color = colors.get(status, "#6c757d")
    label = status.upper().replace("_", " ")
    return f'<span class="status-badge" style="background:{color};">{label}</span>'


def get_metric_badge(value: float, good_threshold: float, bad_threshold: float,
                     unit: str = "", higher_is_better: bool = True) -> str:
    """Get HTML badge for a metric value."""
    if higher_is_better:
        if value >= good_threshold:
            color = "#28a745"
        elif value >= bad_threshold:
            color = "#ffc107"
        else:
            color = "#dc3545"
    else:
        if value <= good_threshold:
            color = "#28a745"
        elif value <= bad_threshold:
            color = "#ffc107"
        else:
            color = "#dc3545"

    return f'<span style="color:{color};font-weight:bold;">{value:.1f}{unit}</span>'


def encode_audio_base64(wav_path: Path) -> str | None:
    """Encode WAV file as base64 for inline audio player."""
    try:
        with open(wav_path, "rb") as f:
            data = f.read()
        return base64.b64encode(data).decode('utf-8')
    except Exception:
        return None


def encode_image_base64(img_path: Path) -> str | None:
    """Encode image as base64 for inline display."""
    try:
        with open(img_path, "rb") as f:
            data = f.read()
        return base64.b64encode(data).decode('utf-8')
    except Exception:
        return None


def generate_html_report(reports: list[ModuleReport], output_path: Path,
                         config: ReportConfig):
    """Generate consolidated HTML report."""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # Count statuses
    status_counts = {"pass": 0, "needs_work": 0, "skip": 0, "error": 0}
    for r in reports:
        status_counts[r.status] = status_counts.get(r.status, 0) + 1

    # Determine mode label
    if config.skip_clap and config.skip_gemini:
        mode_label = "Fast Mode (no AI)"
    elif not config.skip_gemini:
        mode_label = "Full Mode (CLAP + Gemini)"
    else:
        mode_label = "Standard Mode (CLAP)"

    # Build table of contents
    toc_items = []
    for report in reports:
        status_color = {"pass": "#28a745", "needs_work": "#ffc107", "skip": "#6c757d"}.get(report.status, "#6c757d")
        toc_items.append(
            f'<a href="#module-{report.module_name}" class="toc-item">'
            f'<span class="toc-status" style="background:{status_color};"></span>'
            f'{report.module_name} <span class="toc-type">({report.module_type})</span></a>'
        )
    toc_html = "\n".join(toc_items)

    # Generate CSS
    css = generate_report_css()

    html_content = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiggleRoom Audio Report - PR Review</title>
    <style>{css}</style>
</head>
<body>
    <div class="header">
        <h1>WiggleRoom Audio Report</h1>
        <p class="subtitle">PR Review</p>
        <p class="stats">
            <span class="mode-badge">{mode_label}</span>
            Generated: {timestamp}
        </p>
        <div class="status-summary">
            <span class="status-count pass">Pass: {status_counts['pass']}</span>
            <span class="status-count needs-work">Needs Work: {status_counts['needs_work']}</span>
            <span class="status-count skip">Skip: {status_counts['skip']}</span>
        </div>
    </div>
    <div class="toc">
        <h3>Modules</h3>
        <div class="toc-grid">
            {toc_html}
        </div>
    </div>
"""

    for report in reports:
        html_content += generate_module_card_html(report, config)

    html_content += """
    <script>
    function loadAudio(button) {
        const container = button.parentElement;
        const audioWrapper = container.querySelector('.audio-wrapper');
        const audio = audioWrapper.querySelector('audio');
        const src = button.dataset.src;

        button.classList.add('loading');
        button.querySelector('.status-text').textContent = 'Loading...';

        audio.oncanplaythrough = function() {
            button.classList.add('loaded');
            audioWrapper.classList.add('loaded');
            audio.play();
        };

        audio.onerror = function() {
            button.classList.remove('loading');
            button.querySelector('.status-text').textContent = 'Failed to load';
            button.style.color = '#ff6b6b';
        };

        audio.src = src;
        audio.load();
    }

    function toggleSection(id) {
        const section = document.getElementById(id);
        if (section) {
            section.classList.toggle('hidden');
        }
    }
    </script>
</body>
</html>
"""

    output_path.write_text(html_content)
    print(f"Report written to: {output_path}")


def generate_report_css() -> str:
    """Generate CSS for the report."""
    return """
        * { box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            margin: 0;
            padding: 20px;
            background: #1a1a2e;
            color: #eee;
        }
        .header {
            text-align: center;
            padding: 20px;
            border-bottom: 1px solid #333;
            margin-bottom: 30px;
        }
        .header h1 { margin: 0; color: #fff; font-size: 2em; }
        .header .subtitle { margin: 5px 0 15px 0; color: #888; font-size: 1.1em; }
        .header .stats { font-size: 14px; color: #aaa; }
        .mode-badge {
            background: #16213e;
            padding: 4px 12px;
            border-radius: 4px;
            font-size: 12px;
            margin-right: 10px;
        }
        .status-summary {
            margin-top: 15px;
            display: flex;
            justify-content: center;
            gap: 20px;
        }
        .status-count {
            padding: 6px 16px;
            border-radius: 6px;
            font-weight: bold;
        }
        .status-count.pass { background: rgba(40, 167, 69, 0.2); color: #28a745; }
        .status-count.needs-work { background: rgba(255, 193, 7, 0.2); color: #ffc107; }
        .status-count.skip { background: rgba(108, 117, 125, 0.2); color: #6c757d; }
        .status-badge {
            color: white;
            padding: 3px 10px;
            border-radius: 4px;
            font-size: 12px;
            font-weight: bold;
        }
        .module-card {
            background: #16213e;
            border-radius: 12px;
            margin-bottom: 24px;
            padding: 20px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.3);
        }
        .module-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 16px;
            border-bottom: 1px solid #333;
            padding-bottom: 12px;
        }
        .module-header h2 { margin: 0; color: #fff; }
        .module-type { color: #888; font-size: 14px; }
        .module-content {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
        }
        @media (max-width: 900px) {
            .module-content { grid-template-columns: 1fr; }
        }
        .audio-section {
            background: #0f3460;
            border-radius: 8px;
            padding: 16px;
        }
        .audio-section h3 { margin-top: 0; font-size: 14px; color: #aaa; }
        .audio-section audio { width: 100%; }
        .spectrogram { max-width: 100%; border-radius: 4px; margin-top: 10px; }
        .metrics-section {
            background: #0f3460;
            border-radius: 8px;
            padding: 16px;
        }
        .metrics-section h3 { margin-top: 0; font-size: 14px; color: #aaa; }
        .metric-row {
            display: flex;
            justify-content: space-between;
            padding: 6px 0;
            border-bottom: 1px solid #1a3a5e;
        }
        .metric-row:last-child { border-bottom: none; }
        .metric-label { color: #888; }
        .ai-section { margin-top: 16px; }
        .ai-section h4 { margin: 0 0 8px 0; font-size: 13px; color: #aaa; }
        .character-tags { display: flex; flex-wrap: wrap; gap: 4px; }
        .character-tag {
            background: #1a3a5e;
            padding: 2px 8px;
            border-radius: 4px;
            font-size: 12px;
        }
        .issues { margin-top: 10px; padding: 10px; background: #3a1c1c; border-radius: 4px; }
        .issues ul { margin: 0; padding-left: 20px; }
        .issues li { color: #ff8888; font-size: 13px; }
        .panel-section {
            display: flex;
            align-items: flex-start;
            gap: 16px;
            margin-bottom: 16px;
        }
        .panel-svg {
            width: 120px;
            min-width: 120px;
            background: #1a1a2e;
            border-radius: 8px;
            padding: 8px;
            border: 1px solid #333;
        }
        .panel-svg svg {
            width: 100%;
            height: auto;
        }
        .gemini-section {
            margin-top: 16px;
            padding: 16px;
            background: #0a1628;
            border-radius: 8px;
            border-left: 3px solid #4ecdc4;
        }
        .gemini-section h4 {
            margin: 0 0 12px 0;
            font-size: 14px;
            color: #4ecdc4;
        }
        .gemini-content {
            color: #ccc;
            font-size: 13px;
            line-height: 1.6;
            max-height: 400px;
            overflow-y: auto;
            white-space: pre-wrap;
        }
        .toggle-btn {
            background: #1a3a5e;
            border: none;
            color: #aaa;
            padding: 4px 12px;
            border-radius: 4px;
            cursor: pointer;
            font-size: 12px;
            margin-bottom: 8px;
        }
        .toggle-btn:hover { background: #2a4a6e; color: #fff; }
        .hidden { display: none; }
        .toc {
            background: #16213e;
            border-radius: 12px;
            padding: 16px 20px;
            margin-bottom: 24px;
        }
        .toc h3 {
            margin: 0 0 12px 0;
            color: #fff;
            font-size: 16px;
        }
        .toc-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
            gap: 8px;
        }
        .toc-item {
            display: flex;
            align-items: center;
            gap: 8px;
            padding: 6px 10px;
            background: #0f3460;
            border-radius: 6px;
            color: #ccc;
            text-decoration: none;
            font-size: 13px;
            transition: background 0.2s;
        }
        .toc-item:hover { background: #1a4a7e; color: #fff; }
        .toc-status {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            flex-shrink: 0;
        }
        .toc-type { color: #666; font-size: 11px; }
        .param-grid-section {
            margin-top: 16px;
            padding: 16px;
            background: #0a1628;
            border-radius: 8px;
            border-left: 3px solid #74b9ff;
        }
        .param-grid-section h4 {
            margin: 0 0 12px 0;
            font-size: 14px;
            color: #74b9ff;
        }
        .params-container { margin-top: 12px; padding: 10px; background: #151528; border-radius: 6px; }
        .param-row { display: flex; align-items: center; margin-bottom: 8px; font-size: 11px; }
        .param-row:last-child { margin-bottom: 0; }
        .param-name { width: 70px; color: #888; flex-shrink: 0; text-transform: lowercase; }
        .param-min { width: 45px; text-align: right; color: #666; font-family: 'Monaco', 'Consolas', monospace; flex-shrink: 0; font-size: 10px; }
        .param-max { width: 45px; text-align: left; color: #666; font-family: 'Monaco', 'Consolas', monospace; flex-shrink: 0; font-size: 10px; }
        .param-bar-container { flex: 1; height: 8px; background: #252540; border-radius: 4px; margin: 0 8px; position: relative; overflow: visible; }
        .param-bar { height: 100%; background: linear-gradient(90deg, #4a4a8a, #7a7aff); border-radius: 4px; }
        .param-marker { position: absolute; top: -2px; width: 4px; height: 12px; background: #fff; border-radius: 2px; transform: translateX(-50%); box-shadow: 0 0 4px rgba(255,255,255,0.5); }
        .zero-marker { position: absolute; top: -1px; width: 2px; height: 10px; background: #ff8800; border-radius: 1px; transform: translateX(-50%); opacity: 0.8; }
        .param-value { width: 55px; text-align: right; color: #aaf; font-family: 'Monaco', 'Consolas', monospace; flex-shrink: 0; font-weight: bold; padding-left: 8px; border-left: 1px solid #333; }
        .audio-container { margin-top: 12px; position: relative; }
        .audio-placeholder {
            width: 100%; height: 40px;
            background: linear-gradient(135deg, #2a2a4a, #1e1e35);
            border: 1px solid #444; border-radius: 20px;
            display: flex; align-items: center; justify-content: center;
            cursor: pointer; transition: all 0.2s;
            font-size: 12px; color: #888; gap: 8px;
        }
        .audio-placeholder:hover { background: linear-gradient(135deg, #3a3a5a, #2e2e45); color: #aaf; border-color: #666; }
        .audio-placeholder.loading { cursor: wait; color: #aaf; }
        .audio-placeholder.loaded { display: none; }
        .audio-placeholder .play-icon { font-size: 16px; }
        .audio-placeholder .spinner {
            width: 16px; height: 16px;
            border: 2px solid #444; border-top-color: #aaf;
            border-radius: 50%; animation: spin 0.8s linear infinite;
            display: none;
        }
        .audio-placeholder.loading .spinner { display: block; }
        .audio-placeholder.loading .play-icon { display: none; }
        @keyframes spin { to { transform: rotate(360deg); } }
        .audio-wrapper { display: none; }
        .audio-wrapper.loaded { display: block; }
        .audio-wrapper audio { width: 100%; border-radius: 20px; }
    """


def generate_module_card_html(report: ModuleReport, config: ReportConfig) -> str:
    """Generate HTML for a single module card."""
    # Encode audio and images as base64 for inline embedding
    audio_b64 = ""
    if report.showcase_wav and report.showcase_wav.exists():
        audio_b64 = encode_audio_base64(report.showcase_wav) or ""

    spectrogram_b64 = ""
    if report.spectrogram and report.spectrogram.exists():
        spectrogram_b64 = encode_image_base64(report.spectrogram) or ""

    automation_b64 = ""
    if report.automation_graph and report.automation_graph.exists():
        automation_b64 = encode_image_base64(report.automation_graph) or ""

    notes_b64 = ""
    if report.note_score and report.note_score.exists():
        notes_b64 = encode_image_base64(report.note_score) or ""

    # Load SVG panel content (not base64, just embed inline)
    panel_svg_content = ""
    if report.panel_svg and report.panel_svg.exists():
        try:
            panel_svg_content = report.panel_svg.read_text()
            # Clean up SVG for embedding (remove XML declaration if present)
            if panel_svg_content.startswith("<?xml"):
                panel_svg_content = panel_svg_content[panel_svg_content.index("?>") + 2:].strip()
        except Exception:
            pass

    # Quality metrics badges
    peak_badge = get_metric_badge(report.quality.peak_amplitude, 0.3, 0.1, higher_is_better=True)
    rms_badge = get_metric_badge(report.quality.rms_level, 0.1, 0.01, higher_is_better=True)
    thd_badge = get_metric_badge(report.quality.thd_percent, 15.0, 30.0, "%", higher_is_better=False)
    hnr_badge = get_metric_badge(report.quality.hnr_db, 10.0, 5.0, " dB", higher_is_better=True)
    clip_badge = get_metric_badge(report.quality.clipping_percent, 1.0, 5.0, "%", higher_is_better=False)
    dc_badge = get_metric_badge(abs(report.quality.dc_offset), 0.01, 0.1, "", higher_is_better=False)

    # AI section (CLAP)
    ai_html = ""
    if report.ai.clap_quality_score > 0 or report.ai.clap_character:
        ai_html = f"""
        <div class="ai-section">
            <h4>CLAP Analysis</h4>
            <div class="metric-row">
                <span class="metric-label">Quality Score</span>
                <span>{report.ai.clap_quality_score:.0f}/100</span>
            </div>
            <div class="character-tags">
                {"".join(f'<span class="character-tag">{html.escape(c)}</span>' for c in report.ai.clap_character[:5])}
            </div>
        </div>
        """

    # Gemini section
    gemini_html = ""
    if report.gemini_analysis:
        module_id = report.module_name.replace(" ", "_").lower()
        gemini_html = f"""
        <div class="gemini-section">
            <h4>Gemini Analysis</h4>
            <button class="toggle-btn" onclick="toggleSection('gemini-{module_id}')">
                Toggle Details
            </button>
            <div id="gemini-{module_id}" class="gemini-content">
{html.escape(report.gemini_analysis)}
            </div>
        </div>
        """

    # Panel SVG section
    panel_html = ""
    if panel_svg_content:
        panel_html = f"""<div class="panel-svg">{panel_svg_content}</div>"""

    # Issues section
    issues_html = ""
    if report.issues:
        issues_html = f"""
        <div class="issues">
            <ul>
                {"".join(f'<li>{html.escape(issue)}</li>' for issue in report.issues)}
            </ul>
        </div>
        """

    # Parameter grid section (if full mode)
    param_grid_html = ""
    if config.include_param_grid and report.param_permutations:
        module_id = report.module_name.replace(" ", "_").lower()
        param_grid_html = f"""
        <div class="param-grid-section">
            <h4>Parameter Grid ({len(report.param_permutations)} permutations)</h4>
            <button class="toggle-btn" onclick="toggleSection('param-grid-{module_id}')">
                Toggle Grid
            </button>
            <div id="param-grid-{module_id}" class="hidden">
                {generate_param_grid_content(report)}
            </div>
        </div>
        """

    # Build visualizations HTML
    vis_html = ""
    if spectrogram_b64:
        vis_html += f"<img class='spectrogram' src='data:image/png;base64,{spectrogram_b64}' alt='Spectrogram'>"
    if notes_b64:
        vis_html += f"<img class='spectrogram' src='data:image/png;base64,{notes_b64}' alt='Note Score' style='margin-top:8px;'>"
    if automation_b64:
        vis_html += f"<img class='spectrogram' src='data:image/png;base64,{automation_b64}' alt='Parameter Automation' style='margin-top:8px;'>"

    # Audio player
    audio_html = ""
    if audio_b64:
        audio_html = f"<audio controls><source src='data:audio/wav;base64,{audio_b64}' type='audio/wav'></audio>"
    else:
        audio_html = "<p style='color:#888;'>No audio available</p>"

    return f"""
    <div class="module-card" id="module-{report.module_name}">
        <div class="module-header">
            <div>
                <h2>{html.escape(report.module_name)}</h2>
                <span class="module-type">{html.escape(report.module_type)} | {report.duration:.1f}s</span>
            </div>
            {get_status_badge(report.status)}
        </div>
        <div class="panel-section">
            {panel_html}
            <div style="flex:1;">
                <p style="color:#888;margin:0 0 8px 0;font-size:14px;">{html.escape(report.description)}</p>
            </div>
        </div>
        <div class="module-content">
            <div class="audio-section">
                <h3>Showcase Audio</h3>
                {audio_html}
                {vis_html if vis_html else "<p style='color:#888;'>No visualizations available</p>"}
            </div>
            <div class="metrics-section">
                <h3>Quality Metrics</h3>
                <div class="metric-row">
                    <span class="metric-label">Peak Amplitude</span>
                    {peak_badge}
                </div>
                <div class="metric-row">
                    <span class="metric-label">RMS Level</span>
                    {rms_badge}
                </div>
                <div class="metric-row">
                    <span class="metric-label">THD</span>
                    {thd_badge}
                </div>
                <div class="metric-row">
                    <span class="metric-label">HNR</span>
                    {hnr_badge}
                </div>
                <div class="metric-row">
                    <span class="metric-label">Clipping</span>
                    {clip_badge}
                </div>
                <div class="metric-row">
                    <span class="metric-label">DC Offset</span>
                    {dc_badge}
                </div>
                {ai_html}
                {issues_html}
            </div>
        </div>
        {param_grid_html}
        {gemini_html}
    </div>
"""


def generate_param_grid_content(report: ModuleReport) -> str:
    """Generate HTML content for parameter grid section."""
    if not report.param_permutations:
        return "<p>No parameter permutations available</p>"

    # Get parameter ranges for visualization
    module_params = get_module_params(report.module_name)
    param_ranges = {p["name"]: (p["min"], p["max"]) for p in module_params}

    items_html = []
    for idx, perm in enumerate(report.param_permutations[:20], 1):  # Limit to 20
        # Generate parameter bars
        param_bars = ""
        for param_name in sorted(perm.params.keys()):
            if param_name.lower() not in ["gate", "trigger", "velocity", "volts", "freq", "pitch"]:
                value = perm.params[param_name]
                min_val, max_val = param_ranges.get(param_name, (0, 1))
                param_bars += generate_param_bar_html(param_name, value, min_val, max_val)

        items_html.append(f"""
        <div style="background:#1e1e35;padding:10px;border-radius:6px;margin-bottom:8px;">
            <div style="font-size:11px;color:#666;margin-bottom:6px;">#{idx}</div>
            <div class="params-container">{param_bars}</div>
        </div>
        """)

    return "".join(items_html)


# =============================================================================
# Main Pipeline
# =============================================================================

def process_module(module_name: str, output_dir: Path,
                   config: ReportConfig) -> ModuleReport:
    """Process a single module and generate its report."""

    # Load module config
    module_config = load_module_config(module_name)
    module_type = module_config.get("module_type", "instrument")
    description = module_config.get("description", "")
    skip_audio = module_config.get("skip_audio_tests", False)

    report = ModuleReport(
        module_name=module_name,
        module_type=module_type,
        description=description,
    )

    # Skip utility modules
    if skip_audio:
        report.status = "skip"
        skip_reason = module_config.get("skip_reason", "Audio tests skipped")
        report.issues.append(skip_reason)
        if config.verbose:
            print(f"  Skipping {module_name}: {skip_reason}")
        return report

    # Render showcase audio
    wav_path = output_dir / f"{module_name}_showcase.wav"
    if config.verbose:
        print(f"  Rendering showcase for {module_name}...")

    if not render_showcase(module_name, wav_path, config.verbose):
        report.status = "error"
        report.issues.append("Failed to render showcase audio")
        return report

    report.showcase_wav = wav_path

    # Load audio for analysis
    audio = load_audio(wav_path)
    if audio is None:
        report.status = "error"
        report.issues.append("Failed to load rendered audio")
        return report

    report.duration = len(audio) / SAMPLE_RATE

    # Parse showcase config for visualizations
    showcase = parse_showcase_config(module_config, module_type)
    report.showcase_config = showcase

    # Get panel SVG
    panel_svg = get_panel_svg(module_name)
    if panel_svg:
        report.panel_svg = panel_svg

    # Generate spectrogram
    spectrogram_path = output_dir / f"{module_name}_spectrogram.png"
    if generate_spectrogram(audio, spectrogram_path, module_name):
        report.spectrogram = spectrogram_path

    # Get module parameters for complete parameter graph
    module_params = get_module_params(module_name)

    # Generate parameter values graph (all params, with automations highlighted)
    automation_path = output_dir / f"{module_name}_automation.png"
    if generate_automation_graph(showcase, automation_path, module_params, "Parameter Values"):
        report.automation_graph = automation_path

    # Generate note score visualization
    if showcase.notes:
        note_path = output_dir / f"{module_name}_notes.png"
        if generate_note_score(showcase, note_path, "Note Sequence"):
            report.note_score = note_path

    # Analyze quality
    if config.verbose:
        print(f"  Analyzing quality for {module_name}...")
    report.quality = analyze_quality(audio)

    # AI analysis (CLAP + optional Gemini)
    use_clap = not config.skip_clap
    use_gemini = not config.skip_gemini

    if use_clap or use_gemini:
        if config.verbose:
            print(f"  Running AI analysis for {module_name}...")
        showcase_context = format_showcase_context(showcase, module_type)
        report.ai, report.gemini_analysis = run_ai_analysis(
            wav_path, module_name, showcase_context, config.verbose,
            use_clap=use_clap, use_gemini=use_gemini
        )

    # Determine status based on quality metrics
    issues = []

    # Check quality thresholds
    thresholds = module_config.get("quality_thresholds", DEFAULT_QUALITY_THRESHOLDS)
    thd_max = thresholds.get("thd_max_percent", 15.0)
    clipping_max = thresholds.get("clipping_max_percent", 1.0)
    if thresholds.get("allow_hot_signal", False):
        clipping_max = 15.0  # More lenient for hot signal modules

    if report.quality.thd_percent > thd_max:
        issues.append(f"THD {report.quality.thd_percent:.1f}% exceeds threshold {thd_max}%")

    if report.quality.clipping_percent > clipping_max:
        issues.append(f"Clipping {report.quality.clipping_percent:.1f}% exceeds threshold {clipping_max}%")

    if report.quality.peak_amplitude < 0.1:
        issues.append("Very low output level (peak < 0.1)")

    if abs(report.quality.dc_offset) > 0.1:
        issues.append(f"Significant DC offset: {report.quality.dc_offset:.3f}")

    report.issues = issues
    report.status = "pass" if not issues else "needs_work"

    return report


def parse_args(args: list[str] | None = None) -> ReportConfig:
    """Parse command line arguments and return configuration."""
    parser = argparse.ArgumentParser(
        description="Generate unified audio report for VCV modules",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Modes:
  --fast     No AI analysis (fastest, for CI)
  (default)  CLAP analysis only (standard PR review)
  --full     CLAP + Gemini + parameter grid (deep dive)
        """
    )
    parser.add_argument("--module", "-m", help="Process specific module only")
    parser.add_argument("--fast", action="store_true",
                        help="Fast mode: skip all AI analysis")
    parser.add_argument("--full", action="store_true",
                        help="Full mode: include Gemini and parameter grid")
    parser.add_argument("--gemini", action="store_true",
                        help="Enable Gemini analysis (requires GEMINI_API_KEY)")
    parser.add_argument("--no-clap", action="store_true",
                        help="Skip CLAP analysis")
    parser.add_argument("--output", "-o", default=str(OUTPUT_DIR / "unified_report.html"),
                        help="Output HTML file path")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--json", action="store_true", help="Also output JSON report")
    parser.add_argument("--parallel", "-p", type=int, default=4,
                        help="Number of parallel workers (default: 4)")

    parsed = parser.parse_args(args)

    config = ReportConfig()
    config.verbose = parsed.verbose
    config.parallel_workers = parsed.parallel
    config.output_path = Path(parsed.output)

    # Mode configuration
    if parsed.fast:
        config.skip_clap = True
        config.skip_gemini = True
        config.include_param_grid = False
    elif parsed.full:
        config.skip_clap = False
        config.skip_gemini = False
        config.include_param_grid = True
    else:
        # Default mode
        config.skip_clap = parsed.no_clap
        config.skip_gemini = not parsed.gemini
        config.include_param_grid = False

    # Store parsed args for module filtering
    config._module = parsed.module
    config._json = parsed.json

    return config


def main():
    config = parse_args()

    # Ensure output directory exists
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Get modules to process
    if config._module:
        modules = [config._module]
    else:
        modules = get_available_modules()
        if not modules:
            print("No modules found. Ensure faust_render is built.")
            sys.exit(1)

    # Determine mode for display
    if config.skip_clap and config.skip_gemini:
        mode_str = "fast (no AI)"
    elif not config.skip_gemini:
        mode_str = "full (CLAP + Gemini)"
    else:
        mode_str = "standard (CLAP)"

    print(f"Processing {len(modules)} module(s) in {mode_str} mode...")

    # Process modules
    reports = []

    if config.parallel_workers > 1 and len(modules) > 1:
        # Parallel processing
        with ThreadPoolExecutor(max_workers=config.parallel_workers) as executor:
            futures = {
                executor.submit(process_module, m, OUTPUT_DIR, config): m
                for m in modules
            }
            for future in as_completed(futures):
                module = futures[future]
                try:
                    report = future.result()
                    reports.append(report)
                    status_symbol = {"pass": "✓", "needs_work": "⚠", "skip": "○", "error": "✗"}.get(report.status, "?")
                    print(f"  {status_symbol} {module} ({report.status})")
                except Exception as e:
                    print(f"  ✗ {module} (error: {e})")
    else:
        # Sequential processing
        for module in modules:
            try:
                report = process_module(module, OUTPUT_DIR, config)
                reports.append(report)
                status_symbol = {"pass": "✓", "needs_work": "⚠", "skip": "○", "error": "✗"}.get(report.status, "?")
                print(f"  {status_symbol} {module} ({report.status})")
            except Exception as e:
                print(f"  ✗ {module} (error: {e})")

    # Sort reports by module name
    reports.sort(key=lambda r: r.module_name)

    # Generate HTML report
    generate_html_report(reports, config.output_path, config)

    # Optionally output JSON
    if config._json:
        json_path = config.output_path.with_suffix(".json")
        with open(json_path, "w") as f:
            json.dump([r.to_dict() for r in reports], f, indent=2)
        print(f"JSON report written to: {json_path}")

    # Summary
    print("\nSummary:")
    for status in ["pass", "needs_work", "skip", "error"]:
        count = sum(1 for r in reports if r.status == status)
        if count > 0:
            print(f"  {status.upper()}: {count}")


if __name__ == "__main__":
    main()
