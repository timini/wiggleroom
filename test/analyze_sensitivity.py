#!/usr/bin/env python3
"""
Parameter Sensitivity Analysis for Faust DSP Modules

Analyzes how much each parameter actually affects the audio output.
Uses audio feature extraction and statistical analysis to quantify
parameter impact.

Usage:
    python analyze_sensitivity.py --module ChaosFlute
    python analyze_sensitivity.py --module ChaosFlute --param pressure
    python analyze_sensitivity.py  # All modules
"""

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import librosa
import matplotlib.pyplot as plt
import numpy as np
from scipy import stats

# Configuration
SAMPLE_RATE = 48000
DURATION = 2.0
NUM_STEPS = 5  # Number of values to test per parameter


@dataclass
class AudioFeatures:
    """Audio features extracted from a render."""
    rms_mean: float          # Average loudness
    rms_std: float           # Loudness variation
    spectral_centroid: float # Brightness (Hz)
    spectral_bandwidth: float # Timbre spread
    spectral_rolloff: float  # High frequency content
    zero_crossing_rate: float # Noisiness
    spectral_flux: float     # Temporal change

    def to_vector(self) -> np.ndarray:
        """Convert to numpy array for comparison."""
        return np.array([
            self.rms_mean,
            self.rms_std,
            self.spectral_centroid,
            self.spectral_bandwidth,
            self.spectral_rolloff,
            self.zero_crossing_rate,
            self.spectral_flux,
        ])

    @staticmethod
    def feature_names() -> list[str]:
        return [
            "RMS Mean",
            "RMS Std",
            "Spectral Centroid",
            "Spectral Bandwidth",
            "Spectral Rolloff",
            "Zero Crossing Rate",
            "Spectral Flux",
        ]


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


def get_modules() -> list[str]:
    success, output = run_faust_render(["--list-modules"])
    if not success:
        return []
    return [line.strip() for line in output.strip().split("\n")
            if line.strip() and not line.startswith("Available")]


def get_module_params(module_name: str) -> list[dict[str, Any]]:
    success, output = run_faust_render(["--module", module_name, "--list-params"])
    if not success:
        return []

    params = []
    for line in output.strip().split("\n"):
        line = line.strip()
        if line.startswith("["):
            try:
                bracket_end = line.index("]")
                idx = int(line[1:bracket_end])
                rest = line[bracket_end + 1:].strip()
                path_end = rest.index("(")
                path = rest[:path_end].strip()
                name = path.split("/")[-1]
                meta = rest[path_end + 1:-1]
                min_val = float(meta.split("min=")[1].split(",")[0])
                max_val = float(meta.split("max=")[1].split(",")[0])
                init_val = float(meta.split("init=")[1].split(")")[0])
                params.append({
                    "index": idx, "name": name, "path": path,
                    "min": min_val, "max": max_val, "init": init_val,
                })
            except (ValueError, IndexError):
                continue
    return params


def render_audio(module_name: str, params: dict[str, float],
                 output_path: Path) -> bool:
    args = [
        "--module", module_name,
        "--output", str(output_path),
        "--duration", str(DURATION),
        "--sample-rate", str(SAMPLE_RATE),
    ]
    for name, value in params.items():
        args.extend(["--param", f"{name}={value}"])

    success, _ = run_faust_render(args)
    return success


def extract_features(audio_path: Path) -> AudioFeatures | None:
    """Extract audio features from a WAV file."""
    try:
        y, sr = librosa.load(str(audio_path), sr=SAMPLE_RATE, mono=True)

        # Skip if audio is essentially silent
        if np.max(np.abs(y)) < 1e-6:
            return None

        # RMS energy
        rms = librosa.feature.rms(y=y)[0]
        rms_mean = float(np.mean(rms))
        rms_std = float(np.std(rms))

        # Spectral features
        spec_cent = librosa.feature.spectral_centroid(y=y, sr=sr)[0]
        spec_bw = librosa.feature.spectral_bandwidth(y=y, sr=sr)[0]
        spec_rolloff = librosa.feature.spectral_rolloff(y=y, sr=sr)[0]
        zcr = librosa.feature.zero_crossing_rate(y)[0]

        # Spectral flux (onset strength as proxy)
        onset_env = librosa.onset.onset_strength(y=y, sr=sr)
        spectral_flux = float(np.mean(onset_env))

        return AudioFeatures(
            rms_mean=rms_mean,
            rms_std=rms_std,
            spectral_centroid=float(np.mean(spec_cent)),
            spectral_bandwidth=float(np.mean(spec_bw)),
            spectral_rolloff=float(np.mean(spec_rolloff)),
            zero_crossing_rate=float(np.mean(zcr)),
            spectral_flux=spectral_flux,
        )
    except Exception as e:
        print(f"  Error extracting features: {e}")
        return None


