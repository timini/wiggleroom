#!/usr/bin/env python3
"""
Parameter Range Analyzer

Sweeps parameters to find:
1. Safe ranges (no clipping, stable output)
2. Interesting ranges (good dynamic response)
3. Problem areas (clipping, silence, instability)

Outputs recommendations for parameter min/max/default values.

Usage:
    python3 test/analyze_param_ranges.py --module ChaosFlute
    python3 test/analyze_param_ranges.py --module ChaosFlute --param pressure --steps 20
    python3 test/analyze_param_ranges.py --module ChaosFlute --all --output ranges.json
"""

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

# Import shared utilities
from utils import (
    get_project_root,
    get_render_executable,
    get_module_params as _get_module_params,
)


@dataclass
class ParamInfo:
    """Parameter metadata from faust_render."""
    name: str
    min_val: float
    max_val: float
    init_val: float


@dataclass
class SweepPoint:
    """Result of rendering at a specific parameter value."""
    value: float
    peak_amplitude: float
    rms_level: float
    clipping_percent: float
    is_silent: bool = False
    has_error: bool = False


@dataclass
class RangeAnalysis:
    """Analysis results for a parameter."""
    param_name: str
    original_min: float
    original_max: float
    original_init: float
    sweep_points: list[SweepPoint] = field(default_factory=list)
    # Recommendations
    recommended_min: float | None = None
    recommended_max: float | None = None
    recommended_init: float | None = None
    safe_range: tuple[float, float] | None = None
    interesting_range: tuple[float, float] | None = None
    problem_areas: list[tuple[float, float, str]] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)


def get_module_params(module_name: str) -> list[ParamInfo]:
    """Get parameter info from faust_render."""
    params_list = _get_module_params(module_name)
    return [
        ParamInfo(
            name=p["name"],
            min_val=p["min"],
            max_val=p["max"],
            init_val=p["init"]
        )
        for p in params_list
    ]


def render_with_param(module_name: str, param_name: str, param_value: float,
                      duration: float = 1.5, gate_params: dict | None = None) -> SweepPoint:
    """Render audio with a specific parameter value and analyze."""
    exe = get_render_executable()

    args = [
        str(exe),
        "--module", module_name,
        "--output", "/dev/null",
        "--duration", str(duration),
        "--param", f"{param_name}={param_value}"
    ]

    # Add gate/trigger if needed
    if gate_params:
        for k, v in gate_params.items():
            args.extend(["--param", f"{k}={v}"])
    else:
        # Default: try gate=5 for instruments
        args.extend(["--param", "gate=5"])

    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr

        # Parse output
        peak = 0.0
        rms = 0.0
        clipping = 0.0

        peak_match = re.search(r'Peak amplitude:\s*([\d.]+)', output)
        if peak_match:
            peak = float(peak_match.group(1))

        rms_match = re.search(r'RMS level:\s*([\d.]+)', output)
        if rms_match:
            rms = float(rms_match.group(1))

        clip_match = re.search(r'Clipped samples:.*\(([\d.]+)%\)', output)
        if clip_match:
            clipping = float(clip_match.group(1))

        is_silent = rms < 0.001  # Effectively silent

        return SweepPoint(
            value=param_value,
            peak_amplitude=peak,
            rms_level=rms,
            clipping_percent=clipping,
            is_silent=is_silent
        )
    except Exception as e:
        return SweepPoint(
            value=param_value,
            peak_amplitude=0,
            rms_level=0,
            clipping_percent=0,
            has_error=True
        )


def analyze_parameter(module_name: str, param: ParamInfo, steps: int = 15) -> RangeAnalysis:
    """Sweep a parameter and analyze the results."""
    analysis = RangeAnalysis(
        param_name=param.name,
        original_min=param.min_val,
        original_max=param.max_val,
        original_init=param.init_val
    )

    # Generate sweep values
    step_size = (param.max_val - param.min_val) / (steps - 1)
    values = [param.min_val + i * step_size for i in range(steps)]

    print(f"  Sweeping {param.name}: {param.min_val} -> {param.max_val} ({steps} steps)")

    for val in values:
        point = render_with_param(module_name, param.name, val)
        analysis.sweep_points.append(point)

        # Progress indicator
        status = "✓" if point.clipping_percent == 0 and not point.is_silent else "!"
        if point.clipping_percent > 0:
            status = f"⚠ {point.clipping_percent:.1f}%"
        elif point.is_silent:
            status = "○"
        print(f"    {val:.3f}: {status}", end="\r")

    print()  # Newline after progress

    # Analyze results
    _analyze_sweep_results(analysis)

    return analysis


