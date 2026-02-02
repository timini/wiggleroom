#!/usr/bin/env python3
"""
Generate Showcase Report for VCV Rack Modules

Renders comprehensive showcase audio for each module with multiple notes and
parameter sweeps, then generates a consolidated HTML report with:
- Audio player for each module
- Spectrogram visualization
- Quality metrics (THD, HNR, clipping, peak)
- AI analysis (CLAP scores, character)
- Pass/fail status

Usage:
    python generate_showcase_report.py              # All modules
    python generate_showcase_report.py --module ChaosFlute
    python generate_showcase_report.py --skip-ai    # Skip AI analysis
"""

import argparse
import base64
import html
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path
from typing import Any

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
    from scipy import signal
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False

try:
    import librosa
    HAS_LIBROSA = True
except ImportError:
    HAS_LIBROSA = False

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False


# Configuration
SAMPLE_RATE = 48000
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
    status: str = "pending"  # pass, needs_work, skip
    issues: list[str] = field(default_factory=list)
    duration: float = 0.0

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


def generate_systematic_showcase(module_name: str, module_type: str,
                                  base_duration: float = 20.0) -> ShowcaseConfig:
    """Generate systematic showcase config that covers all parameter permutations.

    Strategy:
    1. Get all sweepable parameters
    2. Divide time into segments for individual sweeps
    3. Each param gets swept low->high then high->low
    4. Add combination segments where 2 params change together
    5. For instruments, distribute notes across the duration
    """
    params = get_module_params(module_name)

    # Filter out control signals
    exclude = {'gate', 'trigger', 'volts', 'velocity'}
    sweep_params = [p for p in params if p.name not in exclude]

    if not sweep_params:
        return get_default_showcase_config(module_type)

    n_params = len(sweep_params)

    # Calculate duration based on number of params (more params = longer showcase)
    # Each param gets ~4s for up/down sweep, plus time for combinations
    individual_time = n_params * 4.0
    combo_time = min(n_params * 2.0, 8.0)  # Time for combination sweeps
    duration = max(base_duration, individual_time + combo_time)

    automations = []
    current_time = 0.0
    segment_duration = duration / (n_params * 2 + 2)  # Time per segment

    # Phase 1: Sweep each parameter individually (low->high, then high->low)
    for i, param in enumerate(sweep_params):
        # Low to high sweep
        automations.append(ShowcaseAutomation(
            param=param.name,
            start_time=current_time,
            end_time=current_time + segment_duration,
            start_value=0.1,
            end_value=0.9
        ))
        current_time += segment_duration

        # High to low sweep
        automations.append(ShowcaseAutomation(
            param=param.name,
            start_time=current_time,
            end_time=current_time + segment_duration,
            start_value=0.9,
            end_value=0.2
        ))
        current_time += segment_duration

    # Phase 2: Combination sweeps (pairs of params moving together/opposite)
    if n_params >= 2:
        remaining_time = duration - current_time
        combo_segment = remaining_time / 2

        # First two params moving together
        automations.append(ShowcaseAutomation(
            param=sweep_params[0].name,
            start_time=current_time,
            end_time=current_time + combo_segment,
            start_value=0.2,
            end_value=0.8
        ))
        automations.append(ShowcaseAutomation(
            param=sweep_params[1].name,
            start_time=current_time,
            end_time=current_time + combo_segment,
            start_value=0.2,
            end_value=0.8
        ))
        current_time += combo_segment

        # First two params moving opposite
        automations.append(ShowcaseAutomation(
            param=sweep_params[0].name,
            start_time=current_time,
            end_time=duration,
            start_value=0.8,
            end_value=0.2
        ))
        automations.append(ShowcaseAutomation(
            param=sweep_params[1].name,
            start_time=current_time,
            end_time=duration,
            start_value=0.2,
            end_value=0.8
        ))

    # Generate notes for instruments
    notes = []
    if module_type == "instrument":
        # Distribute notes across duration, covering pitch range
        n_notes = max(8, int(duration / 2))  # One note every ~2 seconds
        note_duration = 1.5

        # Pitch sequence covering range: C2, G2, C3, E3, G3, C4, E4, G4, C5...
        pitch_sequence = [-2.0, -1.42, -1.0, -0.67, -0.42, 0.0, 0.33, 0.58, 1.0, 0.25, -0.25, 0.5]

        for i in range(n_notes):
            start = i * (duration / n_notes)
            volts = pitch_sequence[i % len(pitch_sequence)]
            velocity = 0.8 + 0.2 * ((i % 3) / 2)  # Vary velocity slightly

            notes.append(ShowcaseNote(
                start=start,
                duration=note_duration,
                volts=volts,
                velocity=velocity
            ))

    return ShowcaseConfig(
        duration=duration,
        notes=notes,
        automations=automations
    )


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

    lines.append("\n**Listen for:**")
    if showcase.notes:
        lines.append("- How does each note sound across the pitch range?")
        lines.append("- Are there any problematic notes or registers?")
    if showcase.automations:
        params = set(a.param for a in showcase.automations)
        lines.append(f"- How do the parameters ({', '.join(params)}) affect the sound?")
        lines.append("- Are there any problematic parameter values or transitions?")

    return "\n".join(lines)


