#!/usr/bin/env python3
"""
AI-Powered Audio Analysis using Gemini and CLAP

Combines:
1. Gemini - Subjective analysis, detailed feedback, musicality assessment
2. CLAP - Objective similarity scoring against quality descriptors

Requirements:
    pip install google-generativeai transformers torch librosa

Configuration:
    Create a .env file in the project root (or copy from .env.example):

        GEMINI_API_KEY=your_api_key_here
        GEMINI_MODEL=models/gemini-3-pro-preview

    IMPORTANT: Model name MUST use full path with 'models/' prefix!
    Recommended: models/gemini-3-pro-preview
    Alternatives: models/gemini-2.5-flash, models/gemini-2.5-pro

Multi-Channel Audio:
    This script automatically converts multi-channel WAV files (e.g., TR808's
    13-channel output) to stereo before uploading to Gemini. The Gemini API
    does not support audio with more than 2 channels.

Usage:
    python ai_audio_analysis.py --module ChaosFlute -v
    python ai_audio_analysis.py --module ChaosFlute --clap-only  # Skip Gemini
    python ai_audio_analysis.py --all --instruments-only
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import threading
from dataclasses import dataclass, field
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

# Check for dependencies
HAS_GEMINI = False
HAS_CLAP = False

try:
    import google.generativeai as genai
    HAS_GEMINI = True
except ImportError:
    pass

try:
    import torch
    from transformers import ClapModel, ClapProcessor
    import librosa
    HAS_CLAP = True
except ImportError:
    pass

# Configuration
SAMPLE_RATE = 48000
TEST_DURATION = 4.0
# IMPORTANT: Model name MUST include 'models/' prefix (e.g., 'models/gemini-3-pro-preview')
# Without prefix, API returns 404. Set via GEMINI_MODEL env var or .env file.
GEMINI_MODEL = os.environ.get("GEMINI_MODEL", "models/gemini-3-pro-preview")

# CLAP quality descriptors - positive and negative
CLAP_POSITIVE_DESCRIPTORS = [
    "clean synthesizer sound",
    "warm analog synthesizer",
    "musical and pleasant tone",
    "smooth digital synthesis",
    "rich harmonic content",
    "professional quality audio",
    "well-designed synthesizer patch",
]

CLAP_NEGATIVE_DESCRIPTORS = [
    "harsh digital distortion",
    "aliasing artifacts",
    "unpleasant metallic sound",
    "noisy and harsh",
    "clicking and popping",
    "broken audio with glitches",
    "low quality digital sound",
]

CLAP_CHARACTER_DESCRIPTORS = {
    "warm": "warm and smooth analog sound",
    "bright": "bright and crisp high frequencies",
    "dark": "dark and muted tone",
    "aggressive": "aggressive and distorted sound",
    "soft": "soft and gentle synthesizer pad",
    "percussive": "percussive and punchy attack",
    "sustained": "long sustained drone or pad",
    "metallic": "metallic bell-like resonance",
    "organic": "organic and natural sounding",
    "synthetic": "obviously synthetic electronic sound",
}

GEMINI_PROMPT_TEMPLATE = """You are an expert audio engineer and synthesizer designer analyzing a digital synthesizer module for a modular synthesis environment (VCV Rack).

## Module Information
**Name:** {module_name}
**Description:** {module_description}

## DSP Implementation
The following is the Faust DSP code that generates this sound:
```faust
{dsp_code}
```

## Showcase Audio Context
{showcase_context}

## Analysis Instructions
Listen carefully to this audio sample and provide a detailed analysis.
The audio contains the notes and parameter automations described above - use this information to understand what's happening at each point in the recording.

IMPORTANT: Evaluate the sound based on what the module is INTENDED to do, not against a generic standard.
For example, if it's a "chaos" module, harsh textures may be intentional. If it's a "physical model",
judge it against other physical models, not generic synthesizers.

Focus on:

### Sound Quality Issues (Technical)
- Is there any audible aliasing (harsh, metallic artifacts especially in high frequencies)?
- Is there any digital distortion that sounds UNINTENTIONAL (not the designed sound)?
- Are there any clicks, pops, or discontinuities at envelope transitions?
- Is there any unwanted noise floor or hiss?
- Are there any DC offset issues (thumps at start/end, waveform not centered)?
- Is the output level appropriate (not too quiet or clipping)?

### Does it Achieve its Artistic Intent?
- Based on the description and DSP code, does the sound match what the module is trying to do?
- If it's a physical model, does it sound organic and physically plausible?
- If it has "chaos" or "experimental" in its name/description, is the chaos musically interesting?
- Does the sound have character that distinguishes it from generic patches?