def compute_sensitivity(features_list: list[tuple[float, AudioFeatures]]) -> dict[str, float]:
    """
    Compute sensitivity metrics for a parameter sweep.
    Returns normalized sensitivity scores for each audio feature.
    """
    if len(features_list) < 2:
        return {}

    param_values = np.array([f[0] for f in features_list])
    feature_matrix = np.array([f[1].to_vector() for f in features_list])

    sensitivities = {}
    feature_names = AudioFeatures.feature_names()

    for i, name in enumerate(feature_names):
        feature_values = feature_matrix[:, i]

        # Normalize feature values to [0, 1] range
        f_min, f_max = np.min(feature_values), np.max(feature_values)
        if f_max - f_min > 1e-10:
            normalized = (feature_values - f_min) / (f_max - f_min)
            # Compute coefficient of variation (relative spread)
            cv = np.std(normalized) / (np.mean(normalized) + 1e-10)
            # Compute correlation with parameter value
            corr, _ = stats.pearsonr(param_values, feature_values)
            sensitivities[name] = {
                "range": float(f_max - f_min),
                "cv": float(cv),
                "correlation": float(corr) if not np.isnan(corr) else 0.0,
                "values": feature_values.tolist(),
            }
        else:
            sensitivities[name] = {
                "range": 0.0,
                "cv": 0.0,
                "correlation": 0.0,
                "values": feature_values.tolist(),
            }

    return sensitivities


def analyze_parameter(
    module_name: str,
    param: dict[str, Any],
    all_params: list[dict[str, Any]],
    output_dir: Path,
    num_steps: int = NUM_STEPS,
) -> dict[str, Any]:
    """Analyze sensitivity of a single parameter."""
    param_name = param["name"]

    # Skip control parameters
    if param_name.lower() in ["gate", "trigger", "velocity", "volts", "freq", "pitch"]:
        return {"skipped": True, "reason": "control parameter"}

    print(f"  Analyzing: {param_name} ({param['min']} -> {param['max']})")

    # Generate test values
    test_values = np.linspace(param["min"], param["max"], num_steps)

    # Set default values for other parameters
    default_params = {}
    for p in all_params:
        if p["name"] != param_name:
            default_params[p["name"]] = p["init"]

    # Render and extract features for each value
    features_list = []
    wav_files = []

    for i, value in enumerate(test_values):
        test_params = default_params.copy()
        test_params[param_name] = value

        wav_path = output_dir / f"{module_name}_{param_name}_{i:02d}.wav"

        if render_audio(module_name, test_params, wav_path):
            features = extract_features(wav_path)
            if features:
                features_list.append((value, features))
                wav_files.append(str(wav_path))

    if len(features_list) < 2:
        return {"skipped": True, "reason": "insufficient valid renders"}

    # Compute sensitivity
    sensitivities = compute_sensitivity(features_list)

    # Compute overall sensitivity score (weighted average of feature sensitivities)
    overall_score = 0.0
    weights = {
        "RMS Mean": 2.0,           # Loudness is important
        "Spectral Centroid": 1.5,  # Brightness matters
        "Spectral Bandwidth": 1.0,
        "Spectral Rolloff": 0.5,
        "Zero Crossing Rate": 0.5,
        "Spectral Flux": 1.0,
        "RMS Std": 0.5,
    }

    total_weight = 0
    for feat_name, data in sensitivities.items():
        weight = weights.get(feat_name, 1.0)
        # Use coefficient of variation as sensitivity measure
        overall_score += weight * abs(data["cv"])
        total_weight += weight

    overall_score /= total_weight

    return {
        "skipped": False,
        "param_name": param_name,
        "param_range": [param["min"], param["max"]],
        "test_values": test_values.tolist(),
        "sensitivities": sensitivities,
        "overall_score": overall_score,
        "wav_files": wav_files,
    }