def render_showcase(module_name: str, output_path: Path, verbose: bool = False) -> bool:
    """Render showcase audio for a module."""
    exe = get_render_executable()
    if not exe.exists():
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
        if verbose:
            print(result.stdout)
        if result.returncode != 0:
            print(f"Error rendering {module_name}: {result.stderr}")
            return False
        return True
    except subprocess.TimeoutExpired:
        print(f"Timeout rendering {module_name}")
        return False
    except Exception as e:
        print(f"Error rendering {module_name}: {e}")
        return False


def render_showcase_with_config(module_name: str, output_path: Path,
                                 showcase: ShowcaseConfig, verbose: bool = False) -> bool:
    """Render showcase audio using a custom ShowcaseConfig.

    Writes a temporary config file for faust_render to use.
    """
    import tempfile

    # Create temp config file with showcase config
    config = load_module_config(module_name)

    # Convert ShowcaseConfig to dict format expected by faust_render
    config["showcase"] = {
        "duration": showcase.duration,
        "notes": [
            {"start": n.start, "duration": n.duration, "volts": n.volts, "velocity": n.velocity}
            for n in showcase.notes
        ],
        "automations": [
            {"param": a.param, "start_time": a.start_time, "end_time": a.end_time,
             "start_value": a.start_value, "end_value": a.end_value}
            for a in showcase.automations
        ]
    }

    # Write to temp file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
        json.dump(config, f)
        temp_config_path = f.name

    try:
        exe = get_render_executable()
        if not exe.exists():
            return False

        args = [
            str(exe),
            "--module", module_name,
            "--showcase",
            "--showcase-config", temp_config_path,
            "--output", str(output_path),
            "--sample-rate", str(SAMPLE_RATE),
        ]

        result = subprocess.run(args, capture_output=True, text=True, timeout=180)
        if verbose:
            print(result.stdout)
        if result.returncode != 0:
            print(f"Error rendering {module_name}: {result.stderr}")
            return False
        return True
    except Exception as e:
        print(f"Error rendering {module_name}: {e}")
        return False
    finally:
        # Clean up temp file
        Path(temp_config_path).unlink(missing_ok=True)


@dataclass
class ModuleParam:
    """Module parameter information."""
    name: str
    min_val: float
    max_val: float
    init_val: float


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
        import re
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
# Spectrogram Generation
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
        print(f"Error generating spectrogram: {e}")
        return False


# =============================================================================
# Parameter Automation Graph
# =============================================================================

