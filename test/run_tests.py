#!/usr/bin/env python3
"""
Faust Module Test Runner

Orchestrates audio rendering and spectrogram generation for all Faust modules
with parameter grid testing.

Usage:
    python run_tests.py                    # Run all tests
    python run_tests.py --module MoogLPF   # Test specific module
    python run_tests.py --quick            # Quick test (fewer param values)
    python run_tests.py --report           # Generate HTML report only
"""

import argparse
import itertools
import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from datetime import datetime
from pathlib import Path
from typing import Any

# Configuration
SAMPLE_RATE = 48000
DURATION = 2.0
PARAM_VALUES_FULL = 5  # Number of values per parameter (full grid)
PARAM_VALUES_QUICK = 3  # Number of values per parameter (quick mode)
MAX_COMBINATIONS = 500  # Maximum combinations per module
NUM_WORKERS = 8  # Number of parallel workers


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


def run_faust_render(args: list[str]) -> tuple[bool, str]:
    """Run faust_render with given arguments."""
    exe = get_render_executable()
    if not exe.exists():
        return False, f"Executable not found: {exe}"

    cmd = [str(exe)] + args
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            return False, result.stderr
        return True, result.stdout
    except subprocess.TimeoutExpired:
        return False, "Timeout"
    except Exception as e:
        return False, str(e)


def get_module_params(module_name: str) -> list[dict[str, Any]]:
    """Get parameter info for a module."""
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

                rest = line[bracket_end + 1 :].strip()
                path_end = rest.index("(")
                path = rest[:path_end].strip()

                # Extract name from path (last component)
                name = path.split("/")[-1]

                # Parse min/max/init
                meta = rest[path_end + 1 : -1]
                min_val = float(meta.split("min=")[1].split(",")[0])
                max_val = float(meta.split("max=")[1].split(",")[0])
                init_val = float(meta.split("init=")[1].split(")")[0])

                params.append(
                    {
                        "index": idx,
                        "name": name,
                        "path": path,
                        "min": min_val,
                        "max": max_val,
                        "init": init_val,
                    }
                )
            except (ValueError, IndexError):
                continue

    return params


def get_modules() -> list[str]:
    """Get list of available modules."""
    success, output = run_faust_render(["--list-modules"])
    if not success:
        return []

    modules = []
    for line in output.strip().split("\n"):
        line = line.strip()
        if line and not line.startswith("Available"):
            modules.append(line)

    return modules


def generate_param_values(
    param: dict[str, Any], num_values: int
) -> list[tuple[str, float]]:
    """Generate parameter values for testing."""
    min_val = param["min"]
    max_val = param["max"]
    name = param["name"]

    # Skip gate/trigger parameters (handled by renderer)
    if name.lower() in ["gate", "trigger", "velocity"]:
        return [(name, param["init"])]

    # Skip V/Oct pitch parameter (use default)
    if name.lower() in ["volts", "freq", "pitch"]:
        return [(name, param["init"])]

    values = []
    for i in range(num_values):
        if num_values == 1:
            t = 0.5
        else:
            t = i / (num_values - 1)
        value = min_val + t * (max_val - min_val)
        values.append((name, value))

    return values


def generate_param_grid(
    params: list[dict[str, Any]], num_values: int, max_combinations: int
) -> list[dict[str, float]]:
    """Generate parameter combinations for testing."""
    # Get values for each parameter
    param_values = {}
    for param in params:
        values = generate_param_values(param, num_values)
        param_values[param["name"]] = values

    # Generate all combinations
    names = list(param_values.keys())
    value_lists = [param_values[name] for name in names]

    combinations = []
    for combo in itertools.product(*value_lists):
        param_dict = {name: value for name, value in combo}
        combinations.append(param_dict)
        if len(combinations) >= max_combinations:
            break

    return combinations