def generate_sensitivity_plot(
    module_name: str,
    results: dict[str, Any],
    output_path: Path,
):
    """Generate a visual sensitivity report."""
    # Filter to non-skipped parameters
    param_results = {k: v for k, v in results.items()
                     if isinstance(v, dict) and not v.get("skipped", True)}

    if not param_results:
        print("  No valid parameters to plot")
        return

    # Sort by overall sensitivity score
    sorted_params = sorted(param_results.items(),
                          key=lambda x: x[1]["overall_score"],
                          reverse=True)

    fig, axes = plt.subplots(2, 1, figsize=(12, 10))

    # Plot 1: Overall sensitivity scores
    ax1 = axes[0]
    param_names = [p[0] for p in sorted_params]
    scores = [p[1]["overall_score"] for p in sorted_params]
    colors = plt.cm.RdYlGn_r(np.linspace(0.2, 0.8, len(scores)))

    bars = ax1.barh(param_names, scores, color=colors)
    ax1.set_xlabel("Sensitivity Score (higher = more impact)")
    ax1.set_title(f"{module_name}: Parameter Sensitivity Analysis")
    ax1.axvline(x=0.1, color='red', linestyle='--', alpha=0.5, label='Low impact threshold')
    ax1.legend()

    # Add value labels
    for bar, score in zip(bars, scores):
        width = bar.get_width()
        label = "LOW" if score < 0.1 else ("MED" if score < 0.3 else "HIGH")
        ax1.text(width + 0.01, bar.get_y() + bar.get_height()/2,
                f'{score:.3f} ({label})', va='center', fontsize=9)

    # Plot 2: Feature breakdown for top parameters
    ax2 = axes[1]
    feature_names = AudioFeatures.feature_names()
    x = np.arange(len(feature_names))
    width = 0.8 / min(len(sorted_params), 5)

    for i, (param_name, data) in enumerate(sorted_params[:5]):
        sensitivities = data["sensitivities"]
        cvs = [abs(sensitivities.get(f, {}).get("cv", 0)) for f in feature_names]
        offset = (i - min(len(sorted_params), 5)/2 + 0.5) * width
        ax2.bar(x + offset, cvs, width, label=param_name, alpha=0.8)

    ax2.set_xlabel("Audio Feature")
    ax2.set_ylabel("Coefficient of Variation")
    ax2.set_title("Feature-level Sensitivity (Top 5 Parameters)")
    ax2.set_xticks(x)
    ax2.set_xticklabels(feature_names, rotation=45, ha='right')
    ax2.legend(loc='upper right')
    ax2.axhline(y=0.1, color='red', linestyle='--', alpha=0.5)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"  Saved plot: {output_path}")


def generate_param_sweep_plot(
    module_name: str,
    param_name: str,
    result: dict[str, Any],
    output_path: Path,
):
    """Generate detailed sweep plot for a single parameter."""
    if result.get("skipped"):
        return

    test_values = result["test_values"]
    sensitivities = result["sensitivities"]

    fig, axes = plt.subplots(2, 2, figsize=(12, 10))

    # Plot key features vs parameter value
    features_to_plot = [
        ("RMS Mean", "Loudness"),
        ("Spectral Centroid", "Brightness (Hz)"),
        ("Spectral Bandwidth", "Timbre Width"),
        ("Spectral Flux", "Temporal Change"),
    ]

    for ax, (feat_name, ylabel) in zip(axes.flat, features_to_plot):
        if feat_name in sensitivities:
            values = sensitivities[feat_name]["values"]
            corr = sensitivities[feat_name]["correlation"]
            cv = sensitivities[feat_name]["cv"]

            ax.plot(test_values, values, 'o-', linewidth=2, markersize=8)
            ax.set_xlabel(f"{param_name} value")
            ax.set_ylabel(ylabel)
            ax.set_title(f"{feat_name}\n(r={corr:.2f}, CV={cv:.3f})")
            ax.grid(True, alpha=0.3)

    fig.suptitle(f"{module_name}: {param_name} Parameter Sweep", fontsize=14)
    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()