def generate_automation_graph(showcase: ShowcaseConfig, output_path: Path,
                               module_params: list[ModuleParam] = None, title: str = "") -> bool:
    """Generate parameter values graph showing all parameters over time.

    Args:
        showcase: Showcase config with automations
        output_path: Output image path
        module_params: All module parameters (to show non-automated ones)
        title: Graph title
    """
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
                if param.name in exclude or param.name in automated_params:
                    continue

                # Normalize to 0-1 range
                if param.max_val > param.min_val:
                    norm_value = (param.init_val - param.min_val) / (param.max_val - param.min_val)
                else:
                    norm_value = 0.5

                color = colors[param_idx % len(colors)]
                ax.plot(t_full, np.full_like(t_full, norm_value), color=color,
                       linewidth=1, alpha=0.4, linestyle='--', label=f"{param.name} (fixed)")
                param_idx += 1

        # Then, plot automated parameters with their sweeps
        for auto in showcase.automations:
            color = colors[param_idx % len(colors)]

            # Build the full timeline for this parameter
            values = np.zeros_like(t_full)

            # Find automations for this param
            param_autos = [a for a in showcase.automations if a.param == auto.param]

            # Only process once per param
            if auto != param_autos[0]:
                continue

            # Get initial value from module_params if available
            init_val = 0.5
            if module_params:
                for p in module_params:
                    if p.name == auto.param and p.max_val > p.min_val:
                        init_val = (p.init_val - p.min_val) / (p.max_val - p.min_val)
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
        print(f"Error generating automation graph: {e}")
        import traceback
        traceback.print_exc()
        return False


# =============================================================================
# Note Score Visualization
# =============================================================================

