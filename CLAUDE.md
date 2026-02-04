# WiggleRoom - VCV Rack Plugin

## Development Workflow

**IMPORTANT:** Always use the feature-dev skill for feature development tasks. When implementing new features or making significant changes to the codebase, invoke the `feature-dev:feature-dev` skill to ensure guided feature development with proper codebase understanding and architecture focus.

```
/feature-dev <task description>
```

**For comprehensive development documentation, see [DEVELOPMENT.md](DEVELOPMENT.md).**

**For release strategy and plugin packs, see [docs/RELEASE_STRATEGY.md](docs/RELEASE_STRATEGY.md).**

## Plugin Packs

WiggleRoom is organized into **thematic packs** with **Free** and **Pro** tiers:

| Pack | Free Modules | Pro Modules |
|------|--------------|-------------|
| **Voices** | PluckedString, ModalBell | ChaosFlute, SpaceCello, TheAbyss, Matter, VektorX |
| **Effects** | LadderLPF, BigReverb, InfiniteFolder | SaturationEcho, TriPhaseEnsemble, SpectralResonator, TheCauldron |
| **Modulators** | GravityClock, OctoLFO, Intersect | Cycloid, TheArchitect |

See [docs/RELEASE_STRATEGY.md](docs/RELEASE_STRATEGY.md) for full details on naming, pricing, and release phases.

## Project Structure

```
WiggleRoom/
├── .claude/                  # Claude Code configuration
│   ├── agents/               # Subagent prompt templates
│   │   ├── verifier.md       # Build + test + collect metrics
│   │   ├── judge.md          # Evaluate + prioritize fixes
│   │   └── dev-agent.md      # Apply targeted DSP fixes
│   └── skills/               # Custom slash commands
│       └── dsp-fix/          # /dsp-fix workflow
│           └── SKILL.md      # Automated verify → judge → fix loop
├── cmake/                    # CMake helpers & toolchains
│   ├── RackSDK.cmake         # SDK finder/downloader
│   └── Toolchain-*.cmake     # Cross-compilation toolchains
├── docs/                     # Documentation
│   └── RELEASE_STRATEGY.md   # Plugin packs, pricing, release phases
├── faust/                    # Faust architecture file
│   └── vcvrack.cpp           # VCVRackDSP wrapper template
├── res/                      # Panel graphics (PNG for AI-gen, SVG for hand-drawn)
├── scripts/                  # Development scripts
│   └── generate_faceplate.py # AI faceplate generation (Gemini)
├── src/
│   ├── common/               # Shared utilities
│   │   ├── DSP.hpp           # DSP utilities (V/Oct, smoothing)
│   │   └── FaustModule.hpp   # Base class for Faust modules
│   ├── modules/              # Auto-discovered modules
│   │   └── ModuleName/
│   │       ├── ModuleName.cpp
│   │       ├── CMakeLists.txt
│   │       ├── test_config.json  # Module test configuration
│   │       └── module_name.dsp   # (Faust modules only)
│   └── plugin.cpp            # Plugin entry point
├── test/                     # Testing framework
│   ├── agents/               # Automation tools (NOT Claude subagents)
│   │   ├── verifier_agent.py # Build and test automation
│   │   ├── judge_agent.py    # Quality evaluation automation
│   │   └── orchestrator.py   # Feedback loop coordinator
│   ├── faust_render.cpp      # Audio rendering tool
│   ├── test_framework.py     # Main test runner
│   ├── audio_quality.py      # Audio quality analysis (THD, aliasing, etc.)
│   ├── ai_audio_analysis.py  # AI-powered analysis (Gemini + CLAP)
│   ├── analyze_param_ranges.py # Parameter range optimization
│   └── dsp_wrappers.cpp      # DSP factory functions
├── CMakeLists.txt            # Root build config
├── Justfile                  # Build automation
├── plugin.json               # Plugin manifest
└── DEVELOPMENT.md            # Developer guide
```

## Build Commands