def render_audio(
    module_name: str,
    params: dict[str, float],
    output_dir: Path,
    duration: float = DURATION,
    sample_rate: int = SAMPLE_RATE,
) -> tuple[bool, Path, dict[str, float] | None]:
    """Render audio for a parameter combination.

    Returns: (success, output_path, metadata_if_hashed)
    The third element is the params dict if hash was used, None otherwise.
    """
    # Create filename from parameters
    param_str = "_".join(f"{k}={v:.2f}" for k, v in sorted(params.items()))
    hash_metadata = None
    if len(param_str) > 100:
        # Hash for very long param strings, save metadata separately
        import hashlib

        param_hash = hashlib.md5(param_str.encode()).hexdigest()[:8]
        param_str = f"hash_{param_hash}"
        hash_metadata = params  # Return params for metadata file

    filename = f"{module_name}_{param_str}.wav"
    output_path = output_dir / filename

    # Build command
    args = [
        "--module",
        module_name,
        "--output",
        str(output_path),
        "--duration",
        str(duration),
        "--sample-rate",
        str(sample_rate),
    ]
    for name, value in params.items():
        args.extend(["--param", f"{name}={value}"])

    success, msg = run_faust_render(args)
    return success, output_path, hash_metadata


def generate_spectrogram(wav_path: Path, output_dir: Path) -> tuple[bool, Path]:
    """Generate spectrogram for a WAV file."""
    output_path = output_dir / wav_path.with_suffix(".png").name

    try:
        # Import here to avoid startup cost if not needed
        from analyze_audio import generate_spectrogram as gen_spec, load_audio

        y, sr = load_audio(str(wav_path))
        fig = gen_spec(y, sr, title=wav_path.stem, output_path=str(output_path))
        import matplotlib.pyplot as plt

        plt.close(fig)
        return True, output_path
    except Exception as e:
        print(f"  Error generating spectrogram: {e}")
        return False, output_path


def render_single_test(args: tuple) -> dict[str, Any]:
    """Worker function for parallel rendering. Renders audio and generates spectrogram."""
    module_name, param_combo, wav_dir, spec_dir, duration, sample_rate = args

    # Render audio
    success, wav_path, hash_metadata = render_audio(module_name, param_combo, wav_dir, duration, sample_rate)
    if not success:
        return {"params": param_combo, "success": False}

    # If we used a hash filename, save metadata for the report generator
    if hash_metadata is not None:
        metadata_file = wav_dir / "param_metadata.json"
        # Load existing metadata or create new
        existing = {}
        if metadata_file.exists():
            try:
                existing = json.loads(metadata_file.read_text())
            except Exception:
                pass
        # Add this file's metadata
        existing[wav_path.name] = hash_metadata
        metadata_file.write_text(json.dumps(existing, indent=2))

    # Generate spectrogram
    spec_success, spec_path = generate_spectrogram(wav_path, spec_dir)

    return {
        "params": param_combo,
        "success": True,
        "wav": str(wav_path),
        "spectrogram": str(spec_path) if spec_success else None,
    }