def _analyze_sweep_results(analysis: RangeAnalysis):
    """Analyze sweep results to find safe/interesting ranges."""
    points = analysis.sweep_points
    if not points:
        return

    # Find safe range (no clipping)
    safe_points = [p for p in points if p.clipping_percent == 0 and not p.is_silent and not p.has_error]
    if safe_points:
        safe_min = min(p.value for p in safe_points)
        safe_max = max(p.value for p in safe_points)
        analysis.safe_range = (safe_min, safe_max)

    # Find interesting range (good RMS, dynamic variation)
    rms_values = [p.rms_level for p in points if not p.is_silent and not p.has_error]
    if rms_values:
        avg_rms = sum(rms_values) / len(rms_values)
        # Interesting = above average RMS and safe
        interesting_points = [p for p in safe_points if p.rms_level >= avg_rms * 0.5]
        if interesting_points:
            analysis.interesting_range = (
                min(p.value for p in interesting_points),
                max(p.value for p in interesting_points)
            )

    # Find problem areas
    for p in points:
        if p.clipping_percent > 0:
            analysis.problem_areas.append((p.value, p.value, f"clipping {p.clipping_percent:.1f}%"))
        elif p.is_silent:
            analysis.problem_areas.append((p.value, p.value, "silent"))

    # Merge adjacent problem areas
    analysis.problem_areas = _merge_adjacent_problems(analysis.problem_areas)

    # Generate recommendations
    if analysis.safe_range:
        # Constrain to safe range if problems found
        if analysis.problem_areas:
            analysis.recommended_min = analysis.safe_range[0]
            analysis.recommended_max = analysis.safe_range[1]
            analysis.notes.append(f"Constrained from [{analysis.original_min}, {analysis.original_max}] to safe range")

        # Set init to center of interesting range if available
        if analysis.interesting_range:
            analysis.recommended_init = (analysis.interesting_range[0] + analysis.interesting_range[1]) / 2
        elif analysis.safe_range:
            analysis.recommended_init = (analysis.safe_range[0] + analysis.safe_range[1]) / 2

    # Keep original if within safe range
    if analysis.safe_range:
        if analysis.safe_range[0] <= analysis.original_init <= analysis.safe_range[1]:
            analysis.recommended_init = analysis.original_init
            analysis.notes.append("Original init value is within safe range, keeping it")


def _merge_adjacent_problems(problems: list[tuple[float, float, str]]) -> list[tuple[float, float, str]]:
    """Merge adjacent problem areas with the same issue type."""
    if not problems:
        return []

    # Sort by start value
    sorted_probs = sorted(problems, key=lambda x: x[0])
    merged = [sorted_probs[0]]

    for start, end, issue in sorted_probs[1:]:
        last_start, last_end, last_issue = merged[-1]
        # Merge if same issue type and adjacent
        if issue.split()[0] == last_issue.split()[0]:  # Compare issue type
            merged[-1] = (last_start, end, issue)
        else:
            merged.append((start, end, issue))

    return merged


def print_analysis(analysis: RangeAnalysis, verbose: bool = False):
    """Print analysis results."""
    print(f"\n  {analysis.param_name}:")
    print(f"    Original: [{analysis.original_min}, {analysis.original_max}] init={analysis.original_init}")

    if analysis.safe_range:
        print(f"    Safe range: [{analysis.safe_range[0]:.3f}, {analysis.safe_range[1]:.3f}]")
    else:
        print(f"    Safe range: None found!")

    if analysis.interesting_range:
        print(f"    Interesting: [{analysis.interesting_range[0]:.3f}, {analysis.interesting_range[1]:.3f}]")

    if analysis.problem_areas:
        print(f"    Problems:")
        for start, end, issue in analysis.problem_areas:
            if start == end:
                print(f"      - {start:.3f}: {issue}")
            else:
                print(f"      - [{start:.3f}, {end:.3f}]: {issue}")

    if analysis.recommended_min is not None or analysis.recommended_max is not None:
        rec_min = analysis.recommended_min if analysis.recommended_min is not None else analysis.original_min
        rec_max = analysis.recommended_max if analysis.recommended_max is not None else analysis.original_max
        rec_init = analysis.recommended_init if analysis.recommended_init is not None else analysis.original_init
        print(f"    Recommended: [{rec_min:.3f}, {rec_max:.3f}] init={rec_init:.3f}")

    if verbose and analysis.notes:
        print(f"    Notes:")
        for note in analysis.notes:
            print(f"      - {note}")