```bash
just build      # Compile the plugin
just install    # Build and install to VCV Rack (restart Rack to reload)
just clean      # Remove build artifacts
just package    # Cross-compile for all platforms (requires Docker)
```

## Testing Terminology

This project uses ISO 9001 / software engineering standard terminology:

| Term | Question | What It Checks |
|------|----------|----------------|
| **Verification** | "Are we building the product right?" | Structural correctness, conformance to specs |
| **Validation** | "Are we building the right product?" | Audio quality, fitness for purpose |

### Command Summary

```bash
# VERIFICATION (fast, no build needed)
just verify                  # All verification checks
just verify-manifest         # Check plugin.json against VCV requirements
just verify-manifest-tests   # Run manifest verification unit tests
just verify-test-infra       # Test the test infrastructure

# VALIDATION (requires build)
just validate-modules        # Run module audio quality tests
just validate-audio          # Audio quality analysis (THD, aliasing)
just validate-ai             # AI-powered analysis (Gemini + CLAP)

# COMBINED
just ci                      # Full CI pipeline: verify -> build -> validate
just test                    # Same as ci
```

## Testing

```bash
# Run full test suite (verification + build + validation)
just test

# VERIFICATION (structural checks)
just verify-manifest         # Check plugin.json requirements
just verify-test-infra       # Test infrastructure unit tests

# VALIDATION (audio quality)
just validate-modules        # Run module tests
just validate-modules ModuleName  # Test specific module
just validate-audio ModuleName    # Audio quality analysis

# Quick testing
just test-modules-fast       # Module tests without quality analysis

# List module parameters
./build/test/faust_render --module ModuleName --list-params
```

