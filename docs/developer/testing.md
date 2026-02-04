# Testing Framework

The test framework validates modules by rendering audio and analyzing the output.

## Quick Start

```bash
# Run all tests
just test

# Test specific module
just validate-modules ModuleName

# Run without audio quality analysis (faster)
just test-modules-fast
```

## Test Structure

```
test/
├── test_framework.py      # Main test runner
├── audio_quality.py       # THD, aliasing, harmonics analysis
├── ai_audio_analysis.py   # AI-powered analysis (Gemini + CLAP)
├── faust_render.cpp       # C++ audio rendering
├── dsp_wrappers.cpp       # Module factory functions
└── analyze_sensitivity.py # Parameter sensitivity
```

## Test Pyramid

```
            ┌───────────────────────┐
            │    Audio Quality      │  THD, aliasing, harmonics
            ├───────────────────────┤
            │     Regression        │  Baseline comparison
          ┌─┴───────────────────────┴─┐
          │      Sensitivity          │  All params affect output
        ┌─┴───────────────────────────┴─┐
        │       Gate Response           │  Envelope responds
      ┌─┴───────────────────────────────┴─┐
      │        Audio Stability            │  No NaN/Inf/clipping/DC
    ┌─┴───────────────────────────────────┴─┐
    │           Basic Render                │  Can generate audio
  ┌─┴───────────────────────────────────────┴─┐
  │             Compilation                   │  Module loads
  └─────────────────────────────────────────────┘
```

## Test Types

### Core Tests

| Test | What It Checks |
|------|----------------|
| `compilation` | Module loads and lists parameters |
| `basic_render` | Renders 2s audio without NaN/Inf |
| `audio_stability` | DC offset < 1%, clipping < 1%, not silent |
| `gate_response` | Gate parameter affects output (instruments) |
| `pitch_tracking` | V/Oct produces correct octave ratio |
| `parameter_sensitivity` | All parameters affect output |
| `regression` | Output matches baseline hash/stats |

### Audio Quality Tests

| Test | What It Checks |
|------|----------------|
| `thd` | Total Harmonic Distortion |
| `aliasing` | Aliasing artifacts |
| `harmonic_character` | Even vs odd harmonic balance |
| `spectral_richness` | Spectral entropy, HNR, crest factor |
| `envelope` | Attack/decay/release times |

## Module Test Configuration

Create `src/modules/ModuleName/test_config.json`:

```json
{
  "module_type": "instrument",
  "description": "Module description for context",
  "quality_thresholds": {
    "thd_max_percent": 15.0,
    "clipping_max_percent": 1.0,
    "hnr_min_db": 0.0,
    "allow_hot_signal": false
  },
  "test_scenarios": [
    {
      "name": "default",
      "duration": 2.0,
      "description": "Default settings"
    },
    {
      "name": "extreme",
      "duration": 3.0,
      "parameters": { "drive": 0.9, "mix": 1.0 },
      "description": "Extreme parameter settings"
    }
  ]
}
```

### Module Types

| Type | Description | Test Input |
|------|-------------|------------|
| `instrument` | Generates sound from gate/trigger | Gate signal only |
| `filter` | Processes continuous audio | Saw wave input |
| `effect` | Processes audio with memory | Short burst then silence |
| `resonator` | Needs excitation to ring | Repeated noise bursts |
| `utility` | Non-audio (LFO, clock, etc.) | Skip audio tests |

### Skip Audio Tests

For utility modules:

```json
{
  "module_type": "utility",
  "skip_audio_tests": true,
  "skip_reason": "LFO - outputs control voltages, not audio"
}
```

## Quality Metrics

### THD (Total Harmonic Distortion)

| Range | Quality |
|-------|---------|
| < 1% | Very clean (hi-fi) |
| 1-5% | Clean |
| 5-15% | Noticeable coloration |
| > 15% | High distortion (wavefolders, saturators) |

### Aliasing

| Level | Quality |
|-------|---------|
| < -60 dB | No significant aliasing |
| -60 to -40 dB | Minor aliasing |
| > -40 dB | Significant aliasing |

### Harmonic Character

| Warmth Ratio | Character |
|--------------|-----------|
| > 1.5 | Warm/tube-like (even harmonics) |
| 0.5-1.5 | Neutral |
| < 0.5 | Bright/edgy (odd harmonics) |

### Envelope

| Attack | Character |
|--------|-----------|
| < 5ms | Percussive |
| 5-50ms | Medium |
| > 50ms | Pad-like |

## AI-Powered Analysis

```bash
# Full AI analysis (requires GEMINI_API_KEY)
just validate-ai ModuleName

# CLAP-only analysis (no API key needed)
just test-ai-clap ModuleName
```

The AI analysis includes DSP source code, so Gemini evaluates against artistic intent.

## Parameter Range Analysis

```bash
# Analyze parameter ranges
just analyze-ranges ModuleName

# Get Faust code with recommended ranges
just analyze-ranges-faust ModuleName
```

## Development Workflow

```
┌─────────────────────────────────────────────────┐
│           MODULE DEVELOPMENT LOOP                │
├─────────────────────────────────────────────────┤
│  1. Implement DSP                                │
│         ↓                                        │
│  2. just build                                   │
│         ↓                                        │
│  3. just validate ModuleName     ←────────┐     │
│         ↓                                 │     │
│  4. Review results:                       │     │
│     • Quality score > 80?                 │     │
│     • All params safe range?              │     │
│     • AI score acceptable?                │     │
│         ↓                                 │     │
│  5. If issues found:                      │     │
│     a. Fix DSP                            │     │
│     b. OR constrain param ranges          │     │
│     c. Rebuild and re-validate ──────────→┘     │
│         ↓                                        │
│  6. If all pass: Module complete ✓               │
└─────────────────────────────────────────────────┘
```

## Commands Reference

| Command | Description |
|---------|-------------|
| `just validate-modules` | Run all module tests |
| `just validate-modules ModuleName` | Test specific module |
| `just validate-audio ModuleName` | Audio quality analysis |
| `just validate-ai ModuleName` | AI analysis |
| `just test-modules-fast` | Tests without quality analysis |
| `just test-generate-baselines` | Generate regression baselines |
| `just analyze-ranges ModuleName` | Parameter range analysis |

## Next Steps

- [CI Pipeline](../ci/pipeline.md) - How CI runs tests
- [Agent System](../ci/agents.md) - Automated development loop