def generate_faust_recommendations(module_name: str, analyses: list[RangeAnalysis]) -> str:
    """Generate Faust DSP code snippets with recommended ranges."""
    lines = [f"// Recommended parameter ranges for {module_name}"]
    lines.append("// Based on automated sweep analysis")
    lines.append("")

    for a in analyses:
        if a.recommended_min is not None or a.recommended_max is not None:
            rec_min = a.recommended_min if a.recommended_min is not None else a.original_min
            rec_max = a.recommended_max if a.recommended_max is not None else a.original_max
            rec_init = a.recommended_init if a.recommended_init is not None else a.original_init

            # Check if range changed
            if rec_min != a.original_min or rec_max != a.original_max:
                lines.append(f"// {a.param_name}: constrained from [{a.original_min}, {a.original_max}]")
                lines.append(f'{a.param_name} = hslider("{a.param_name}", {rec_init:.3f}, {rec_min:.3f}, {rec_max:.3f}, 0.01);')
                if a.problem_areas:
                    lines.append(f"// Avoided: {', '.join(f'{s:.2f}-{e:.2f} ({i})' for s, e, i in a.problem_areas)}")
                lines.append("")

    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description="Analyze parameter ranges for safe/interesting areas")
    parser.add_argument("--module", required=True, help="Module to analyze")
    parser.add_argument("--param", help="Specific parameter to analyze (default: all)")
    parser.add_argument("--steps", type=int, default=15, help="Number of sweep steps (default: 15)")
    parser.add_argument("--all", action="store_true", help="Analyze all parameters")
    parser.add_argument("--output", help="Output JSON file")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("--faust", action="store_true", help="Output Faust code recommendations")

    args = parser.parse_args()

    # Check executable
    if not get_render_executable().exists():
        print("Error: faust_render not found. Run 'just build' first.")
        sys.exit(1)

    # Get parameters
    params = get_module_params(args.module)
    if not params:
        print(f"Error: No parameters found for {args.module}")
        sys.exit(1)

    # Filter to specific param if requested
    if args.param:
        params = [p for p in params if p.name == args.param]
        if not params:
            print(f"Error: Parameter '{args.param}' not found")
            sys.exit(1)

    # Skip gate/velocity params (they're triggers, not ranges)
    skip_params = {'gate', 'velocity', 'trig', 'trigger'}
    params = [p for p in params if p.name.lower() not in skip_params]

    print(f"Analyzing {args.module} ({len(params)} parameters)")

    # Analyze each parameter
    analyses = []
    for param in params:
        analysis = analyze_parameter(args.module, param, args.steps)
        analyses.append(analysis)
        print_analysis(analysis, args.verbose)

    # Output Faust recommendations
    if args.faust:
        print("\n" + "=" * 60)
        print("FAUST RECOMMENDATIONS")
        print("=" * 60)
        print(generate_faust_recommendations(args.module, analyses))

    # JSON output
    if args.output:
        json_data = {
            "module": args.module,
            "parameters": []
        }
        for a in analyses:
            json_data["parameters"].append({
                "name": a.param_name,
                "original": {"min": a.original_min, "max": a.original_max, "init": a.original_init},
                "safe_range": list(a.safe_range) if a.safe_range else None,
                "interesting_range": list(a.interesting_range) if a.interesting_range else None,
                "recommended": {
                    "min": a.recommended_min,
                    "max": a.recommended_max,
                    "init": a.recommended_init
                } if a.recommended_min is not None else None,
                "problems": [{"start": s, "end": e, "issue": i} for s, e, i in a.problem_areas],
                "notes": a.notes
            })

        with open(args.output, 'w') as f:
            json.dump(json_data, f, indent=2)
        print(f"\nResults saved to: {args.output}")

    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)

    constrained = [a for a in analyses if a.recommended_min is not None and
                   (a.recommended_min != a.original_min or a.recommended_max != a.original_max)]
    if constrained:
        print(f"Parameters needing range constraints: {len(constrained)}")
        for a in constrained:
            print(f"  - {a.param_name}: [{a.original_min}, {a.original_max}] -> [{a.recommended_min:.3f}, {a.recommended_max:.3f}]")
    else:
        print("All parameters have safe full ranges ✓")

    problematic = [a for a in analyses if a.problem_areas]
    if problematic:
        print(f"\nParameters with problem areas: {len(problematic)}")
        for a in problematic:
            issues = ', '.join(i for _, _, i in a.problem_areas[:3])
            print(f"  - {a.param_name}: {issues}")


if __name__ == "__main__":
    main()