### Envelope & Dynamics
- How does the attack sound? (natural, clicky, appropriate for the instrument type)
- How does the decay/release behave? (realistic, appropriate sustain)
- Is there good dynamic range and response to parameters?

### Overall Assessment
- Rate the TECHNICAL sound quality from 1-10 (ignoring artistic choices)
- Rate how well it achieves its ARTISTIC INTENT from 1-10
- List any specific technical issues that should be fixed
- Suggest any improvements that preserve the artistic intent

Please be honest and critical - this feedback will be used to improve the synthesizer.
Format your response as structured sections with clear headings."""


def get_dsp_file_for_module(module_name: str) -> Path | None:
    """Find the .dsp file for a module."""
    project_root = get_project_root()
    # Try various naming conventions
    patterns = [
        project_root / "src" / "modules" / module_name / f"{module_name.lower()}.dsp",
        project_root / "src" / "modules" / module_name / f"{camel_to_snake(module_name)}.dsp",
    ]
    for pattern in patterns:
        if pattern.exists():
            return pattern
    # Search for any .dsp file in the module directory
    module_dir = project_root / "src" / "modules" / module_name
    if module_dir.exists():
        dsp_files = list(module_dir.glob("*.dsp"))
        if dsp_files:
            return dsp_files[0]
    return None


def camel_to_snake(name: str) -> str:
    """Convert CamelCase to snake_case."""
    import re
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()


def extract_module_description(dsp_code: str) -> str:
    """Extract module description from DSP comments."""
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


@dataclass
class CLAPScores:
    """CLAP embedding similarity scores."""
    positive_score: float  # Similarity to positive descriptors (0-1)
    negative_score: float  # Similarity to negative descriptors (0-1)
    quality_score: float   # positive - negative, normalized to 0-100
    character_scores: dict[str, float] = field(default_factory=dict)
    top_positive: list[tuple[str, float]] = field(default_factory=list)
    top_negative: list[tuple[str, float]] = field(default_factory=list)


@dataclass
class AIAnalysisResult:
    """Combined result from AI audio analysis."""
    module_name: str
    # Gemini results
    gemini_quality_score: int | None = None
    gemini_musical_score: int | None = None
    gemini_analysis: str = ""
    gemini_issues: list[str] = field(default_factory=list)
    gemini_suggestions: list[str] = field(default_factory=list)
    # CLAP results
    clap_scores: CLAPScores | None = None
    # Combined
    combined_quality_score: float | None = None


# Global CLAP model (loaded once, thread-safe)
_clap_model = None
_clap_processor = None
_clap_lock = threading.Lock()


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
    """Get list of available modules."""
    success, output = run_faust_render(["--list-modules"])
    if not success:
        return []
    return [line.strip() for line in output.strip().split("\n")
            if line.strip() and not line.startswith("Available")]


def load_module_config(module_name: str) -> dict:
    """Load test configuration for a module from its test_config.json file."""
    project_root = get_project_root()
    config_path = project_root / "src" / "modules" / module_name / "test_config.json"

    if not config_path.exists():
        return {"first_scenario": None}

    try:
        with open(config_path) as f:
            data = json.load(f)

        # Get first scenario for audio rendering
        first_scenario = None
        if "test_scenarios" in data and data["test_scenarios"]:
            first_scenario = data["test_scenarios"][0].get("name")

        return {"first_scenario": first_scenario}
    except (json.JSONDecodeError, IOError):
        return {"first_scenario": None}


def render_audio(module_name: str, output_path: Path,
                 params: dict[str, float] | None = None,
                 scenario: str | None = None) -> bool:
    """Render audio from a module."""
    args = [
        "--module", module_name,
        "--output", str(output_path),
        "--duration", str(TEST_DURATION),
        "--sample-rate", str(SAMPLE_RATE),
    ]
    if params:
        for name, value in params.items():
            args.extend(["--param", f"{name}={value}"])
    if scenario:
        args.extend(["--scenario", scenario])

    success, _ = run_faust_render(args)
    return success


def load_clap_model(verbose: bool = True):
    """Load CLAP model (cached globally, thread-safe)."""
    global _clap_model, _clap_processor

    # Fast path - already loaded
    if _clap_model is not None:
        return _clap_model, _clap_processor

    if not HAS_CLAP:
        return None, None

    # Thread-safe loading
    with _clap_lock:
        # Double-check after acquiring lock
        if _clap_model is not None:
            return _clap_model, _clap_processor

        if verbose:
            print("  Loading CLAP model (first time only)...")
        try:
            _clap_processor = ClapProcessor.from_pretrained("laion/clap-htsat-unfused")
            _clap_model = ClapModel.from_pretrained("laion/clap-htsat-unfused")
            _clap_model.eval()

            # Move to GPU if available
            if torch.cuda.is_available():
                _clap_model = _clap_model.cuda()

            return _clap_model, _clap_processor
        except Exception as e:
            if verbose:
                print(f"  Warning: Failed to load CLAP model: {e}")
            return None, None


def analyze_with_clap(audio_path: Path, verbose: bool = True) -> CLAPScores | None:
    """Analyze audio using CLAP embeddings (thread-safe)."""
    model, processor = load_clap_model(verbose=verbose)
    if model is None:
        return None

    try:
        # Load audio (can be done outside lock)
        audio, sr = librosa.load(str(audio_path), sr=48000, mono=True)

        # Truncate/pad to reasonable length for CLAP (10 seconds max)
        max_samples = 48000 * 10
        if len(audio) > max_samples:
            audio = audio[:max_samples]

        # Model inference must be serialized (PyTorch not thread-safe)
        with _clap_lock:
            # Get audio embedding
            audio_inputs = processor(
                audios=[audio],
                sampling_rate=48000,
                return_tensors="pt"
            )

            if torch.cuda.is_available():
                audio_inputs = {k: v.cuda() for k, v in audio_inputs.items()}

            with torch.no_grad():
                audio_embed = model.get_audio_features(**audio_inputs)
                audio_embed = audio_embed / audio_embed.norm(dim=-1, keepdim=True)

            # Get text embeddings for all descriptors
            all_texts = CLAP_POSITIVE_DESCRIPTORS + CLAP_NEGATIVE_DESCRIPTORS + list(CLAP_CHARACTER_DESCRIPTORS.values())

            text_inputs = processor(
                text=all_texts,
                return_tensors="pt",
                padding=True
            )

            if torch.cuda.is_available():
                text_inputs = {k: v.cuda() for k, v in text_inputs.items()}

            with torch.no_grad():
                text_embeds = model.get_text_features(**text_inputs)
                text_embeds = text_embeds / text_embeds.norm(dim=-1, keepdim=True)

            # Compute similarities
            similarities = (audio_embed @ text_embeds.T).squeeze().cpu().numpy()

        # Rest of processing can be done outside lock
        n_pos = len(CLAP_POSITIVE_DESCRIPTORS)
        n_neg = len(CLAP_NEGATIVE_DESCRIPTORS)

        pos_sims = similarities[:n_pos]
        neg_sims = similarities[n_pos:n_pos + n_neg]
        char_sims = similarities[n_pos + n_neg:]

        # Compute scores
        positive_score = float(np.mean(pos_sims))
        negative_score = float(np.mean(neg_sims))

        # Quality score: positive - negative, scaled to 0-100
        raw_quality = positive_score - negative_score
        quality_score = float(np.clip((raw_quality + 0.3) / 0.6 * 100, 0, 100))

        # Character scores
        character_scores = {}
        char_names = list(CLAP_CHARACTER_DESCRIPTORS.keys())
        for i, name in enumerate(char_names):
            character_scores[name] = float(char_sims[i])

        # Top matches
        pos_sorted = sorted(zip(CLAP_POSITIVE_DESCRIPTORS, pos_sims),
                           key=lambda x: x[1], reverse=True)
        neg_sorted = sorted(zip(CLAP_NEGATIVE_DESCRIPTORS, neg_sims),
                           key=lambda x: x[1], reverse=True)

        return CLAPScores(
            positive_score=positive_score,
            negative_score=negative_score,
            quality_score=quality_score,
            character_scores=character_scores,
            top_positive=[(d, float(s)) for d, s in pos_sorted[:3]],
            top_negative=[(d, float(s)) for d, s in neg_sorted[:3]]
        )

    except Exception as e:
        print(f"  Warning: CLAP analysis failed: {e}")
        return None


def analyze_with_clap_batch(audio_paths: list[Path], batch_size: int = 8) -> list[CLAPScores | None]:
    """Analyze multiple audio files using CLAP embeddings in batches (thread-safe).

    This is more efficient than calling analyze_with_clap() repeatedly because:
    1. Audio loading is parallelized outside the lock
    2. Model inference is batched, reducing lock contention
    3. Text embeddings are computed once and reused

    Args:
        audio_paths: List of paths to audio files
        batch_size: Number of audio files to process in each batch

    Returns:
        List of CLAPScores (same order as input), None for failures
    """
    model, processor = load_clap_model()
    if model is None:
        return [None] * len(audio_paths)

    results = [None] * len(audio_paths)

    # Pre-load all audio files in parallel (outside lock)
    from concurrent.futures import ThreadPoolExecutor

    def load_audio_file(idx_path):
        idx, path = idx_path
        try:
            audio, sr = librosa.load(str(path), sr=48000, mono=True)
            # Truncate/pad to reasonable length for CLAP (10 seconds max)
            max_samples = 48000 * 10
            if len(audio) > max_samples:
                audio = audio[:max_samples]
            return idx, audio
        except Exception as e:
            return idx, None

    audio_data = {}
    with ThreadPoolExecutor(max_workers=8) as executor:
        for idx, audio in executor.map(load_audio_file, enumerate(audio_paths)):
            if audio is not None:
                audio_data[idx] = audio

    if not audio_data:
        return results

    # Process in batches under the lock
    try:
        with _clap_lock:
            # Get text embeddings once (shared across all batches)
            all_texts = CLAP_POSITIVE_DESCRIPTORS + CLAP_NEGATIVE_DESCRIPTORS + list(CLAP_CHARACTER_DESCRIPTORS.values())
            text_inputs = processor(text=all_texts, return_tensors="pt", padding=True)

            if torch.cuda.is_available():
                text_inputs = {k: v.cuda() for k, v in text_inputs.items()}

            with torch.no_grad():
                text_embeds = model.get_text_features(**text_inputs)
                text_embeds = text_embeds / text_embeds.norm(dim=-1, keepdim=True)

            # Process audio in batches
            indices = list(audio_data.keys())
            for batch_start in range(0, len(indices), batch_size):
                batch_indices = indices[batch_start:batch_start + batch_size]
                batch_audios = [audio_data[i] for i in batch_indices]

                # Get audio embeddings for batch
                audio_inputs = processor(
                    audios=batch_audios,
                    sampling_rate=48000,
                    return_tensors="pt"
                )

                if torch.cuda.is_available():
                    audio_inputs = {k: v.cuda() for k, v in audio_inputs.items()}

                with torch.no_grad():
                    audio_embeds = model.get_audio_features(**audio_inputs)
                    audio_embeds = audio_embeds / audio_embeds.norm(dim=-1, keepdim=True)

                # Compute similarities for each audio in batch
                for batch_idx, orig_idx in enumerate(batch_indices):
                    audio_embed = audio_embeds[batch_idx:batch_idx+1]
                    similarities = (audio_embed @ text_embeds.T).squeeze().cpu().numpy()

                    # Compute scores
                    n_pos = len(CLAP_POSITIVE_DESCRIPTORS)
                    n_neg = len(CLAP_NEGATIVE_DESCRIPTORS)

                    pos_sims = similarities[:n_pos]
                    neg_sims = similarities[n_pos:n_pos + n_neg]
                    char_sims = similarities[n_pos + n_neg:]

                    positive_score = float(np.mean(pos_sims))
                    negative_score = float(np.mean(neg_sims))
                    raw_quality = positive_score - negative_score
                    quality_score = float(np.clip((raw_quality + 0.3) / 0.6 * 100, 0, 100))

                    character_scores = {}
                    char_names = list(CLAP_CHARACTER_DESCRIPTORS.keys())
                    for i, name in enumerate(char_names):
                        character_scores[name] = float(char_sims[i])

                    pos_sorted = sorted(zip(CLAP_POSITIVE_DESCRIPTORS, pos_sims),
                                       key=lambda x: x[1], reverse=True)
                    neg_sorted = sorted(zip(CLAP_NEGATIVE_DESCRIPTORS, neg_sims),
                                       key=lambda x: x[1], reverse=True)

                    results[orig_idx] = CLAPScores(
                        positive_score=positive_score,
                        negative_score=negative_score,
                        quality_score=quality_score,
                        character_scores=character_scores,
                        top_positive=[(d, float(s)) for d, s in pos_sorted[:3]],
                        top_negative=[(d, float(s)) for d, s in neg_sorted[:3]]
                    )

    except Exception as e:
        print(f"  Warning: CLAP batch analysis failed: {e}")

    return results


def analyze_with_gemini(audio_path: Path, module_name: str,
                        showcase_context: str = "",
                        verbose: bool = False,
                        timeout: float = 120.0) -> dict[str, Any]:
    """Send audio to Gemini for analysis with DSP and showcase context.

    Args:
        audio_path: Path to WAV file
        module_name: Name of the module
        showcase_context: Description of showcase audio content
        verbose: Print detailed progress logs
        timeout: Timeout in seconds for API calls (default: 120s)

    Returns:
        Dict with analysis results or error
    """
    import signal
    import threading
    import time

    if not HAS_GEMINI:
        return {
            "error": "google-generativeai not installed. Run: pip install google-generativeai"
        }

    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        return {"error": "GEMINI_API_KEY environment variable not set"}

    # Check file size first
    file_size_mb = audio_path.stat().st_size / (1024 * 1024)
    if verbose:
        print(f"    Audio file size: {file_size_mb:.2f} MB")

    if file_size_mb > 20:
        return {"error": f"Audio file too large: {file_size_mb:.1f} MB (max 20 MB)"}

    audio_file = None
    start_time = time.time()
    upload_path = audio_path  # May be replaced with stereo version

    try:
        if verbose:
            print(f"    Configuring Gemini API...")
        genai.configure(api_key=api_key)

        # Check if audio has more than 2 channels and convert to stereo
        import wave
        try:
            with wave.open(str(audio_path), 'rb') as wav_in:
                n_channels = wav_in.getnchannels()
                if n_channels > 2:
                    if verbose:
                        print(f"    Converting {n_channels}-channel audio to stereo...")
                    sample_width = wav_in.getsampwidth()
                    frame_rate = wav_in.getframerate()
                    n_frames = wav_in.getnframes()
                    frames = wav_in.readframes(n_frames)

                    # Parse audio data
                    if sample_width == 2:
                        samples = np.frombuffer(frames, dtype=np.int16).reshape(-1, n_channels)
                    else:
                        samples = np.frombuffer(frames, dtype=np.float32).reshape(-1, n_channels)

                    # Take first 2 channels (stereo mix)
                    stereo = samples[:, :2]

                    # Write to temp file
                    stereo_path = Path(tempfile.gettempdir()) / f"{module_name}_stereo.wav"
                    with wave.open(str(stereo_path), 'wb') as wav_out:
                        wav_out.setnchannels(2)
                        wav_out.setsampwidth(sample_width)
                        wav_out.setframerate(frame_rate)
                        wav_out.writeframes(stereo.tobytes())
                    upload_path = stereo_path
                    if verbose:
                        print(f"    Created stereo file: {stereo_path}")
        except Exception as e:
            if verbose:
                print(f"    Warning: Could not check/convert channels: {e}")

        # Load DSP code if available
        dsp_code = ""
        module_description = "No description available"
        dsp_file = get_dsp_file_for_module(module_name)
        if dsp_file:
            if verbose:
                print(f"    Loading DSP code from {dsp_file.name}...")
            with open(dsp_file) as f:
                dsp_code = f.read()
            module_description = extract_module_description(dsp_code)
            if verbose:
                print(f"    DSP code: {len(dsp_code)} chars")

        # Upload audio to Gemini with progress
        if verbose:
            print(f"    Uploading audio to Gemini ({file_size_mb:.2f} MB)...")
        upload_start = time.time()

        try:
            audio_file = genai.upload_file(upload_path, mime_type="audio/wav")
            upload_time = time.time() - upload_start
            if verbose:
                print(f"    Upload complete in {upload_time:.1f}s")
        except Exception as e:
            return {"error": f"Failed to upload audio: {e}"}

        # Check if we've exceeded timeout during upload
        elapsed = time.time() - start_time
        if elapsed > timeout:
            if audio_file:
                try:
                    audio_file.delete()
                except Exception:
                    pass
            return {"error": f"Timeout during upload ({elapsed:.1f}s > {timeout}s)"}

        # Create model and analyze
        if verbose:
            print(f"    Creating model: {GEMINI_MODEL}...")
        model = genai.GenerativeModel(GEMINI_MODEL)

        # Build prompt with DSP and showcase context
        if not showcase_context:
            showcase_context = "No showcase context provided."

        prompt = GEMINI_PROMPT_TEMPLATE.format(
            module_name=module_name,
            module_description=module_description,
            dsp_code=dsp_code if dsp_code else "DSP code not available",
            showcase_context=showcase_context
        )

        if verbose:
            print(f"    Prompt size: {len(prompt)} chars")
            print(f"    Generating content (this may take a while)...")

        # Generate with timeout using threading
        generation_start = time.time()
        remaining_timeout = max(10, timeout - (time.time() - start_time))

        result_container = {"response": None, "error": None}

        def generate():
            try:
                result_container["response"] = model.generate_content([prompt, audio_file])
            except Exception as e:
                result_container["error"] = str(e)

        thread = threading.Thread(target=generate)
        thread.start()
        thread.join(timeout=remaining_timeout)

        if thread.is_alive():
            # Thread is still running - timeout occurred
            generation_time = time.time() - generation_start
            if verbose:
                print(f"    WARNING: Generation timed out after {generation_time:.1f}s")
            # Clean up the uploaded file
            if audio_file:
                try:
                    audio_file.delete()
                except Exception:
                    pass
            return {"error": f"Generation timed out after {generation_time:.1f}s"}

        generation_time = time.time() - generation_start
        if verbose:
            print(f"    Generation complete in {generation_time:.1f}s")

        if result_container["error"]:
            return {"error": result_container["error"]}

        response = result_container["response"]
        if response is None:
            return {"error": "No response received from Gemini"}

        try:
            analysis_text = response.text
        except Exception as e:
            return {"error": f"Failed to get response text: {e}"}

        if verbose:
            print(f"    Response: {len(analysis_text)} chars")

        # Parse scores from response
        import re

        quality_score = None
        musical_score = None
        issues = []
        suggestions = []

        # Look for technical quality score
        quality_match = re.search(
            r'(?:technical|sound)[^:]*quality[^\d]*[:\s]+(\d+)\s*(?:/\s*10|out of 10)?',
            analysis_text, re.IGNORECASE
        )
        if quality_match:
            quality_score = int(quality_match.group(1))

        # Look for artistic intent score (new)
        intent_match = re.search(
            r'(?:artistic|intent)[^\d]*[:\s]+(\d+)\s*(?:/\s*10|out of 10)?',
            analysis_text, re.IGNORECASE
        )
        if intent_match:
            musical_score = int(intent_match.group(1))

        # Fallback: look for musical/usability score
        if musical_score is None:
            musical_match = re.search(
                r'(?:musical|interest|usability)[^\d]*(\d+)\s*(?:/\s*10|out of 10)?',
                analysis_text, re.IGNORECASE
            )
            if musical_match:
                musical_score = int(musical_match.group(1))

        # Extract issues
        issues_section = re.search(
            r'(?:issues|problems|fix)[^\n]*\n((?:[-•*][^\n]+\n?)+)',
            analysis_text, re.IGNORECASE
        )
        if issues_section:
            issues = [line.strip().lstrip('-•* ') for line in issues_section.group(1).split('\n')
                     if line.strip()][:5]

        # Extract suggestions
        suggestions_section = re.search(
            r'(?:suggestions|improvements|recommend)[^\n]*\n((?:[-•*][^\n]+\n?)+)',
            analysis_text, re.IGNORECASE
        )
        if suggestions_section:
            suggestions = [line.strip().lstrip('-•* ') for line in suggestions_section.group(1).split('\n')
                          if line.strip()][:5]

        # Cleanup
        if audio_file:
            try:
                if verbose:
                    print(f"    Cleaning up uploaded file...")
                audio_file.delete()
            except Exception as e:
                if verbose:
                    print(f"    Warning: Failed to delete uploaded file: {e}")

        total_time = time.time() - start_time
        if verbose:
            print(f"    Total Gemini analysis time: {total_time:.1f}s")

        return {
            "analysis": analysis_text,
            "quality_score": quality_score,
            "musical_score": musical_score,
            "issues": issues,
            "suggestions": suggestions
        }

    except Exception as e:
        # Cleanup on error
        if audio_file:
            try:
                audio_file.delete()
            except Exception:
                pass
        import traceback
        error_msg = f"{type(e).__name__}: {e}"
        if verbose:
            print(f"    ERROR: {error_msg}")
            traceback.print_exc()
        return {"error": error_msg}


def analyze_module(module_name: str, wav_path: Path,
                   use_gemini: bool = True, use_clap: bool = True,
                   showcase_context: str = "",
                   verbose: bool = False,
                   gemini_timeout: float = 120.0) -> AIAnalysisResult:
    """Run full AI analysis on a module.

    Args:
        module_name: Name of the module
        wav_path: Path to audio file
        use_gemini: Whether to run Gemini analysis
        use_clap: Whether to run CLAP analysis
        showcase_context: Description of showcase audio content
        verbose: Print detailed logs
        gemini_timeout: Timeout for Gemini API calls (default: 120s)

    Returns:
        AIAnalysisResult with all analysis data
    """
    result = AIAnalysisResult(module_name=module_name)

    # CLAP analysis
    if use_clap and HAS_CLAP:
        if verbose:
            print("  Running CLAP analysis...")
        result.clap_scores = analyze_with_clap(wav_path, verbose=verbose)

    # Gemini analysis
    if use_gemini and HAS_GEMINI and os.environ.get("GEMINI_API_KEY"):
        if verbose:
            print("  Running Gemini analysis...")
        gemini_result = analyze_with_gemini(
            wav_path, module_name, showcase_context,
            verbose=verbose, timeout=gemini_timeout
        )

        if "error" not in gemini_result:
            result.gemini_analysis = gemini_result.get("analysis", "")
            result.gemini_quality_score = gemini_result.get("quality_score")
            result.gemini_musical_score = gemini_result.get("musical_score")
            result.gemini_issues = gemini_result.get("issues", [])
            result.gemini_suggestions = gemini_result.get("suggestions", [])
        else:
            error_msg = gemini_result['error']
            result.gemini_analysis = f"Error: {error_msg}"
            if verbose:
                print(f"    Gemini error: {error_msg}")

    # Combined score
    scores = []
    if result.clap_scores:
        scores.append(result.clap_scores.quality_score)
    if result.gemini_quality_score:
        scores.append(result.gemini_quality_score * 10)  # Scale to 0-100

    if scores:
        result.combined_quality_score = np.mean(scores)

    return result


def print_analysis(result: AIAnalysisResult, verbose: bool = False):
    """Print analysis result to console."""
    print(f"\n{'=' * 70}")
    print(f"AI AUDIO ANALYSIS: {result.module_name}")
    print('=' * 70)

    # Combined score
    if result.combined_quality_score is not None:
        score = result.combined_quality_score
        if score >= 80:
            color = "\033[92m"  # Green
        elif score >= 60:
            color = "\033[93m"  # Yellow
        else:
            color = "\033[91m"  # Red
        reset = "\033[0m"
        print(f"\nCombined Quality Score: {color}{score:.0f}/100{reset}")

    # CLAP results
    if result.clap_scores:
        clap = result.clap_scores
        print(f"\n{'-' * 70}")
        print("CLAP EMBEDDING ANALYSIS")
        print('-' * 70)
        print(f"  Quality Score: {clap.quality_score:.0f}/100")
        print(f"  Positive Similarity: {clap.positive_score:.3f}")
        print(f"  Negative Similarity: {clap.negative_score:.3f}")

        print("\n  Top Positive Matches:")
        for desc, score in clap.top_positive:
            print(f"    {score:.3f} - {desc}")

        print("\n  Top Negative Matches:")
        for desc, score in clap.top_negative:
            print(f"    {score:.3f} - {desc}")

        if verbose:
            print("\n  Character Profile:")
            # Sort by score
            sorted_chars = sorted(clap.character_scores.items(),
                                 key=lambda x: x[1], reverse=True)
            for char, score in sorted_chars[:5]:
                bar_len = int(score * 50 + 25)  # Scale for display
                bar = "█" * bar_len
                print(f"    {char:<12} {bar} {score:.3f}")

    # Gemini results
    if result.gemini_analysis and not result.gemini_analysis.startswith("Error"):
        print(f"\n{'-' * 70}")
        print("GEMINI ANALYSIS")
        print('-' * 70)

        if result.gemini_quality_score:
            print(f"  Quality Score: {result.gemini_quality_score}/10")
        if result.gemini_musical_score:
            print(f"  Musical Score: {result.gemini_musical_score}/10")

        if verbose:
            print(f"\n{result.gemini_analysis}")
        else:
            # Print condensed version
            lines = result.gemini_analysis.split('\n')
            for line in lines[:20]:
                print(f"  {line}")
            if len(lines) > 20:
                print("  ... (use -v for full analysis)")

        if result.gemini_issues:
            print("\n  Key Issues:")
            for issue in result.gemini_issues:
                print(f"    • {issue}")

        if result.gemini_suggestions:
            print("\n  Suggestions:")
            for suggestion in result.gemini_suggestions:
                print(f"    • {suggestion}")

    print()


def main():
    parser = argparse.ArgumentParser(
        description="AI-powered audio analysis using Gemini and CLAP"
    )
    parser.add_argument("--module", help="Specific module to analyze")
    parser.add_argument("--all", action="store_true",
                       help="Analyze all available modules")
    parser.add_argument("--instruments-only", action="store_true",
                       help="Only analyze instrument modules")
    parser.add_argument("--clap-only", action="store_true",
                       help="Only run CLAP analysis (no Gemini)")
    parser.add_argument("--gemini-only", action="store_true",
                       help="Only run Gemini analysis (no CLAP)")
    parser.add_argument("--json", action="store_true",
                       help="Output as JSON")
    parser.add_argument("--output", help="Output file for results")
    parser.add_argument("-v", "--verbose", action="store_true",
                       help="Verbose output")

    args = parser.parse_args()

    # Check dependencies
    if not HAS_CLAP and not HAS_GEMINI:
        print("Error: No AI backends available")
        print("Install with: pip install google-generativeai transformers torch librosa")
        sys.exit(1)

    use_gemini = HAS_GEMINI and not args.clap_only and os.environ.get("GEMINI_API_KEY")
    use_clap = HAS_CLAP and not args.gemini_only

    if not use_gemini and not use_clap:
        print("Error: No analysis method available")
        if args.gemini_only and not os.environ.get("GEMINI_API_KEY"):
            print("Set GEMINI_API_KEY environment variable")
        if args.clap_only and not HAS_CLAP:
            print("Install CLAP: pip install transformers torch librosa")
        sys.exit(1)

    # Check executable
    if not get_render_executable().exists():
        print("Error: faust_render not found. Run 'just build' first.")
        sys.exit(1)

    # Get modules
    if args.module:
        modules = [args.module]
    elif args.all or args.instruments_only:
        modules = get_modules()
        if args.instruments_only:
            instruments = ["ModalBell", "PluckedString", "ChaosFlute",
                          "SpaceCello", "TheAbyss", "Matter", "Linkage"]
            modules = [m for m in modules if m in instruments]
    else:
        print("Error: Specify --module NAME or --all")
        sys.exit(1)

    if not args.json:
        print(f"Analyzing {len(modules)} module(s)...")
        print(f"  CLAP: {'enabled' if use_clap else 'disabled'}")
        print(f"  Gemini: {'enabled' if use_gemini else 'disabled'}")

    # Analyze
    results = []

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)

        for module_name in modules:
            if not args.json:
                print(f"\n{'=' * 50}")
                print(f"Analyzing: {module_name}")
                print('=' * 50)
                print("  Rendering audio...")

            wav_path = tmp_dir / f"{module_name}_ai.wav"

            # Get first scenario if available (for trigger-based modules like drums)
            config = load_module_config(module_name)
            first_scenario = config.get("first_scenario")

            if not render_audio(module_name, wav_path, scenario=first_scenario):
                if not args.json:
                    print(f"  Error: Failed to render")
                continue

            result = analyze_module(module_name, wav_path, use_gemini, use_clap,
                                    verbose=not args.json)
            results.append(result)

            if not args.json:
                print_analysis(result, args.verbose)

    # JSON output
    if args.json or args.output:
        json_data = []
        for r in results:
            entry = {
                "module_name": r.module_name,
                "combined_score": r.combined_quality_score,
            }
            if r.clap_scores:
                entry["clap"] = {
                    "quality_score": r.clap_scores.quality_score,
                    "positive_score": r.clap_scores.positive_score,
                    "negative_score": r.clap_scores.negative_score,
                    "character": r.clap_scores.character_scores,
                    "top_positive": r.clap_scores.top_positive,
                    "top_negative": r.clap_scores.top_negative,
                }
            if r.gemini_analysis:
                entry["gemini"] = {
                    "quality_score": r.gemini_quality_score,
                    "musical_score": r.gemini_musical_score,
                    "analysis": r.gemini_analysis,
                    "issues": r.gemini_issues,
                    "suggestions": r.gemini_suggestions,
                }
            json_data.append(entry)

        json_str = json.dumps(json_data, indent=2)

        if args.output:
            with open(args.output, "w") as f:
                f.write(json_str)
            print(f"\nResults saved to: {args.output}")
        else:
            print(json_str)

    # Summary table
    if len(results) > 1 and not args.json:
        print("\n" + "=" * 70)
        print("SUMMARY")
        print("=" * 70)
        print(f"{'Module':<18} {'Combined':<10} {'CLAP':<10} {'Gemini':<10}")
        print("-" * 48)
        for r in results:
            combined = f"{r.combined_quality_score:.0f}" if r.combined_quality_score else "N/A"
            clap = f"{r.clap_scores.quality_score:.0f}" if r.clap_scores else "N/A"
            gemini = f"{r.gemini_quality_score*10:.0f}" if r.gemini_quality_score else "N/A"
            print(f"{r.module_name:<18} {combined:<10} {clap:<10} {gemini:<10}")


if __name__ == "__main__":
    main()
