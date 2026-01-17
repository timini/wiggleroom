#!/usr/bin/env python3
"""
Regenerate HTML report from existing rendered files.
Does not re-render audio - just rebuilds the HTML.
"""

import json
import re
import shutil
import subprocess
from pathlib import Path
from datetime import datetime


def get_module_params(module_name: str) -> list[dict]:
    """Get parameter metadata from faust_render."""
    exe = Path(__file__).parent.parent / "build" / "test" / "faust_render"
    if not exe.exists():
        return []

    try:
        result = subprocess.run(
            [str(exe), "--module", module_name, "--list-params"],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode != 0:
            return []

        params = []
        for line in result.stdout.strip().split("\n"):
            line = line.strip()
            if line.startswith("["):
                try:
                    bracket_end = line.index("]")
                    rest = line[bracket_end + 1:].strip()
                    path_end = rest.index("(")
                    path = rest[:path_end].strip()
                    name = path.split("/")[-1]
                    meta = rest[path_end + 1:-1]
                    min_val = float(meta.split("min=")[1].split(",")[0])
                    max_val = float(meta.split("max=")[1].split(",")[0])
                    init_val = float(meta.split("init=")[1].split(")")[0])
                    params.append({
                        "name": name, "min": min_val, "max": max_val, "init": init_val
                    })
                except (ValueError, IndexError):
                    continue
        return params
    except Exception:
        return []


def format_value(value: float) -> str:
    """Format a value nicely for display."""
    if abs(value) < 0.01 and value != 0:
        return f"{value:.3f}"
    elif abs(value) < 10:
        return f"{value:.2f}"
    elif abs(value) < 1000:
        return f"{value:.1f}"
    else:
        return f"{value:.0f}"


def generate_param_bar_html(name: str, value: float, min_val: float, max_val: float) -> str:
    """Generate HTML for a parameter visualization bar with range labels and zero marker."""
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


def main():
    output_dir = Path(__file__).parent / "output"
    wav_dir = output_dir / "wav"
    spec_dir = output_dir / "spectrograms"
    audio_dir = output_dir / "audio"
    audio_dir.mkdir(parents=True, exist_ok=True)

    if not wav_dir.exists():
        print("Error: No rendered files found. Run 'just test-audio' first.")
        return 1

    # Scan existing renders
    modules = {}
    for module_dir in wav_dir.iterdir():
        if module_dir.is_dir():
            module = module_dir.name
            modules[module] = []

            # Load hash metadata if it exists
            hash_metadata = {}
            metadata_file = module_dir / "param_metadata.json"
            if metadata_file.exists():
                try:
                    hash_metadata = json.loads(metadata_file.read_text())
                except Exception:
                    pass

            for wav_file in module_dir.glob("*.wav"):
                name = wav_file.stem
                filename = wav_file.name

                # Check if this is a hash-named file with metadata
                if filename in hash_metadata:
                    params = hash_metadata[filename]
                else:
                    # Parse params from filename: ModuleName_param1=val1_param2=val2...
                    # Use regex to handle params with underscores in names (e.g., decay_high=0.10)
                    params = {}
                    param_str = name[len(module) + 1:] if name.startswith(module + "_") else name
                    # Match param=value where param starts with a letter (not underscore separator)
                    for match in re.finditer(r'(?:^|_)([a-zA-Z][a-zA-Z0-9_]*)=(-?[0-9.]+)', param_str):
                        k, v = match.groups()
                        params[k] = float(v)

                spec_file = spec_dir / f"{name}.png"
                modules[module].append({
                    "wav": str(wav_file),
                    "spectrogram": str(spec_file) if spec_file.exists() else None,
                    "params": params
                })

    total = sum(len(r) for r in modules.values())
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    html = f'''<!DOCTYPE html>
<html><head><title>Faust Module Audio Test Report</title>
<style>
*{{box-sizing:border-box}}
body{{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;margin:0;padding:20px;background:#1a1a2e;color:#eee}}
h1{{color:#fff;font-weight:300;margin-bottom:20px}}
h2{{color:#a0a0ff;border-bottom:2px solid #333;padding-bottom:10px;font-weight:400}}
.module{{margin-bottom:40px;background:#252540;padding:25px;border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,0.3)}}
.grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:20px}}
.item{{background:#1e1e35;padding:15px;border-radius:10px;border:1px solid #333;transition:transform 0.2s,box-shadow 0.2s}}
.item:hover{{transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,0,0,0.4)}}
.item-number{{font-size:11px;color:#666;margin-bottom:8px;font-weight:500}}
.item img{{width:100%;border-radius:6px;border:1px solid #444}}
.audio-container{{margin-top:12px;position:relative}}
.audio-placeholder{{
    width:100%;height:40px;
    background:linear-gradient(135deg,#2a2a4a,#1e1e35);
    border:1px solid #444;border-radius:20px;
    display:flex;align-items:center;justify-content:center;
    cursor:pointer;transition:all 0.2s;
    font-size:12px;color:#888;gap:8px
}}
.audio-placeholder:hover{{background:linear-gradient(135deg,#3a3a5a,#2e2e45);color:#aaf;border-color:#666}}
.audio-placeholder.loading{{cursor:wait;color:#aaf}}
.audio-placeholder.loaded{{display:none}}
.audio-placeholder .play-icon{{font-size:16px}}
.audio-placeholder .spinner{{
    width:16px;height:16px;
    border:2px solid #444;border-top-color:#aaf;
    border-radius:50%;animation:spin 0.8s linear infinite;
    display:none
}}
.audio-placeholder.loading .spinner{{display:block}}
.audio-placeholder.loading .play-icon{{display:none}}
@keyframes spin{{to{{transform:rotate(360deg)}}}}
.audio-wrapper{{display:none}}
.audio-wrapper.loaded{{display:block}}
.audio-wrapper audio{{width:100%;border-radius:20px}}
.params-container{{margin-top:12px;padding:10px;background:#151528;border-radius:6px}}
.param-row{{display:flex;align-items:center;margin-bottom:8px;font-size:11px}}
.param-row:last-child{{margin-bottom:0}}
.param-name{{width:70px;color:#888;flex-shrink:0;text-transform:lowercase}}
.param-min{{width:45px;text-align:right;color:#666;font-family:'Monaco','Consolas',monospace;flex-shrink:0;font-size:10px}}
.param-max{{width:45px;text-align:left;color:#666;font-family:'Monaco','Consolas',monospace;flex-shrink:0;font-size:10px}}
.param-bar-container{{flex:1;height:8px;background:#252540;border-radius:4px;margin:0 8px;position:relative;overflow:visible}}
.param-bar{{height:100%;background:linear-gradient(90deg,#4a4a8a,#7a7aff);border-radius:4px}}
.param-marker{{position:absolute;top:-2px;width:4px;height:12px;background:#fff;border-radius:2px;transform:translateX(-50%);box-shadow:0 0 4px rgba(255,255,255,0.5)}}
.zero-marker{{position:absolute;top:-1px;width:2px;height:10px;background:#ff8800;border-radius:1px;transform:translateX(-50%);opacity:0.8}}
.param-value{{width:55px;text-align:right;color:#aaf;font-family:'Monaco','Consolas',monospace;flex-shrink:0;font-weight:bold;padding-left:8px;border-left:1px solid #333}}
.module-info{{margin-bottom:20px;padding:15px;background:#1e1e35;border-radius:8px}}
.module-info h3{{margin:0 0 10px 0;color:#8888cc;font-size:14px;text-transform:uppercase;letter-spacing:1px}}
.param-legend{{display:flex;flex-wrap:wrap;gap:15px}}
.param-legend-item{{font-size:12px;color:#aaa}}
.param-legend-item strong{{color:#fff}}
.summary{{background:linear-gradient(135deg,#2a2a4a,#1e1e35);padding:20px;border-radius:10px;margin-bottom:30px;border:1px solid #333}}
.summary strong{{color:#aaf}}
.toc{{background:#252540;padding:15px 20px;border-radius:8px;margin-bottom:20px}}
.toc h3{{margin:0 0 10px 0;color:#888;font-size:12px;text-transform:uppercase;letter-spacing:1px}}
.toc a{{color:#aaf;text-decoration:none;margin-right:20px;font-size:14px}}
.toc a:hover{{text-decoration:underline}}
</style></head><body>
<h1>Faust Module Audio Test Report</h1>
<div class="summary">
    <strong>Generated:</strong> {ts}<br>
    <strong>Modules:</strong> {len(modules)}<br>
    <strong>Total renders:</strong> {total}
</div>
<div class="toc">
    <h3>Jump to Module</h3>
    {"".join(f'<a href="#{n}">{n}</a>' for n in sorted(modules.keys()))}
</div>
'''

    for mod, results in sorted(modules.items()):
        # Get parameter metadata for this module
        module_params = get_module_params(mod)
        param_ranges = {p["name"]: (p["min"], p["max"]) for p in module_params}

        html += f'''<div class="module" id="{mod}">
<h2>{mod}</h2>
'''
        # Parameter legend
        if module_params:
            html += '<div class="module-info"><h3>Parameter Ranges</h3><div class="param-legend">'
            for p in module_params:
                if p["name"].lower() not in ["gate", "trigger", "velocity", "volts", "freq", "pitch"]:
                    html += f'<div class="param-legend-item"><strong>{p["name"]}</strong>: {p["min"]:.2f} → {p["max"]:.2f}</div>'
            html += '</div></div>\n'

        html += f'''<p style="color:#888;margin-bottom:15px">Rendered {len(results)} combinations</p>
<div class="grid">
'''
        for idx, r in enumerate(sorted(results, key=lambda x: str(x["params"])), 1):
            if r["spectrogram"] and Path(r["spectrogram"]).exists():
                rel_spec = Path(r["spectrogram"]).name
                wav_name = Path(r["wav"]).name
                dest = audio_dir / wav_name
                if not dest.exists():
                    shutil.copy(r["wav"], dest)

                # Generate parameter bars
                param_bars = ""
                for param_name in sorted(r["params"].keys()):
                    if param_name.lower() not in ["gate", "trigger", "velocity", "volts", "freq", "pitch"]:
                        value = r["params"][param_name]
                        min_val, max_val = param_ranges.get(param_name, (0, 1))
                        param_bars += generate_param_bar_html(param_name, value, min_val, max_val)

                html += f'''<div class="item">
    <div class="item-number">#{idx}</div>
    <a href="spectrograms/{rel_spec}" target="_blank">
        <img src="spectrograms/{rel_spec}" alt="{mod}">
    </a>
    <div class="audio-container">
        <div class="audio-placeholder" onclick="loadAudio(this)" data-src="audio/{wav_name}">
            <span class="play-icon">&#9658;</span>
            <div class="spinner"></div>
            <span class="status-text">Click to play</span>
        </div>
        <div class="audio-wrapper">
            <audio controls></audio>
        </div>
    </div>
    <div class="params-container">
        {param_bars}
    </div>
</div>
'''
        html += '</div></div>\n'

    html += '''<script>
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
</script>
</body></html>'''

    report_path = output_dir / "report.html"
    report_path.write_text(html)
    print(f"Report: {report_path}")
    print(f"Modules: {len(modules)}, Renders: {total}")
    return 0


if __name__ == "__main__":
    exit(main())