def generate_html_report(
    module_name: str,
    results: dict[str, Any],
    output_dir: Path,
) -> Path:
    """Generate HTML sensitivity report."""
    report_path = output_dir / f"{module_name}_sensitivity.html"

    # Sort parameters by sensitivity
    param_results = [(k, v) for k, v in results.items()
                     if isinstance(v, dict) and not v.get("skipped", True)]
    param_results.sort(key=lambda x: x[1]["overall_score"], reverse=True)

    skipped = [(k, v) for k, v in results.items()
               if isinstance(v, dict) and v.get("skipped", True)]

    html = f"""<!DOCTYPE html>
<html>
<head>
    <title>{module_name} - Parameter Sensitivity Analysis</title>
    <style>
        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
            margin: 0; padding: 20px;
            background: #1a1a2e; color: #eee;
        }}
        h1 {{ color: #fff; }}
        h2 {{ color: #a0a0ff; border-bottom: 2px solid #333; padding-bottom: 10px; }}
        .summary {{
            background: #252540; padding: 20px; border-radius: 10px;
            margin-bottom: 20px;
        }}
        .param-card {{
            background: #252540; padding: 20px; border-radius: 10px;
            margin-bottom: 15px;
        }}
        .score {{
            font-size: 24px; font-weight: bold;
            padding: 5px 15px; border-radius: 20px;
            display: inline-block; margin-left: 10px;
        }}
        .high {{ background: #4caf50; color: white; }}
        .medium {{ background: #ff9800; color: white; }}
        .low {{ background: #f44336; color: white; }}
        .feature-grid {{
            display: grid; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
            gap: 10px; margin-top: 15px;
        }}
        .feature-item {{
            background: #1e1e35; padding: 10px; border-radius: 6px;
        }}
        .feature-name {{ color: #888; font-size: 12px; }}
        .feature-value {{ color: #aaf; font-size: 16px; font-weight: bold; }}
        .bar {{
            height: 8px; background: #333; border-radius: 4px;
            margin-top: 5px; overflow: hidden;
        }}
        .bar-fill {{
            height: 100%; background: linear-gradient(90deg, #4a4a8a, #7a7aff);
            border-radius: 4px;
        }}
        img {{ max-width: 100%; border-radius: 8px; margin: 10px 0; }}
        .skipped {{ color: #888; font-style: italic; }}
        table {{ width: 100%; border-collapse: collapse; margin-top: 10px; }}
        th, td {{ padding: 8px; text-align: left; border-bottom: 1px solid #333; }}
        th {{ color: #888; }}
    </style>
</head>
<body>
    <h1>{module_name} - Parameter Sensitivity Analysis</h1>

    <div class="summary">
        <h2>Summary</h2>
        <p><strong>Parameters analyzed:</strong> {len(param_results)}</p>
        <p><strong>Parameters skipped:</strong> {len(skipped)} (control signals)</p>
        <img src="{module_name}_sensitivity.png" alt="Sensitivity Overview">
    </div>

    <h2>Parameter Details</h2>
"""

    for param_name, data in param_results:
        score = data["overall_score"]
        score_class = "high" if score >= 0.3 else ("medium" if score >= 0.1 else "low")
        score_label = "HIGH IMPACT" if score >= 0.3 else ("MEDIUM" if score >= 0.1 else "LOW IMPACT")

        html += f"""
    <div class="param-card">
        <h3>{param_name}
            <span class="score {score_class}">{score:.3f} - {score_label}</span>
        </h3>
        <p>Range: {data['param_range'][0]:.2f} to {data['param_range'][1]:.2f}</p>

        <div class="feature-grid">
"""
        for feat_name, feat_data in data["sensitivities"].items():
            cv = abs(feat_data["cv"])
            corr = feat_data["correlation"]
            bar_width = min(cv * 100 / 0.5, 100)  # Scale CV to percentage

            html += f"""
            <div class="feature-item">
                <div class="feature-name">{feat_name}</div>
                <div class="feature-value">CV: {cv:.3f}</div>
                <div style="font-size: 11px; color: #666;">r = {corr:+.2f}</div>
                <div class="bar"><div class="bar-fill" style="width: {bar_width}%"></div></div>
            </div>
"""

        html += f"""
        </div>
        <img src="{module_name}_{param_name}_sweep.png" alt="{param_name} sweep">
    </div>
"""

    if skipped:
        html += """
    <h2>Skipped Parameters</h2>
    <ul class="skipped">
"""
        for param_name, data in skipped:
            html += f"        <li>{param_name}: {data.get('reason', 'unknown')}</li>\n"
        html += "    </ul>\n"

    html += """
    <h2>Interpretation Guide</h2>
    <table>
        <tr><th>Metric</th><th>Meaning</th></tr>
        <tr><td>Sensitivity Score</td><td>Overall measure of parameter impact (0-1). Higher = more effect on sound.</td></tr>
        <tr><td>CV (Coefficient of Variation)</td><td>Relative spread of feature values. Higher = more variation.</td></tr>
        <tr><td>Correlation (r)</td><td>Linear relationship with parameter. +1/-1 = strong, 0 = none.</td></tr>
    </table>
    <p style="margin-top: 20px; color: #888;">
        <strong>LOW IMPACT</strong> (score &lt; 0.1): Parameter may not be working or has subtle effect.<br>
        <strong>MEDIUM</strong> (0.1 - 0.3): Parameter has noticeable effect.<br>
        <strong>HIGH IMPACT</strong> (score &gt; 0.3): Parameter significantly changes the sound.
    </p>
</body>
</html>
"""

    with open(report_path, "w") as f:
        f.write(html)

    return report_path


