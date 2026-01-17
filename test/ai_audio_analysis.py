#!/usr/bin/env python3
"""
AI-Powered Audio Analysis using Gemini and CLAP

Combines:
1. Gemini - Subjective analysis, detailed feedback, musicality assessment
2. CLAP - Objective similarity scoring against quality descriptors

Requirements:
    pip install google-generativeai transformers torch librosa

Usage:
    export GEMINI_API_KEY=your_api_key
    python ai_audio_analysis.py --module ChaosFlute
    python ai_audio_analysis.py --module ChaosFlute --clap-only  # Skip Gemini
    python ai_audio_analysis.py --all --instruments-only
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np

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
GEMINI_MODEL = os.environ.get("GEMINI_MODEL", "gemini-3-pro-preview")  # Or set via env var

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

## Analysis Instructions
Listen carefully to this audio sample and provide a detailed analysis.

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


# Global CLAP model (loaded once)
_clap_model = None
_clap_processor = None


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


def render_audio(module_name: str, output_path: Path,
                 params: dict[str, float] | None = None) -> bool:
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

    success, _ = run_faust_render(args)
    return success


def load_clap_model():
    """Load CLAP model (cached globally)."""
    global _clap_model, _clap_processor

    if _clap_model is not None:
        return _clap_model, _clap_processor

    if not HAS_CLAP:
        return None, None

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
        print(f"  Warning: Failed to load CLAP model: {e}")
        return None, None


def analyze_with_clap(audio_path: Path) -> CLAPScores | None:
    """Analyze audio using CLAP embeddings."""
    model, processor = load_clap_model()
    if model is None:
        return None

    try:
        # Load audio
        audio, sr = librosa.load(str(audio_path), sr=48000, mono=True)

        # Truncate/pad to reasonable length for CLAP (10 seconds max)
        max_samples = 48000 * 10
        if len(audio) > max_samples:
            audio = audio[:max_samples]

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

        # Split by category
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


def analyze_with_gemini(audio_path: Path, module_name: str) -> dict[str, Any]:
    """Send audio to Gemini for analysis with DSP context."""
    if not HAS_GEMINI:
        return {
            "error": "google-generativeai not installed. Run: pip install google-generativeai"
        }

    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        return {"error": "GEMINI_API_KEY environment variable not set"}

    try:
        genai.configure(api_key=api_key)

        # Load DSP code if available
        dsp_code = ""
        module_description = "No description available"
        dsp_file = get_dsp_file_for_module(module_name)
        if dsp_file:
            with open(dsp_file) as f:
                dsp_code = f.read()
            module_description = extract_module_description(dsp_code)

        # Upload audio to Gemini
        audio_file = genai.upload_file(audio_path, mime_type="audio/wav")

        # Create model and analyze
        model = genai.GenerativeModel(GEMINI_MODEL)

        # Build prompt with DSP context
        prompt = GEMINI_PROMPT_TEMPLATE.format(
            module_name=module_name,
            module_description=module_description,
            dsp_code=dsp_code if dsp_code else "DSP code not available"
        )

        response = model.generate_content([prompt, audio_file])
        analysis_text = response.text

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
        try:
            audio_file.delete()
        except Exception:
            pass

        return {
            "analysis": analysis_text,
            "quality_score": quality_score,
            "musical_score": musical_score,
            "issues": issues,
            "suggestions": suggestions
        }

    except Exception as e:
        return {"error": str(e)}


def analyze_module(module_name: str, wav_path: Path,
                   use_gemini: bool = True, use_clap: bool = True) -> AIAnalysisResult:
    """Run full AI analysis on a module."""
    result = AIAnalysisResult(module_name=module_name)

    # CLAP analysis
    if use_clap and HAS_CLAP:
        print("  Running CLAP analysis...")
        result.clap_scores = analyze_with_clap(wav_path)

    # Gemini analysis
    if use_gemini and HAS_GEMINI and os.environ.get("GEMINI_API_KEY"):
        print("  Running Gemini analysis...")
        gemini_result = analyze_with_gemini(wav_path, module_name)

        if "error" not in gemini_result:
            result.gemini_analysis = gemini_result.get("analysis", "")
            result.gemini_quality_score = gemini_result.get("quality_score")
            result.gemini_musical_score = gemini_result.get("musical_score")
            result.gemini_issues = gemini_result.get("issues", [])
            result.gemini_suggestions = gemini_result.get("suggestions", [])
        else:
            result.gemini_analysis = f"Error: {gemini_result['error']}"

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

    print(f"Analyzing {len(modules)} module(s)...")
    print(f"  CLAP: {'enabled' if use_clap else 'disabled'}")
    print(f"  Gemini: {'enabled' if use_gemini else 'disabled'}")

    # Analyze
    results = []

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)

        for module_name in modules:
            print(f"\n{'=' * 50}")
            print(f"Analyzing: {module_name}")
            print('=' * 50)
            print("  Rendering audio...")

            wav_path = tmp_dir / f"{module_name}_ai.wav"

            if not render_audio(module_name, wav_path):
                print(f"  Error: Failed to render")
                continue

            result = analyze_module(module_name, wav_path, use_gemini, use_clap)
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