def sort_results_by_params(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Sort results by parameter values for consistent ordering."""
    def sort_key(result):
        params = result.get("params", {})
        # Sort by parameter values in alphabetical order of param names
        return tuple(params.get(k, 0) for k in sorted(params.keys()))
    return sorted(results, key=sort_key)


def generate_param_bar_html(
    name: str, value: float, min_val: float, max_val: float
) -> str:
    """Generate HTML for a parameter visualization bar."""
    # Calculate percentage position
    if max_val == min_val:
        pct = 50
    else:
        pct = ((value - min_val) / (max_val - min_val)) * 100
    pct = max(0, min(100, pct))

    # Format value nicely
    if abs(value) < 0.01:
        val_str = f"{value:.3f}"
    elif abs(value) < 10:
        val_str = f"{value:.2f}"
    else:
        val_str = f"{value:.1f}"

    return f'''<div class="param-row">
        <span class="param-name">{name}</span>
        <div class="param-bar-container">
            <div class="param-bar" style="width: {pct:.1f}%"></div>
            <div class="param-marker" style="left: {pct:.1f}%"></div>
        </div>
        <span class="param-value">{val_str}</span>
    </div>'''


def generate_html_report(
    output_dir: Path,
    module_results: dict[str, list[dict[str, Any]]],
    param_metadata: dict[str, list[dict[str, Any]]] = None
) -> Path:
    """Generate HTML report with all spectrograms."""
    report_path = output_dir / "report.html"
    param_metadata = param_metadata or {}

    total_renders = sum(len(results) for results in module_results.values())
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # Create audio directory and copy WAV files
    audio_dir = output_dir / "audio"
    audio_dir.mkdir(parents=True, exist_ok=True)

    html = f"""<!DOCTYPE html>
<html>
<head>
    <title>Faust Module Audio Test Report</title>
    <style>
        * {{ box-sizing: border-box; }}
        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            margin: 0;
            padding: 20px;
            background: #1a1a2e;
            color: #eee;
        }}
        h1 {{
            color: #fff;
            margin-bottom: 20px;
            font-weight: 300;
        }}
        h2 {{
            color: #a0a0ff;
            border-bottom: 2px solid #333;
            padding-bottom: 10px;
            font-weight: 400;
        }}
        .module {{
            margin-bottom: 40px;
            background: #252540;
            padding: 25px;
            border-radius: 12px;
            box-shadow: 0 4px 20px rgba(0,0,0,0.3);
        }}
        .module-info {{
            margin-bottom: 20px;
            padding: 15px;
            background: #1e1e35;
            border-radius: 8px;
        }}
        .module-info h3 {{
            margin: 0 0 10px 0;
            color: #8888cc;
            font-size: 14px;
            text-transform: uppercase;
            letter-spacing: 1px;
        }}
        .param-legend {{
            display: flex;
            flex-wrap: wrap;
            gap: 15px;
        }}
        .param-legend-item {{
            font-size: 12px;
            color: #aaa;
        }}
        .param-legend-item strong {{
            color: #fff;
        }}
        .grid {{
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
            gap: 20px;
        }}
        .item {{
            background: #1e1e35;
            padding: 15px;
            border-radius: 10px;
            border: 1px solid #333;
            transition: transform 0.2s, box-shadow 0.2s;
        }}
        .item:hover {{
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(0,0,0,0.4);
        }}
        .item-number {{
            font-size: 11px;
            color: #666;
            margin-bottom: 8px;
            font-weight: 500;
        }}
        .item img {{
            width: 100%;
            border-radius: 6px;
            border: 1px solid #444;
        }}
        .item audio {{
            width: 100%;
            margin-top: 12px;
            border-radius: 20px;
        }}
        .audio-container {{
            margin-top: 12px;
            position: relative;
        }}
        .audio-placeholder {{
            width: 100%;
            height: 40px;
            background: linear-gradient(135deg, #2a2a4a, #1e1e35);
            border: 1px solid #444;
            border-radius: 20px;
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
            transition: all 0.2s;
            font-size: 12px;
            color: #888;
            gap: 8px;
        }}
        .audio-placeholder:hover {{
            background: linear-gradient(135deg, #3a3a5a, #2e2e45);
            color: #aaf;
            border-color: #666;
        }}
        .audio-placeholder.loading {{
            cursor: wait;
            color: #aaf;
        }}
        .audio-placeholder.loaded {{
            display: none;
        }}
        .audio-placeholder .play-icon {{
            font-size: 16px;
        }}
        .audio-placeholder .spinner {{
            width: 16px;
            height: 16px;
            border: 2px solid #444;
            border-top-color: #aaf;
            border-radius: 50%;
            animation: spin 0.8s linear infinite;
            display: none;
        }}
        .audio-placeholder.loading .spinner {{
            display: block;
        }}
        .audio-placeholder.loading .play-icon {{
            display: none;
        }}
        @keyframes spin {{
            to {{ transform: rotate(360deg); }}
        }}
        .audio-wrapper {{
            display: none;
        }}
        .audio-wrapper.loaded {{
            display: block;
        }}
        .params-container {{
            margin-top: 12px;
            padding: 10px;
            background: #151528;
            border-radius: 6px;
        }}
        .param-row {{
            display: flex;
            align-items: center;
            margin-bottom: 6px;
            font-size: 11px;
        }}
        .param-row:last-child {{
            margin-bottom: 0;
        }}
        .param-name {{
            width: 80px;
            color: #888;
            flex-shrink: 0;
            text-transform: lowercase;
        }}
        .param-bar-container {{
            flex: 1;
            height: 8px;
            background: #252540;
            border-radius: 4px;
            margin: 0 10px;
            position: relative;
            overflow: visible;
        }}
        .param-bar {{
            height: 100%;
            background: linear-gradient(90deg, #4a4a8a, #7a7aff);
            border-radius: 4px;
            transition: width 0.3s;
        }}
        .param-marker {{
            position: absolute;
            top: -2px;
            width: 4px;
            height: 12px;
            background: #fff;
            border-radius: 2px;
            transform: translateX(-50%);
            box-shadow: 0 0 4px rgba(255,255,255,0.5);
        }}
        .param-value {{
            width: 50px;
            text-align: right;
            color: #aaf;
            font-family: 'Monaco', 'Consolas', monospace;
            flex-shrink: 0;
        }}
        .summary {{
            background: linear-gradient(135deg, #2a2a4a, #1e1e35);
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 30px;
            border: 1px solid #333;
        }}
        .summary strong {{
            color: #aaf;
        }}
        .error {{
            color: #ff6b6b;
            background: #3a1a1a;
        }}
        .toc {{
            background: #252540;
            padding: 15px 20px;
            border-radius: 8px;
            margin-bottom: 20px;
        }}
        .toc h3 {{
            margin: 0 0 10px 0;
            color: #888;
            font-size: 12px;
            text-transform: uppercase;
        }}
        .toc a {{
            color: #aaf;
            text-decoration: none;
            margin-right: 20px;
            font-size: 14px;
        }}
        .toc a:hover {{
            text-decoration: underline;
        }}
    </style>
</head>
<body>
    <h1>Faust Module Audio Test Report</h1>
    <div class="summary">
        <strong>Generated:</strong> {timestamp}<br>
        <strong>Modules tested:</strong> {len(module_results)}<br>
        <strong>Total renders:</strong> {total_renders}
    </div>
    <div class="toc">
        <h3>Jump to Module</h3>
        {"".join(f'<a href="#{name}">{name}</a>' for name in sorted(module_results.keys()))}
    </div>
"""

    for module_name, results in sorted(module_results.items()):
        # Get parameter metadata for this module
        module_params = param_metadata.get(module_name, [])
        param_ranges = {p["name"]: (p["min"], p["max"]) for p in module_params}

        # Sort results by parameter values
        sorted_results = sort_results_by_params(results)

        html += f'<div class="module" id="{module_name}">\n'
        html += f"<h2>{module_name}</h2>\n"

        # Parameter legend
        if module_params:
            html += '<div class="module-info">\n'
            html += '<h3>Parameter Ranges</h3>\n'
            html += '<div class="param-legend">\n'
            for p in module_params:
                # Skip gate/trigger/velocity params
                if p["name"].lower() in ["gate", "trigger", "velocity", "volts", "freq", "pitch"]:
                    continue
                html += f'<div class="param-legend-item"><strong>{p["name"]}</strong>: {p["min"]:.2f} → {p["max"]:.2f}</div>\n'
            html += '</div>\n</div>\n'

        html += f"<p style='color: #888; margin-bottom: 15px;'>Rendered {len(results)} parameter combinations</p>\n"
        html += '<div class="grid">\n'

        for idx, result in enumerate(sorted_results, 1):
            spec_path = result.get("spectrogram")
            wav_path = result.get("wav")
            params = result.get("params", {})
            success = result.get("success", False)

            if spec_path and Path(spec_path).exists():
                rel_spec = Path(spec_path).name

                # Copy WAV file to audio directory if it exists
                audio_element = ""
                if wav_path and Path(wav_path).exists():
                    wav_name = Path(wav_path).name
                    dest_wav = audio_dir / wav_name
                    if not dest_wav.exists():
                        shutil.copy(wav_path, dest_wav)
                    # Lazy loading audio with placeholder
                    audio_element = f'''
                    <div class="audio-container">
                        <div class="audio-placeholder" onclick="loadAudio(this)" data-src="audio/{wav_name}">
                            <span class="play-icon">&#9658;</span>
                            <div class="spinner"></div>
                            <span class="status-text">Click to play</span>
                        </div>
                        <div class="audio-wrapper">
                            <audio controls></audio>
                        </div>
                    </div>'''

                # Generate parameter bars
                param_bars = ""
                for param_name in sorted(params.keys()):
                    # Skip gate/trigger/velocity params in display
                    if param_name.lower() in ["gate", "trigger", "velocity", "volts", "freq", "pitch"]:
                        continue
                    value = params[param_name]
                    min_val, max_val = param_ranges.get(param_name, (0, 1))
                    param_bars += generate_param_bar_html(param_name, value, min_val, max_val)

                html += f'''
                <div class="item">
                    <div class="item-number">#{idx}</div>
                    <a href="spectrograms/{rel_spec}" target="_blank">
                        <img src="spectrograms/{rel_spec}" alt="{module_name}">
                    </a>
                    {audio_element}
                    <div class="params-container">
                        {param_bars}
                    </div>
                </div>
'''
            elif not success:
                html += f'''
                <div class="item error">
                    <div class="item-number">#{idx}</div>
                    <p>Render failed</p>
                </div>
'''

        html += "</div>\n</div>\n"

    # Add JavaScript for lazy loading audio
    html += """
    <script>
    function loadAudio(button) {
        const container = button.parentElement;
        const audioWrapper = container.querySelector('.audio-wrapper');
        const audio = audioWrapper.querySelector('audio');
        const src = button.dataset.src;

        // Show loading state
        button.classList.add('loading');
        button.querySelector('.status-text').textContent = 'Loading...';

        // Set up event listeners before setting src
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

        // Start loading
        audio.src = src;
        audio.load();
    }

    // Optional: Preload audio when item becomes visible
    const observer = new IntersectionObserver((entries) => {
        entries.forEach(entry => {
            if (entry.isIntersecting) {
                const placeholder = entry.target.querySelector('.audio-placeholder:not(.loading):not(.loaded)');
                if (placeholder) {
                    // Mark as observed but don't auto-load (user must click)
                    placeholder.dataset.visible = 'true';
                }
            }
        });
    }, { rootMargin: '200px' });

    // Observe all items
    document.querySelectorAll('.item').forEach(item => observer.observe(item));
    </script>
"""

    html += "</body></html>"

    with open(report_path, "w") as f:
        f.write(html)

    return report_path


def run_module_tests(
    module_name: str,
    output_dir: Path,
    num_values: int = PARAM_VALUES_FULL,
    max_combinations: int = MAX_COMBINATIONS,
    num_workers: int = NUM_WORKERS,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Run tests for a single module. Returns (results, param_metadata)."""
    print(f"\n{'=' * 60}")
    print(f"Testing module: {module_name}")
    print("=" * 60)

    # Get parameters
    params = get_module_params(module_name)
    if not params:
        print(f"  Warning: Could not get parameters for {module_name}")
        return [], []

    print(f"  Parameters: {len(params)}")
    for p in params:
        print(f"    - {p['name']}: [{p['min']}, {p['max']}] (init={p['init']})")

    # Generate parameter grid
    grid = generate_param_grid(params, num_values, max_combinations)
    print(f"  Testing {len(grid)} parameter combinations ({num_workers} workers)")

    # Create output directories
    wav_dir = output_dir / "wav" / module_name
    spec_dir = output_dir / "spectrograms"
    wav_dir.mkdir(parents=True, exist_ok=True)
    spec_dir.mkdir(parents=True, exist_ok=True)

    # Prepare work items
    work_items = [
        (module_name, param_combo, wav_dir, spec_dir, DURATION, SAMPLE_RATE)
        for param_combo in grid
    ]

    # Run in parallel
    results = []
    completed = 0
    failed = 0

    with ProcessPoolExecutor(max_workers=num_workers) as executor:
        futures = {executor.submit(render_single_test, item): i for i, item in enumerate(work_items)}

        for future in as_completed(futures):
            completed += 1
            try:
                result = future.result()
                results.append(result)
                if result["success"]:
                    status = "OK"
                else:
                    status = "FAILED"
                    failed += 1
            except Exception as e:
                results.append({"params": work_items[futures[future]][1], "success": False})
                status = f"ERROR: {e}"
                failed += 1

            # Progress update every 10 items or at the end
            if completed % 10 == 0 or completed == len(grid):
                print(f"  Progress: {completed}/{len(grid)} ({failed} failed)", flush=True)

    print(f"  Completed: {len(results)} renders ({failed} failed)")
    return results, params


def main():
    parser = argparse.ArgumentParser(description="Run Faust module audio tests")
    parser.add_argument("--module", help="Test specific module only")
    parser.add_argument(
        "--quick", action="store_true", help="Quick mode (fewer parameter values)"
    )
    parser.add_argument(
        "--output", default="test/output", help="Output directory"
    )
    parser.add_argument(
        "--report-only", action="store_true", help="Generate report from existing results"
    )
    parser.add_argument(
        "--clean", action="store_true", help="Clean output directory before running"
    )
    parser.add_argument(
        "--max-combinations",
        type=int,
        default=MAX_COMBINATIONS,
        help="Maximum combinations per module",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=NUM_WORKERS,
        help=f"Number of parallel workers (default: {NUM_WORKERS})",
    )

    args = parser.parse_args()

    project_root = get_project_root()
    output_dir = project_root / args.output

    # Check renderer exists
    if not get_render_executable().exists():
        print("Error: faust_render not found. Build with 'just build' first.")
        sys.exit(1)

    # Clean if requested
    if args.clean and output_dir.exists():
        print(f"Cleaning {output_dir}...")
        shutil.rmtree(output_dir)

    output_dir.mkdir(parents=True, exist_ok=True)

    # Get modules to test
    if args.module:
        modules = [args.module]
    else:
        modules = get_modules()
        if not modules:
            print("Error: Could not get module list")
            sys.exit(1)

    print(f"Testing {len(modules)} module(s)")
    print(f"Output directory: {output_dir}")

    num_values = PARAM_VALUES_QUICK if args.quick else PARAM_VALUES_FULL
    print(f"Parameter values: {num_values} per parameter")
    print(f"Workers: {args.workers}")

    # Run tests
    all_results = {}
    all_param_metadata = {}
    for module in modules:
        results, param_metadata = run_module_tests(
            module, output_dir, num_values=num_values,
            max_combinations=args.max_combinations,
            num_workers=args.workers
        )
        all_results[module] = results
        all_param_metadata[module] = param_metadata

    # Generate report
    print("\n" + "=" * 60)
    print("Generating HTML report...")
    report_path = generate_html_report(output_dir, all_results, all_param_metadata)
    print(f"Report: {report_path}")

    # Summary
    total = sum(len(r) for r in all_results.values())
    successful = sum(1 for r in all_results.values() for x in r if x.get("success"))
    print(f"\nSummary: {successful}/{total} renders successful")


if __name__ == "__main__":
    main()