def volts_to_note_name(volts: float) -> str:
    """Convert V/Oct voltage to note name."""
    # 0V = C4 (MIDI 60)
    semitones = round(volts * 12)
    midi_note = 60 + semitones

    note_names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    octave = (midi_note // 12) - 1
    note = note_names[midi_note % 12]
    return f"{note}{octave}"


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
        print(f"Error generating note score: {e}")
        return False


# =============================================================================
# AI Analysis
# =============================================================================

def run_ai_analysis(wav_path: Path, module_name: str, showcase_context: str,
                    verbose: bool = False, use_clap: bool = True,
                    use_gemini: bool = True) -> tuple[AIAnalysis, str]:
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
        sys.path.insert(0, str(Path(__file__).parent))
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

    except Exception as e:
        if verbose:
            print(f"AI analysis error: {e}")
            import traceback
            traceback.print_exc()

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
    return f'<span style="background:{color};color:white;padding:2px 8px;border-radius:4px;font-size:12px;">{status.upper()}</span>'


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


def generate_html_report(reports: list[ModuleReport], output_path: Path):
    """Generate consolidated HTML report."""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # Count statuses
    status_counts = {"pass": 0, "needs_work": 0, "skip": 0, "error": 0}
    for r in reports:
        status_counts[r.status] = status_counts.get(r.status, 0) + 1

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

    html_content = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiggleRoom Showcase Report</title>
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
            margin-bottom: 30px;
        }}
        .header h1 {{ margin: 0; color: #fff; }}
        .header .stats {{ margin-top: 10px; font-size: 14px; color: #aaa; }}
        .stats span {{ margin: 0 10px; }}
        .module-card {{
            background: #16213e;
            border-radius: 12px;
            margin-bottom: 24px;
            padding: 20px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.3);
        }}
        .module-header {{
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 16px;
            border-bottom: 1px solid #333;
            padding-bottom: 12px;
        }}
        .module-header h2 {{ margin: 0; color: #fff; }}
        .module-type {{ color: #888; font-size: 14px; }}
        .module-content {{
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
        }}
        @media (max-width: 900px) {{
            .module-content {{ grid-template-columns: 1fr; }}
        }}
        .audio-section {{
            background: #0f3460;
            border-radius: 8px;
            padding: 16px;
        }}
        .audio-section h3 {{ margin-top: 0; font-size: 14px; color: #aaa; }}
        .audio-section audio {{ width: 100%; }}
        .spectrogram {{ max-width: 100%; border-radius: 4px; margin-top: 10px; }}
        .metrics-section {{
            background: #0f3460;
            border-radius: 8px;
            padding: 16px;
        }}
        .metrics-section h3 {{ margin-top: 0; font-size: 14px; color: #aaa; }}
        .metric-row {{
            display: flex;
            justify-content: space-between;
            padding: 6px 0;
            border-bottom: 1px solid #1a3a5e;
        }}
        .metric-row:last-child {{ border-bottom: none; }}
        .metric-label {{ color: #888; }}
        .ai-section {{ margin-top: 16px; }}
        .ai-section h4 {{ margin: 0 0 8px 0; font-size: 13px; color: #aaa; }}
        .character-tags {{ display: flex; flex-wrap: wrap; gap: 4px; }}
        .character-tag {{
            background: #1a3a5e;
            padding: 2px 8px;
            border-radius: 4px;
            font-size: 12px;
        }}
        .issues {{ margin-top: 10px; padding: 10px; background: #3a1c1c; border-radius: 4px; }}
        .issues ul {{ margin: 0; padding-left: 20px; }}
        .issues li {{ color: #ff8888; font-size: 13px; }}
        .panel-section {{
            display: flex;
            align-items: flex-start;
            gap: 16px;
            margin-bottom: 16px;
        }}
        .panel-svg {{
            width: 120px;
            min-width: 120px;
            background: #1a1a2e;
            border-radius: 8px;
            padding: 8px;
            border: 1px solid #333;
        }}
        .panel-svg svg {{
            width: 100%;
            height: auto;
        }}
        .gemini-section {{
            margin-top: 16px;
            padding: 16px;
            background: #0a1628;
            border-radius: 8px;
            border-left: 3px solid #4ecdc4;
        }}
        .gemini-section h4 {{
            margin: 0 0 12px 0;
            font-size: 14px;
            color: #4ecdc4;
        }}
        .gemini-content {{
            color: #ccc;
            font-size: 13px;
            line-height: 1.6;
            max-height: 400px;
            overflow-y: auto;
            white-space: pre-wrap;
        }}
        .gemini-toggle {{
            background: #1a3a5e;
            border: none;
            color: #aaa;
            padding: 4px 12px;
            border-radius: 4px;
            cursor: pointer;
            font-size: 12px;
            margin-bottom: 8px;
        }}
        .gemini-toggle:hover {{ background: #2a4a6e; color: #fff; }}
        .hidden {{ display: none; }}
        .toc {{
            background: #16213e;
            border-radius: 12px;
            padding: 16px 20px;
            margin-bottom: 24px;
        }}
        .toc h3 {{
            margin: 0 0 12px 0;
            color: #fff;
            font-size: 16px;
        }}
        .toc-grid {{
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
            gap: 8px;
        }}
        .toc-item {{
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
        }}
        .toc-item:hover {{ background: #1a4a7e; color: #fff; }}
        .toc-status {{
            width: 8px;
            height: 8px;
            border-radius: 50%;
            flex-shrink: 0;
        }}
        .toc-type {{ color: #666; font-size: 11px; }}
    </style>
</head>
<body>
    <div class="header">
        <h1>WiggleRoom Showcase Report</h1>
        <p class="stats">
            Generated: {timestamp} |
            <span style="color:#28a745;">Pass: {status_counts['pass']}</span> |
            <span style="color:#ffc107;">Needs Work: {status_counts['needs_work']}</span> |
            <span style="color:#6c757d;">Skip: {status_counts['skip']}</span>
        </p>
    </div>
    <div class="toc">
        <h3>Modules</h3>
        <div class="toc-grid">
            {toc_html}
        </div>
    </div>
"""

    for report in reports:
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
                <button class="gemini-toggle" onclick="document.getElementById('gemini-{module_id}').classList.toggle('hidden')">
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

        # Parameter grid link (if exists)
        grid_link_html = ""
        grid_report_path = OUTPUT_DIR / "param_grids" / f"{report.module_name}.html"
        if grid_report_path.exists():
            # Count clips from directory
            grid_dir = OUTPUT_DIR / "param_grids" / report.module_name
            clip_count = len(list(grid_dir.glob("perm_*.wav"))) if grid_dir.exists() else 0
            grid_link_html = f"""
            <div style="margin-top:12px;padding:10px;background:#0a1628;border-radius:6px;border-left:3px solid #4ecdc4;">
                <a href="param_grids/{report.module_name}.html" style="color:#4ecdc4;text-decoration:none;font-size:13px;">
                    📊 Parameter Grid Analysis ({clip_count} permutations)
                </a>
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

        html_content += f"""
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
                <h3>Audio & Visualizations</h3>
                {"<audio controls><source src='data:audio/wav;base64," + audio_b64 + "' type='audio/wav'></audio>" if audio_b64 else "<p>No audio available</p>"}
                {vis_html if vis_html else "<p>No visualizations available</p>"}
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
                {grid_link_html}
            </div>
        </div>
        {gemini_html}
    </div>
"""

    html_content += """
</body>
</html>
"""

    output_path.write_text(html_content)
    print(f"Report written to: {output_path}")


# =============================================================================
# Main Pipeline
# =============================================================================

def process_module(module_name: str, output_dir: Path,
                   skip_ai: bool = False, verbose: bool = False) -> ModuleReport:
    """Process a single module and generate its report."""

    # Load module config
    config = load_module_config(module_name)
    module_type = config.get("module_type", "instrument")
    description = config.get("description", "")
    skip_audio = config.get("skip_audio_tests", False)

    report = ModuleReport(
        module_name=module_name,
        module_type=module_type,
        description=description,
    )

    # Skip utility modules
    if skip_audio:
        report.status = "skip"
        skip_reason = config.get("skip_reason", "Audio tests skipped")
        report.issues.append(skip_reason)
        if verbose:
            print(f"  Skipping {module_name}: {skip_reason}")
        return report

    # Render showcase audio
    wav_path = output_dir / f"{module_name}_showcase.wav"
    if verbose:
        print(f"  Rendering showcase for {module_name}...")

    if not render_showcase(module_name, wav_path, verbose):
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
    showcase = parse_showcase_config(config, module_type)
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
    if verbose:
        print(f"  Analyzing quality for {module_name}...")
    report.quality = analyze_quality(audio)

    # AI analysis (CLAP + Gemini)
    if not skip_ai:
        if verbose:
            print(f"  Running AI analysis for {module_name}...")
        # Format showcase context for AI
        showcase_context = format_showcase_context(showcase, module_type)
        report.ai, report.gemini_analysis = run_ai_analysis(
            wav_path, module_name, showcase_context, verbose
        )

    # Determine status based on quality metrics
    issues = []

    # Check quality thresholds
    thresholds = config.get("quality_thresholds", {})
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


def main():
    parser = argparse.ArgumentParser(description="Generate showcase report for VCV modules")
    parser.add_argument("--module", "-m", help="Process specific module only")
    parser.add_argument("--skip-ai", action="store_true", help="Skip AI analysis")
    parser.add_argument("--output", "-o", default=str(OUTPUT_DIR / "showcase_report.html"),
                        help="Output HTML file path")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--json", action="store_true", help="Also output JSON report")
    parser.add_argument("--parallel", "-p", type=int, default=4,
                        help="Number of parallel workers (default: 4)")
    parser.add_argument("--systematic", "-s", action="store_true",
                        help="Use systematic parameter sweeps (auto-generated)")
    parser.add_argument("--no-clap", action="store_true",
                        help="Skip CLAP analysis (faster)")
    parser.add_argument("--no-gemini", action="store_true",
                        help="Skip Gemini analysis")
    args = parser.parse_args()

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

    print(f"Processing {len(modules)} module(s) with {args.parallel} workers...")

    # Generate systematic configs if requested
    systematic_configs = {}
    if args.systematic:
        print("\n=== Generating systematic parameter sweeps ===")
        for module in modules:
            config = load_module_config(module)
            module_type = config.get("module_type", "instrument")
            systematic_configs[module] = generate_systematic_showcase(module, module_type)
            n_params = len([a.param for a in systematic_configs[module].automations])
            n_notes = len(systematic_configs[module].notes)
            print(f"  {module}: {systematic_configs[module].duration:.0f}s, "
                  f"{n_params} param sweeps, {n_notes} notes")

    # Phase 1: Render all audio in parallel
    print("\n=== Phase 1: Rendering audio ===")
    wav_paths = {}

    def render_module(module: str) -> tuple[str, Path, bool]:
        wav_path = OUTPUT_DIR / f"{module}_showcase.wav"
        if module in systematic_configs:
            # Use systematic config - write temp config for faust_render
            success = render_showcase_with_config(
                module, wav_path, systematic_configs[module], args.verbose
            )
        else:
            success = render_showcase(module, wav_path, args.verbose)
        return module, wav_path, success

    with ThreadPoolExecutor(max_workers=args.parallel) as executor:
        futures = [executor.submit(render_module, m) for m in modules]

        for future in as_completed(futures):
            try:
                module, wav_path, success = future.result()
                if success:
                    wav_paths[module] = wav_path
                    print(f"  ✓ {module}")
                else:
                    print(f"  ✗ {module} (render failed)")
            except Exception as e:
                print(f"  ✗ (error: {e})")

    # Phase 2: Process analysis in parallel (quality, visualizations)
    print("\n=== Phase 2: Analyzing audio ===")
    reports = []

    def analyze_module(module_name: str) -> ModuleReport:
        """Analyze a single module (called in parallel)."""
        config = load_module_config(module_name)
        module_type = config.get("module_type", "instrument")
        description = config.get("description", "")
        skip_audio = config.get("skip_audio_tests", False)

        report = ModuleReport(
            module_name=module_name,
            module_type=module_type,
            description=description,
        )

        if skip_audio:
            report.status = "skip"
            report.issues.append(config.get("skip_reason", "Audio tests skipped"))
            return report

        wav_path = wav_paths.get(module_name)
        if not wav_path or not wav_path.exists():
            report.status = "error"
            report.issues.append("Audio file not found")
            return report

        report.showcase_wav = wav_path

        # Load audio
        audio = load_audio(wav_path)
        if audio is None:
            report.status = "error"
            report.issues.append("Failed to load audio")
            return report

        report.duration = len(audio) / SAMPLE_RATE

        # Use systematic config if available, otherwise parse from file
        if module_name in systematic_configs:
            showcase = systematic_configs[module_name]
        else:
            showcase = parse_showcase_config(config, module_type)
        report.showcase_config = showcase

        # Panel SVG
        panel_svg = get_panel_svg(module_name)
        if panel_svg:
            report.panel_svg = panel_svg

        # Generate visualizations
        spectrogram_path = OUTPUT_DIR / f"{module_name}_spectrogram.png"
        if generate_spectrogram(audio, spectrogram_path, module_name):
            report.spectrogram = spectrogram_path

        module_params = get_module_params(module_name)
        automation_path = OUTPUT_DIR / f"{module_name}_automation.png"
        if generate_automation_graph(showcase, automation_path, module_params, "Parameter Values"):
            report.automation_graph = automation_path

        if showcase.notes:
            note_path = OUTPUT_DIR / f"{module_name}_notes.png"
            if generate_note_score(showcase, note_path, "Note Sequence"):
                report.note_score = note_path

        # Quality analysis
        report.quality = analyze_quality(audio)

        # Determine status
        thresholds = config.get("quality_thresholds", {})
        thd_max = thresholds.get("thd_max_percent", 15.0)
        clipping_max = thresholds.get("clipping_max_percent", 1.0)
        if thresholds.get("allow_hot_signal", False):
            clipping_max = 15.0

        issues = []
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

    with ThreadPoolExecutor(max_workers=args.parallel) as executor:
        futures = {executor.submit(analyze_module, m): m for m in modules}
        for future in as_completed(futures):
            module = futures[future]
            try:
                report = future.result()
                reports.append(report)
                print(f"  ✓ {module} ({report.status})")
            except Exception as e:
                print(f"  ✗ {module} (error: {e})")
                import traceback
                traceback.print_exc()

    # Phase 3: AI analysis
    use_clap = not args.no_clap
    use_gemini_opt = not args.no_gemini
    if not args.skip_ai and (use_clap or use_gemini_opt):
        print(f"\n=== Phase 3: AI analysis (CLAP: {use_clap}, Gemini: {use_gemini_opt}) ===")
        for report in reports:
            if report.status in ("skip", "error"):
                continue
            print(f"  Analyzing {report.module_name}...")
            # Use the showcase config already stored in the report
            showcase_context = format_showcase_context(report.showcase_config, report.module_type)
            report.ai, report.gemini_analysis = run_ai_analysis(
                report.showcase_wav, report.module_name, showcase_context, args.verbose,
                use_clap=use_clap, use_gemini=use_gemini_opt
            )

    # Sort reports by module name
    reports.sort(key=lambda r: r.module_name)

    # Generate HTML report
    output_path = Path(args.output)
    generate_html_report(reports, output_path)

    # Optionally output JSON
    if args.json:
        json_path = output_path.with_suffix(".json")
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