def analyze_module(module_name: str, output_dir: Path,
                   specific_param: str = None,
                   num_steps: int = NUM_STEPS) -> dict[str, Any]:
    """Run full sensitivity analysis for a module."""
    print(f"\n{'=' * 60}")
    print(f"Analyzing: {module_name}")
    print("=" * 60)

    params = get_module_params(module_name)
    if not params:
        print(f"  Error: Could not get parameters")
        return {}

    print(f"  Found {len(params)} parameters")

    # Create output directory
    module_dir = output_dir / module_name
    module_dir.mkdir(parents=True, exist_ok=True)

    results = {}

    for param in params:
        if specific_param and param["name"] != specific_param:
            continue

        result = analyze_parameter(module_name, param, params, module_dir, num_steps)
        results[param["name"]] = result

        # Generate individual sweep plot
        if not result.get("skipped"):
            sweep_plot = module_dir / f"{module_name}_{param['name']}_sweep.png"
            generate_param_sweep_plot(module_name, param["name"], result, sweep_plot)

    # Generate overview plot
    overview_plot = module_dir / f"{module_name}_sensitivity.png"
    generate_sensitivity_plot(module_name, results, overview_plot)

    # Generate HTML report
    report_path = generate_html_report(module_name, results, module_dir)
    print(f"\n  Report: {report_path}")

    # Print summary
    print(f"\n  Summary:")
    scored = [(k, v["overall_score"]) for k, v in results.items()
              if isinstance(v, dict) and not v.get("skipped")]
    scored.sort(key=lambda x: x[1], reverse=True)

    for name, score in scored:
        impact = "HIGH" if score >= 0.3 else ("MED" if score >= 0.1 else "LOW")
        print(f"    {name}: {score:.3f} ({impact})")

    return results


def main():
    parser = argparse.ArgumentParser(
        description="Analyze parameter sensitivity for Faust DSP modules"
    )
    parser.add_argument("--module", help="Specific module to analyze")
    parser.add_argument("--param", help="Specific parameter to analyze")
    parser.add_argument("--output", default="test/sensitivity",
                       help="Output directory")
    parser.add_argument("--steps", type=int, default=NUM_STEPS,
                       help=f"Number of test values per parameter (default: {NUM_STEPS})")

    args = parser.parse_args()

    project_root = get_project_root()
    output_dir = project_root / args.output
    output_dir.mkdir(parents=True, exist_ok=True)

    # Check renderer exists
    if not get_render_executable().exists():
        print("Error: faust_render not found. Run 'just build' first.")
        sys.exit(1)

    num_steps = args.steps

    if args.module:
        analyze_module(args.module, output_dir, args.param, num_steps)
    else:
        modules = get_modules()
        for module in modules:
            analyze_module(module, output_dir, num_steps=num_steps)

    print(f"\nAll reports saved to: {output_dir}")


if __name__ == "__main__":
    main()