See [DEVELOPMENT.md](DEVELOPMENT.md#testing-framework) for full testing documentation.

## Showcase Audio & Reports

Generate comprehensive showcase audio for modules with multiple notes and parameter sweeps:

```bash
# Generate showcase report for all modules
just showcase

# Generate for specific module
just showcase-module ChaosFlute

# Skip AI analysis (faster)
just showcase-fast

# Render showcase audio only (no report)
just render-showcase ChaosFlute
```

The showcase report (`test/output/showcase_report.html`) provides:
- Module SVG panel display
- Audio player with full showcase clip
- Spectrogram visualization
- Note sequence visualization (piano roll)
- Parameter automation graph
- Quality metrics (THD, HNR, peak, clipping)
- AI analysis (CLAP scores, sound character, Gemini analysis)
- Pass/fail status based on thresholds

### Environment Setup for AI Analysis

Create a `.env` file in the project root (copy from `.env.example`):
```bash
# .env
GEMINI_API_KEY=your_api_key_here

# Gemini model - MUST use full path with models/ prefix
GEMINI_MODEL=models/gemini-3-pro-preview
```

Get your API key at: https://aistudio.google.com/apikey

The scripts automatically load this file. Without it, only CLAP analysis runs (no API key needed).

### Gemini Audio Analysis Notes

**Model Configuration:**
- Always use the full model path with `models/` prefix (e.g., `models/gemini-3-pro-preview`)
- Recommended model: `models/gemini-3-pro-preview` for best audio analysis quality
- Alternative models: `models/gemini-2.5-flash`, `models/gemini-2.5-pro`

**Multi-Channel Audio:**
- The analysis script automatically converts multi-channel WAV files to stereo
- This is required because Gemini API does not support >2 channel audio
- Modules like TR808 output 13 channels (individual drums + stereo mix)

**Troubleshooting:**
| Error | Solution |
|-------|----------|
| `404 model not found` | Use full path: `models/gemini-3-pro-preview` not `gemini-3-pro-preview` |
| `500 Internal error` | Often means unsupported audio format; script now auto-converts to stereo |
| `400 Invalid argument` | Check audio file format (must be WAV, ≤20MB) |

**Running Analysis:**
```bash
# Full analysis with CLAP + Gemini
python3 test/ai_audio_analysis.py --module ModuleName -v

# CLAP only (no API key needed)
python3 test/ai_audio_analysis.py --module ModuleName --clap-only -v
```

### Showcase Configuration

Each module can define custom showcase settings in `test_config.json`:

```json
{
  "showcase": {
    "duration": 10.0,
    "notes": [
      {"start": 0.0, "duration": 2.0, "volts": -1.0, "velocity": 1.0},
      {"start": 2.5, "duration": 2.0, "volts": 0.0, "velocity": 0.9}
    ],
    "automations": [
      {"param": "brightness", "start_time": 0.0, "end_time": 10.0, "start_value": 0.2, "end_value": 0.8}
    ]
  }
}
```

| Module Type | Default Showcase |
|-------------|------------------|
| **Instrument** | 10s, 4 notes at C3/C4/C5/G4 |
| **Filter** | 8s, cutoff sweep up/down |
| **Effect** | 10s, repeated bursts with mix sweep |
| **Resonator** | 10s, varied excitation with decay sweep |

## Faceplate Generation

Generate AI-powered PNG faceplates using Gemini Imagen 4.0:

```bash
# Generate faceplate for a specific module
just faceplate ModuleName

# Preview the AI prompt without generating
just faceplate-prompt ModuleName

# Generate faceplates for all modules
just faceplate-all

# List available modules and their HP widths
python3 scripts/generate_faceplate.py --list
```

### Panel Dimensions

VCV Rack panels use specific dimensions based on HP (horizontal pitch):
- **Width**: `HP × 15.24 × 3` pixels (e.g., 8HP = 366px, 20HP = 914px)
- **Height**: `380 × 3 = 1140` pixels (standard 3U height)
- **Aspect ratio**: ~9:16 requested from Imagen API

### Adding New Modules

When adding a new module, update `scripts/generate_faceplate.py`:

```python
MODULE_HP = {
    # ... existing modules ...
    "MyNewModule": 8,  # Add your module's HP width
}
```

### Style Guide

The script generates Grateful Dead-inspired psychedelic artwork:
- Dark backgrounds (deep purple, navy, black)
- Flowing organic shapes and sacred geometry
- Bioluminescent accents (cyan, magenta, orange, gold)
- Module name at top, "WIGGLE ROOM" brand at bottom
- Clear middle area for hardware controls

Requires `GEMINI_API_KEY` in `.env` file (same key used for AI audio analysis).

## Iterative Module Development

When debugging or improving Faust DSP modules, use this systematic approach:

```bash
# 1. Baseline: Run all quality tests
python3 test/audio_quality.py --module ModuleName --report -v

# 2. Get AI assessment (includes DSP context for artistic intent)
# Note: Requires GEMINI_API_KEY in .env file for Gemini analysis
python3 test/ai_audio_analysis.py --module ModuleName -v

# 3. Make ONE targeted DSP change based on feedback
# 4. Rebuild and re-test
just build
python3 test/audio_quality.py --module ModuleName --report

# 5. Repeat until improved
```

**Key metrics to track:**
- Peak Amplitude (0.3-0.9 is healthy)
- THD (< 15% for most, higher for distortion effects)
- HNR (> 10 dB for good tone-to-noise ratio)
- Attack/Release times (matches instrument type)

**AI analysis includes DSP code** so Gemini evaluates against the module's intended artistic effect (e.g., "chaos flute" vs generic flute).

**Parameter range optimization** - constrain ranges to safe/interesting areas:
```bash
# Analyze parameter ranges
just analyze-ranges ModuleName

# Get Faust code with recommended ranges
just analyze-ranges-faust ModuleName

# Full validation (quality + ranges + AI)
just validate ModuleName
```

**Complete development feedback loop:**
1. Implement DSP → `just build`
2. Validate → `just validate ModuleName`
3. If issues: fix DSP OR constrain ranges
4. Re-validate until all pass

See [DEVELOPMENT.md#iterative-module-development-technique](DEVELOPMENT.md#iterative-module-development-technique) for detailed workflow.

## DSP Module Fix Workflow

For Faust DSP module development, use the **`/dsp-fix` slash command** to automate the verify → judge → fix cycle until the module passes quality thresholds.

### Quick Start

```bash
# Run the automated fix workflow for a module
/dsp-fix ModuleName
```

This slash command orchestrates the complete feedback loop:

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│    Verifier     │────▶│      Judge      │────▶│    Dev Agent    │
│                 │     │                 │     │                 │
│ - Build         │     │ - Evaluate      │     │ - Read DSP      │
│ - Quality tests │     │ - Prioritize    │     │ - Apply fix     │
│ - AI analysis   │     │ - Generate fix  │     │ - Rebuild       │
│                 │     │   instructions  │     │                 │
└─────────────────┘     └─────────────────┘     └────────┬────────┘
         ▲                                               │
         └───────────────────────────────────────────────┘
                        (iterate until PASS)
```

### Pass Thresholds

| Metric | Default Threshold | Notes |
|--------|-------------------|-------|
| Build | Must succeed | No errors |
| Clipping | <5% | Or module-specific in test_config.json |
| Quality Score | ≥70 | Combined metric |
| THD | <15% | Higher OK for distortion effects |
| HNR | >0 dB | Harmonic-to-noise ratio |

### Subagent Reference

The `/dsp-fix` command uses three subagent roles (prompts in `.claude/agents/`):

| Agent | File | Purpose |
|-------|------|---------|
| **Verifier** | `.claude/agents/verifier.md` | Build, run quality tests, collect metrics |
| **Judge** | `.claude/agents/judge.md` | Evaluate results, prioritize fixes |
| **Dev** | `.claude/agents/dev-agent.md` | Apply targeted fixes to DSP code |

### Manual Subagent Spawning

You can also spawn subagents manually using the Task tool:

#### Verifier Subagent

```
Task tool:
  subagent_type: "general-purpose"
  description: "Verify {ModuleName}"
  prompt: |
    You are the Verifier for Faust DSP module development.

    Your task: Build and test the {MODULE_NAME} module, returning structured quality metrics.

    ## Steps
    1. Build: `just build`
    2. Quality tests: `python3 test/audio_quality.py --module {MODULE_NAME} --report -v`
    3. AI analysis: `python3 test/ai_audio_analysis.py --module {MODULE_NAME} --clap-only -v`
    4. Parameter ranges: `python3 test/analyze_param_ranges.py {MODULE_NAME}`

    ## Output Format
    Return: Build Status, Audio Quality (Peak, Clipping, THD, HNR, Score), Issues Found, AI Analysis, Verdict (PASS/NEEDS_WORK/CRITICAL_ISSUES)

    Do NOT fix issues - just report them.
```

#### 2. Judge Subagent

Spawns a subagent to evaluate Verifier results and generate prioritized fix instructions.

```
Task tool:
  subagent_type: "general-purpose"
  description: "Judge {ModuleName}"
  prompt: |
    You are the Judge for Faust DSP module development.

    Your task: Evaluate verification results and generate prioritized fix instructions.

    ## Verification Results
    {PASTE_VERIFIER_OUTPUT_HERE}

    ## Severity Classification
    - CRITICAL: Build failure, silent output, clipping >15%
    - HIGH: Clipping 5-15%, quality score <60
    - MEDIUM: THD >15%, HNR <10dB, AI-detected harshness
    - LOW: Minor suggestions, optimizations

    ## Output Format
    Return: Verdict, Score, Priority Issues (with specific Faust code fixes), Next Action

    Do NOT apply fixes - generate instructions for Dev agent.
```

#### 3. Dev Agent (Optional)

Can apply fixes in main conversation, or spawn a dedicated subagent:

```
Task tool:
  subagent_type: "general-purpose"
  description: "Fix {ModuleName}"
  prompt: |
    You are the Dev Agent for Faust DSP module development.

    Your task: Apply the #1 priority fix to {MODULE_NAME}.

    ## Judge Instructions
    {PASTE_JUDGE_OUTPUT_HERE}

    ## Workflow
    1. Read DSP: src/modules/{MODULE_NAME}/{module_name}.dsp
    2. Apply ONLY the #1 priority fix
    3. Rebuild: `just build`

    ## Common Fixes
    - Clipping: `output : ma.tanh : *(0.7)`
    - Clicks: `gate : si.smooth(0.995)`
    - DC Offset: `signal : fi.dcblocker`

    Report what you changed and build status.
```

### Severity Levels

| Severity | Action | Examples |
|----------|--------|----------|
| **CRITICAL** | Fix immediately | Build failure, silent output, clipping >15% |
| **HIGH** | Fix before release | Clipping 5-15%, quality score <60 |
| **MEDIUM** | Should fix | AI-detected issues, parameter problems, low HNR |
| **LOW** | Optional | Minor suggestions, optimizations |

### Pass Thresholds

| Metric | Default Threshold | Notes |
|--------|-------------------|-------|
| Build | Must succeed | No errors |
| Clipping | <5% | Or module-specific in test_config.json |
| Quality Score | ≥70 | Combined metric |
| THD | <15% | Higher OK for distortion effects |
| HNR | >0 dB | Harmonic-to-noise ratio |

### Quick Commands

```bash
# Manual validation (without subagents)
just validate ModuleName

# Quality tests only
python3 test/audio_quality.py --module ModuleName --report -v

# AI analysis (CLAP embeddings)
python3 test/ai_audio_analysis.py --module ModuleName --clap-only -v

# Parameter range analysis
python3 test/analyze_param_ranges.py ModuleName
```

### Python Automation Tools

The `test/agents/` directory has Python tools the subagents can use:

| Tool | Command | Purpose |
|------|---------|---------|
| `verifier_agent.py` | `python3 test/agents/verifier_agent.py ModuleName -v` | Run all tests |
| `judge_agent.py` | `python3 test/agents/judge_agent.py ModuleName` | Evaluate results |
| `orchestrator.py` | `python3 -m test.agents.orchestrator ModuleName --single` | Single iteration |

### Example Loop Execution

```
1. User: "Fix the TetanusCoil module"

2. Claude spawns Verifier:
   → Builds module
   → Runs quality tests
   → Returns: "THD 28%, HNR -0.1dB, Verdict: NEEDS_WORK"

3. Claude spawns Judge:
   → Evaluates results
   → Returns: "#1 Priority: Adjust test_config.json thresholds for extreme effect"

4. Claude (or Dev agent) applies fix:
   → Updates test_config.json
   → Rebuilds

5. Claude spawns Verifier again:
   → Returns: "Verdict: PASS"

6. Done!
```

## Module Architecture

### Two Module Types

| Type | Base Class | Use Case |
|------|------------|----------|
| **Faust DSP** | `FaustModule<VCVRackDSP>` | Audio DSP (filters, synths, effects) |
| **Native C++** | `rack::Module` | Custom algorithms, utilities, sequencers |

### Faust Module Pattern

```cpp
#include "FaustModule.hpp"
#define FAUST_MODULE_NAME MyModule
#include "my_module.hpp"

struct MyModule : FaustModule<VCVRackDSP> {
    MyModule() {
        config(...);

        // Map VCV params to Faust params (ALPHABETICAL index)
        mapParam(CUTOFF_PARAM, 0);     // cutoff -> index 0
        mapParam(RESONANCE_PARAM, 1);  // resonance -> index 1

        // CV modulation
        mapCVInput(CV_INPUT, 0, true);  // V/Oct exponential
        mapCVInput(CV_INPUT, 1, false, 0.1f);  // Linear
    }
};
```

**Key points:**
- Faust parameters are indexed **alphabetically by name**
- Use `mapParam()` and `mapCVInput()` in constructor
- Call `updateFaustParams()` in `process()` override
- Gate threshold should be `> 0.9` for test compatibility

### Faust DSP Conventions

```faust
import("stdfaust.lib");

// Parameters with smoothing
cutoff = hslider("cutoff", 1000, 20, 20000, 1) : si.smoo;

// Gate/trigger detection (0.9 threshold for test compatibility)
gate = hslider("gate", 0, 0, 10, 0.01);
trig = (gate > 0.9) & (gate' <= 0.9);

// V/Oct pitch (0V = C4)
volts = hslider("volts", 0, -5, 10, 0.001);
freq = max(20, 261.62 * (2.0 ^ volts));

// Stability protection
dc_block = fi.dcblocker;
soft_limit = ma.tanh;
```

## Adding a New Module (Quick Reference)

1. **Create files:**
   - `src/modules/MyModule/my_module.dsp` (Faust DSP)
   - `src/modules/MyModule/MyModule.cpp` (VCV wrapper)
   - `src/modules/MyModule/CMakeLists.txt`
   - `src/modules/MyModule/test_config.json` (test configuration)
   - `res/MyModule.png` or `res/MyModule.svg` (panel - see [Faceplate Generation](#faceplate-generation))

2. **Register module:**
   - `src/plugin.cpp`: Add `extern Model* modelMyModule;` (with `#ifdef HAS_MYMODULE` guard) and `p->addModel(modelMyModule);`
   - `plugin.json`: Add module entry

3. **CRITICAL for Faust modules - Add compile definition to CMakeLists.txt:**
   ```cmake
   # Find the section with other HAS_* definitions (around line 105) and add:
   if(MyModule_Module IN_LIST ACTIVE_MODULES)
       target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_MYMODULE=1)
   endif()
   ```
   **Without this step, the module will not be registered and VCV Rack will fail to load the plugin!**

4. **Create test_config.json:**
   ```json
   {
     "module_type": "instrument",
     "description": "Brief description of your module",
     "quality_thresholds": {
       "thd_max_percent": 15.0,
       "clipping_max_percent": 1.0
     },
     "test_scenarios": [
       {"name": "default", "duration": 2.0}
     ]
   }
   ```
   Module types: `instrument`, `filter`, `effect`, `resonator`, `utility`

5. **Add to tests:**
   - `test/faust_render.cpp`: Factory declaration and function
   - `test/dsp_wrappers.cpp`: DSP wrapper
   - `test/CMakeLists.txt`: Module mapping

6. **Generate faceplate** (if using AI-generated PNG):
   ```bash
   # Add module HP to scripts/generate_faceplate.py MODULE_HP dict first
   just faceplate MyModule
   ```

7. **Build and test:**
   ```bash
   just build
   python3 test/test_framework.py --module MyModule -v
   just install
   ```

**Full walkthrough: [DEVELOPMENT.md#adding-a-new-module](DEVELOPMENT.md#adding-a-new-module)**

## Key Requirements

### Version Compatibility
- **plugin.json version must start with "2."** (e.g., "2.0.0") for VCV Rack 2.x compatibility
- The major version indicates Rack ABI compatibility

### macOS Linker Flags
- VCV Rack plugins use `-undefined dynamic_lookup` to allow undefined symbols
- Symbols are resolved at runtime when Rack loads the plugin

### Plugin Paths (VCV Rack Pro)
- **macOS ARM64**: `~/Library/Application Support/Rack2/plugins-mac-arm64/`
- **macOS x64**: `~/Library/Application Support/Rack2/plugins-mac-x64/`
- **Windows**: `%LOCALAPPDATA%/Rack2/plugins-win-x64/`
- **Linux**: `~/.Rack2/plugins-lin-x64/`

## Debugging

Check VCV Rack log for errors:
```bash
# macOS
cat ~/Library/Application\ Support/Rack2/log.txt | grep -i "wiggle\|error"

# Linux
cat ~/.Rack2/log.txt | grep -i "wiggle\|error"
```

## SDK Auto-Download

CMake automatically downloads the VCV Rack SDK if `RACK_DIR` is not set. The SDK is cached in `build/Rack-SDK/`.

To use a custom SDK location:
```bash
export RACK_DIR=/path/to/Rack-SDK
just build
```
