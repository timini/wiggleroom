# WiggleRoom Developer Guide

This document covers the architecture, patterns, and practices for developing modules in the WiggleRoom VCV Rack plugin.

## Table of Contents

- [Module Architecture](#module-architecture)
- [Faust DSP Modules](#faust-dsp-modules)
- [Native C++ Modules](#native-c-modules)
- [Testing Framework](#testing-framework)
- [CI Pipeline](#ci-pipeline)
- [Adding a New Module](#adding-a-new-module)
- [Agent-Based Development](#agent-based-development)

---

## Module Architecture

WiggleRoom supports two module types:

| Type | Description | Base Class | Examples |
|------|-------------|------------|----------|
| **Faust DSP** | Audio DSP written in Faust language | `FaustModule<VCVRackDSP>` | LadderLPF, ModalBell, Matter |
| **Native C++** | Custom DSP written in C++ | `rack::Module` | SuperOsc, Cycloid, Intersect |

### Directory Structure

Each module lives in its own directory:

```
src/modules/
└── ModuleName/
    ├── ModuleName.cpp      # VCV Rack wrapper
    ├── CMakeLists.txt      # Build configuration
    └── module_name.dsp     # Faust DSP (if applicable)
```

---

## Faust DSP Modules

### Overview

Faust modules use the [Faust](https://faust.grame.fr/) language for DSP and a C++ wrapper for VCV Rack integration. The build system compiles `.dsp` files to C++ headers.

### Architecture Diagram

```
┌──────────────────┐    ┌─────────────────┐    ┌──────────────────┐
│   module.dsp     │───▶│  Faust Compiler │───▶│  module.hpp      │
│   (Faust DSP)    │    │                 │    │  (Generated C++) │
└──────────────────┘    └─────────────────┘    └────────┬─────────┘
                                                        │
                                                        ▼
┌──────────────────┐    ┌─────────────────┐    ┌──────────────────┐
│  VCV Rack        │◀───│  FaustModule    │◀───│  VCVRackDSP      │
│  (Host)          │    │  (Base Class)   │    │  (Wrapper)       │
└──────────────────┘    └─────────────────┘    └──────────────────┘
```

### Faust DSP Conventions

#### Parameter Declaration

Use `hslider()` with `si.smoo` for smoothed parameters:

```faust
import("stdfaust.lib");

// Parameters are indexed ALPHABETICALLY by Faust
cutoff = hslider("cutoff", 1000, 20, 20000, 1) : si.smoo;
resonance = hslider("resonance", 0, 0, 0.95, 0.01) : si.smoo;

process = ve.moog_vcf_2bn(resonance, cutoff);
```

**Important**: Faust indexes parameters alphabetically by name, not by declaration order.

#### Gate/Trigger Handling

For instruments with gate inputs:

```faust
// Gate input (0-10V from VCV Rack)
gate = hslider("gate", 0, 0, 10, 0.01);

// Trigger on rising edge crossing threshold
// Use 0.9 threshold (slightly below 1V) for compatibility with test framework
trig = (gate > 0.9) & (gate' <= 0.9);

// Envelope
env = en.ar(0.01, decay, trig);
```

#### V/Oct Pitch Tracking

Standard V/Oct implementation (0V = C4 = 261.62 Hz):

```faust
volts = hslider("volts", 0, -5, 10, 0.001);
freq = max(20, 261.62 * (2.0 ^ volts));
```

#### Stability Measures

Always include protection for numerical stability:

```faust
// Clamp Q factor to prevent instability
safe_resonbp(freq, q, g) = fi.resonbp(freq, max(1, q), g);

// DC blocker for stability
dc_block = fi.dcblocker;

// Soft limiter to prevent clipping
soft_limit = ma.tanh;

// Chain protections in output
process = my_synth : *(gain) : dc_block : soft_limit;
```

### FaustModule Base Class

The `FaustModule<T>` template class (`src/common/FaustModule.hpp`) handles:

- Parameter mapping between VCV knobs and Faust parameters
- CV input modulation (V/Oct and linear)
- Audio I/O buffering
- Sample rate change handling
- JSON state serialization

#### Key Methods

| Method | Purpose |
|--------|---------|
| `mapParam(vcvId, faustIdx)` | Map VCV knob to Faust parameter by index |
| `mapCVInput(vcvId, faustIdx, exp, scale)` | Map CV input to modulate a Faust parameter |
| `setAudioIO(inputId, outputId)` | Set main audio I/O jacks |
| `updateFaustParams()` | Sync all mapped parameters to Faust DSP |

#### Parameter Mapping Example

```cpp
struct MyFilter : FaustModule<VCVRackDSP> {
    enum ParamId { CUTOFF_PARAM, RESONANCE_PARAM, PARAMS_LEN };
    enum InputId { AUDIO_INPUT, CUTOFF_CV_INPUT, INPUTS_LEN };
    enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    MyFilter() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(CUTOFF_PARAM, 20.f, 20000.f, 1000.f, "Cutoff", " Hz");
        configParam(RESONANCE_PARAM, 0.f, 0.95f, 0.f, "Resonance");
        configInput(AUDIO_INPUT, "Audio");
        configInput(CUTOFF_CV_INPUT, "Cutoff CV (V/Oct)");
        configOutput(AUDIO_OUTPUT, "Filtered Audio");

        setAudioIO(AUDIO_INPUT, AUDIO_OUTPUT);

        // Faust params alphabetically: cutoff=0, resonance=1
        mapParam(CUTOFF_PARAM, 0);
        mapParam(RESONANCE_PARAM, 1);

        // V/Oct exponential scaling for cutoff
        mapCVInput(CUTOFF_CV_INPUT, 0, true, 1.0f);
    }
};
```

#### Custom Process Override

For modules needing custom processing (e.g., instruments with gate/V/Oct):

```cpp
void process(const ProcessArgs& args) override {
    if (!initialized) {
        faustDsp.init(static_cast<int>(args.sampleRate));
        initialized = true;
    }

    // Update mapped parameters (knobs + CV)
    updateFaustParams();

    // Set unmapped parameters directly
    // Faust params alphabetically: gate=1, volts=7
    faustDsp.setParamValue(1, inputs[GATE_INPUT].getVoltage());
    faustDsp.setParamValue(7, inputs[VOCT_INPUT].getVoltage());

    // Process audio
    float* nullInputs[1] = { nullptr };
    float outputL = 0.f, outputR = 0.f;
    float* outputPtrs[2] = { &outputL, &outputR };

    faustDsp.compute(1, nullInputs, outputPtrs);

    outputs[LEFT_OUTPUT].setVoltage(outputL * 5.0f);
    outputs[RIGHT_OUTPUT].setVoltage(outputR * 5.0f);
}
```

### Faust Parameter Index Reference

To find Faust parameter indices, list them alphabetically:

```faust
// Example Faust DSP with these parameters:
decay = hslider("decay", 0.5, 0.1, 2.0, 0.01);
gate = hslider("gate", 0, 0, 10, 0.01);
hardness = hslider("hardness", 0.5, 0.01, 1.0, 0.01);
volts = hslider("volts", 0, -5, 10, 0.001);

// Alphabetical order: decay=0, gate=1, hardness=2, volts=3
```

Or use the test harness to list them:
```bash
./build/test/faust_render --module ModuleName --list-params
```

---

## Native C++ Modules

For custom DSP not using Faust, inherit directly from `rack::Module`:

```cpp
struct MyOscillator : rack::Module {
    enum ParamId { FREQ_PARAM, PARAMS_LEN };
    enum InputId { VOCT_INPUT, INPUTS_LEN };
    enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    float phase = 0.f;
    DSP::OnePoleLPF freqSmooth{0.01f};  // Parameter smoothing

    MyOscillator() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(FREQ_PARAM, 20.f, 20000.f, 440.f, "Frequency", " Hz");
        configInput(VOCT_INPUT, "V/Oct");
        configOutput(AUDIO_OUTPUT, "Audio");
    }

    void process(const ProcessArgs& args) override {
        float freq = params[FREQ_PARAM].getValue();

        // V/Oct modulation
        if (inputs[VOCT_INPUT].isConnected()) {
            freq *= DSP::voltToFreqMultiplier(inputs[VOCT_INPUT].getVoltage());
        }

        freq = freqSmooth.process(freq);

        // Generate oscillator
        float dt = freq / args.sampleRate;
        phase += dt;
        if (phase >= 1.f) phase -= 1.f;

        // Anti-aliased sawtooth using polyblep
        float saw = 2.f * phase - 1.f;
        saw -= DSP::polyblep(phase, dt);

        outputs[AUDIO_OUTPUT].setVoltage(saw * 5.f);
    }
};
```

### Shared DSP Utilities

`src/common/DSP.hpp` provides:

| Utility | Purpose |
|---------|---------|
| `voltToFreqMultiplier(v)` | Convert V/Oct to frequency multiplier |
| `midiToFreq(note)` | MIDI note number to frequency |
| `clamp(val, min, max)` | Clamp value to range |
| `lerp(a, b, t)` | Linear interpolation |
| `OnePoleLPF` | One-pole lowpass for parameter smoothing |
| `polyblep(t, dt)` | Polyblep anti-aliasing correction |

---

## Testing Framework

### Overview

The test framework validates Faust modules by rendering audio and analyzing the output.

```
test/
├── AbstractDSP.hpp        # Polymorphic DSP interface
├── dsp_wrappers.cpp       # Factory functions for each module
├── faust_render.cpp       # C++ audio rendering executable
├── test_framework.py      # Main test runner
├── audio_quality.py       # Audio quality analysis (THD, aliasing, harmonics)
├── analyze_audio.py       # Spectrogram generation
└── analyze_sensitivity.py # Parameter sensitivity analysis
```

### Module Test Configuration

Each module can have a `test_config.json` file in its directory that defines module-specific test settings:

```
src/modules/ModuleName/
├── ModuleName.cpp
├── module_name.dsp          # (Faust modules only)
├── CMakeLists.txt
└── test_config.json         # <-- Test configuration
```

#### test_config.json Format

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
  ],
  "parameter_sweeps": {
    "exclude": ["gate", "trigger"],
    "steps": 10
  }
}
```

#### Module Types

| Type | Description | Test Input |
|------|-------------|------------|
| `instrument` | Generates sound from gate/trigger | Gate signal only |
| `filter` | Processes continuous audio | Saw wave input |
| `effect` | Processes audio with memory | Short burst then silence |
| `resonator` | Needs excitation to ring | Repeated noise bursts |
| `utility` | Non-audio (LFO, clock, etc.) | Skip audio tests |

#### Quality Thresholds

| Threshold | Description | Default |
|-----------|-------------|---------|
| `thd_max_percent` | Maximum acceptable THD | 15.0 |
| `clipping_max_percent` | Maximum clipping ratio | 1.0 |
| `hnr_min_db` | Minimum harmonic-to-noise ratio | 0.0 |
| `allow_hot_signal` | Allow higher output (wavefolders) | false |

#### Skip Audio Tests

For utility modules (LFOs, clocks, sequencers) that don't produce audio output:

```json
{
  "module_type": "utility",
  "skip_audio_tests": true,
  "skip_reason": "LFO - outputs control voltages, not audio"
}
```

#### Using Test Scenarios

```bash
# List available scenarios
./build/test/faust_render --module ChaosFlute --list-scenarios

# Render with a specific scenario
./build/test/faust_render --module ChaosFlute --scenario high_chaos --output test.wav

# Show full module config
./build/test/faust_render --module ChaosFlute --show-config
```

### Test Pyramid

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

### Running Tests

```bash
# Run all tests
python3 test/test_framework.py

# Test specific module
python3 test/test_framework.py --module Matter

# Verbose output
python3 test/test_framework.py -v

# Generate baselines for regression testing
python3 test/test_framework.py --generate-baselines

# CI mode (JSON output, exit codes)
python3 test/test_framework.py --ci --output results.json
```

### Test Types

#### Core Tests

| Test | What It Checks |
|------|----------------|
| `compilation` | Module loads and lists parameters |
| `basic_render` | Renders 2s audio without NaN/Inf |
| `audio_stability` | DC offset < 1%, clipping < 1%, not silent |
| `gate_response` | Gate parameter affects output (instruments) |
| `pitch_tracking` | V/Oct produces correct octave ratio |
| `parameter_sensitivity` | All parameters affect output |
| `regression` | Output matches baseline hash/stats |

#### Audio Quality Tests

| Test | What It Checks |
|------|----------------|
| `thd` | Total Harmonic Distortion (< 15% for most modules) |
| `aliasing` | Aliasing artifacts from insufficient oversampling |
| `harmonic_character` | Even vs odd harmonic balance (warm vs bright) |
| `spectral_richness` | Spectral entropy, HNR, crest factor |
| `envelope` | Attack/decay/release times, transient sharpness |

### Module Type Classification

The test harness auto-generates appropriate input signals:

| Type | Modules | Input Signal |
|------|---------|--------------|
| Filter | LadderLPF, InfiniteFolder | 440Hz sawtooth |
| Resonator | SpectralResonator | Noise bursts (20ms every 300ms) |
| Effect | BigReverb, SaturationEcho, TriPhaseEnsemble | 100ms noise burst |
| Instrument | ModalBell, PluckedString, Matter, etc. | Gate signal only |

### Audio Analysis Tools

```bash
# Generate spectrogram
python3 test/analyze_audio.py output.wav --output spectrogram.png

# Parameter sensitivity analysis
python3 test/analyze_sensitivity.py --module Matter --output sensitivity/

# Audio quality analysis (THD, aliasing, harmonics, envelope)
just test-quality                    # All modules
just test-quality ModuleName         # Specific module
just test-quality-report ModuleName  # JSON report

# Run tests without quality analysis (faster)
just test-modules-fast
```

### Audio Quality Metrics Interpretation

#### THD (Total Harmonic Distortion)
- **< 1%**: Very clean (hi-fi quality)
- **1-5%**: Clean (acceptable for most synths)
- **5-15%**: Noticeable coloration (may be intentional)
- **> 15%**: High distortion (expected for wavefolders, saturators)

#### Aliasing
- **< -60 dB**: No significant aliasing
- **-60 to -40 dB**: Minor aliasing (may be acceptable)
- **> -40 dB**: Significant aliasing (consider oversampling)

#### Harmonic Character
- **Warmth ratio > 1.5**: Warm/tube-like (even harmonics dominant)
- **Warmth ratio 0.5-1.5**: Neutral balance
- **Warmth ratio < 0.5**: Bright/edgy (odd harmonics dominant)

#### Spectral Richness
- **Entropy 0.0-0.3**: Simple/pure tones
- **Entropy 0.3-0.6**: Moderate complexity
- **Entropy 0.6-1.0**: Rich/complex spectra

#### Envelope
- **Attack < 5ms**: Percussive/sharp transients
- **Attack 5-50ms**: Medium attack
- **Attack > 50ms**: Slow/pad-like

### AI-Assisted Audio Analysis

The test framework includes AI-powered analysis using Gemini and CLAP for subjective audio evaluation.

```bash
# Run AI analysis on all instruments
just test-ai

# Run on specific module with verbose output
GEMINI_API_KEY=your_key python3 test/ai_audio_analysis.py --module ChaosFlute -v

# CLAP-only analysis (no API key needed)
just test-ai-clap
```

**IMPORTANT**: The AI analysis includes the DSP source code and module description, so Gemini evaluates against the module's intended artistic effect rather than generic quality standards.

### Iterative Module Development Technique

For developing and debugging Faust DSP modules, use this systematic approach:

#### 1. Establish Baseline
```bash
# Run full quality tests to identify issues
python3 test/audio_quality.py --module ModuleName --report -v

# Run parameter sensitivity analysis
python3 test/analyze_sensitivity.py --module ModuleName

# Get AI subjective assessment (includes DSP context)
GEMINI_API_KEY=key python3 test/ai_audio_analysis.py --module ModuleName -v
```

#### 2. Iterate on Fixes

For each iteration:
1. **Make ONE targeted change** to the DSP based on test feedback
2. **Rebuild**: `just build`
3. **Re-run tests** to measure improvement:
   ```bash
   python3 test/audio_quality.py --module ModuleName --report
   ```
4. **Check AI analysis** for subjective feedback
5. **Document the change** and its effect

#### 3. Key Metrics to Track

| Metric | Good Range | What It Indicates |
|--------|------------|-------------------|
| Peak Amplitude | 0.3-0.9 | Output level (too low = not audible) |
| THD | < 15% | Harmonic distortion (high may be intentional) |
| HNR | > 10 dB | Tone-to-noise ratio (physical models need both) |
| Attack time | Context-dependent | Transient sharpness |
| Dynamic Range | > 10 dB | Expressiveness |

#### 4. Common Issues and Fixes

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| Very low output | Gain staging issue | Increase `output_gain` or excitation |
| Clicks at onset | Instant envelope attack | Add smoothing: `si.smooth(0.99)` |
| Physical model silent | Insufficient excitation | Boost input signal, adjust resonator feedback |
| Harsh high frequencies | Aliasing or no filtering | Add lowpass: `fi.lowpass(2, 8000)` |
| DC offset | No DC blocking | Add `fi.dcblocker` in signal chain |
| Clipping | Too much gain | Add soft limiter: `ma.tanh` or reduce gain |

#### Example: ChaosFlute Iteration Summary

| Iteration | Change | Result |
|-----------|--------|--------|
| 1 | Initial analysis | 1/10 quality - no oscillation |
| 2 | Added breath noise (min_breath=0.08) | Amplitude improved 4x |
| 3 | Added output filtering, smoothing | THD: 42% → 14% |
| 4 | Increased output_gain to 8.0 | Still too quiet for Gemini |
| 5 | Boosted excitation (2.5x) | **HNR: 27dB** - model oscillating! |
| 6 | Reduced boost, added chaos LFO | Dynamic range: 4.7 → 21 dB |

Each iteration was validated with both automated tests and AI analysis.

### Parameter Sweep Verification

After iterating on a module, verify it works across the full parameter range:

#### 1. Sweep Individual Parameters
```bash
# Pitch sweep
for v in -3 -2 -1 0 1 2 3 4; do
  ./build/test/faust_render --module ModuleName --output /dev/null \
    --duration 1.5 --param gate=5 --param volts=$v 2>&1 | grep "Clipped"
done

# Parameter sweep template
for val in 0.1 0.3 0.5 0.7 0.9; do
  ./build/test/faust_render --module ModuleName --output /dev/null \
    --duration 1.5 --param gate=5 --param paramName=$val 2>&1 | grep "Clipped"
done
```

#### 2. Test Edge Case Combinations
Identify and test worst-case combinations:
```bash
# Example: low pitch + high gain + resonance peak
./build/test/faust_render --module ModuleName --output edge_test.wav \
  --duration 2.0 --param gate=5 --param volts=-2 --param param1=max --param param2=peak
```

#### 3. Analyze Problem Renders with AI
```bash
# Quick CLAP analysis
python3 -c "
from test.ai_audio_analysis import analyze_with_clap
from pathlib import Path
result = analyze_with_clap(Path('edge_test.wav'))
print(f'Quality: {result.quality_score}/100')
print('Top negatives:', result.top_negative)
"
```

#### 4. Common Parameter-Related Issues

| Symptom | Common Cause | Fix |
|---------|--------------|-----|
| Clipping at low pitch | Waveguide energy accumulation | Add frequency-dependent gain compensation |
| Clipping at high resonance | Filter self-oscillation | Reduce feedback or add limiting |
| Silent at extremes | Parameter hitting zero | Add minimum floors |
| Unstable at certain values | Numerical edge cases | Clamp to safe ranges |

#### Example: ChaosFlute Parameter Fix

**Problem**: 32% clipping at low pitch + high mouth position

**Solution**: Add frequency-dependent gain compensation:
```faust
// Low frequencies produce more energy in waveguide models
// Scale down output at low pitches to prevent clipping
freq_compensation = min(1.0, freq / 261.62);  // Reference: C4
flute_sound = flute_raw * freq_compensation;
```

**Result**: 0% clipping across all parameter combinations.

### Parameter Range Optimization

After fixing issues, optimize parameter ranges to only cover safe/interesting areas.

#### Automated Range Analysis
```bash
# Analyze all parameters and get recommendations
just analyze-ranges ModuleName

# Get Faust code with recommended ranges
just analyze-ranges-faust ModuleName

# Full validation pipeline
just validate ModuleName
```

#### Range Analyzer Output
```
Analyzing ChaosFlute (7 parameters)
  Sweeping pressure: 0.0 -> 1.5 (15 steps)
    0.000: ○    0.107: ✓    0.214: ✓    ...

  pressure:
    Original: [0.0, 1.5] init=0.6
    Safe range: [0.167, 1.500]
    Interesting: [0.667, 1.500]
    Problems:
      - 0.000: silent
    Recommended: [0.167, 1.500] init=0.600
```

Legend:
- ✓ = Safe (no clipping, audible output)
- ○ = Silent (may be intentional for some params)
- ⚠ = Clipping detected

#### When to Constrain Ranges

| Situation | Action |
|-----------|--------|
| Silent at min/max | Constrain to audible range |
| Clipping at extremes | Either fix DSP OR constrain range |
| Unstable/noise at values | Constrain to stable range |
| Parameter has no effect | Consider removing from UI |

#### Applying Recommendations

The analyzer generates Faust code:
```faust
// Recommended parameter ranges for ChaosFlute
// Based on automated sweep analysis

// pressure: constrained from [0.0, 1.5]
pressure = hslider("pressure", 0.600, 0.167, 1.500, 0.01);
// Avoided: 0.00-0.00 (silent)
```

#### Complete Validation Pipeline

Run all validation steps:
```bash
just validate ModuleName
```

This runs:
1. **Audio Quality Test** - THD, aliasing, harmonics, envelope
2. **Parameter Range Analysis** - Finds safe/interesting ranges
3. **AI Analysis** - CLAP similarity scoring

#### Development Feedback Loop

```
┌─────────────────────────────────────────────────────────────┐
│                  MODULE DEVELOPMENT LOOP                     │
├─────────────────────────────────────────────────────────────┤
│  1. Implement DSP                                            │
│         ↓                                                    │
│  2. just build                                               │
│         ↓                                                    │
│  3. just validate ModuleName     ←──────────────────────┐   │
│         ↓                                                │   │
│  4. Review results:                                      │   │
│     • Quality score > 80?                                │   │
│     • All params safe range?                             │   │
│     • AI score acceptable?                               │   │
│         ↓                                                │   │
│  5. If issues found:                                     │   │
│     a. Fix DSP (clipping, noise, etc.)                   │   │
│     b. OR constrain param ranges                         │   │
│     c. Rebuild and re-validate ────────────────────────→─┘   │
│         ↓                                                    │
│  6. If all pass: Module complete ✓                           │
└─────────────────────────────────────────────────────────────┘
```

---

## CI Pipeline

The project uses GitHub Actions for continuous integration with a multi-stage pipeline that ensures code quality at every level.

### Testing Terminology (ISO 9001 / Software Engineering Standard)

| Term | Question | What It Checks |
|------|----------|----------------|
| **Verification** | "Are we building the product right?" | Structural correctness, conformance to specs |
| **Validation** | "Are we building the right product?" | Audio quality, fitness for purpose |

### Pipeline Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           CI PIPELINE STAGES                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ╔═══════════════════════════════════════════════════════════════════════╗  │
│  ║  VERIFICATION - "Are we building the product right?"                  ║  │
│  ╠═══════════════════════════════════════════════════════════════════════╣  │
│  ║  Stage 1: Verify Manifest     Fast checks, no build required          ║  │
│  ║  ├─ 1.1 Verify plugin.json (VCV Library requirements)                 ║  │
│  ║  ├─ 1.2 Manifest verification tests                                   ║  │
│  ║  └─ 1.3 JSON syntax check                                             ║  │
│  ║                                                                        ║  │
│  ║  Stage 2: Verify Test Infra   Test the test infrastructure            ║  │
│  ║  ├─ 2.1 Utility function tests (test_utils_unit.py)                   ║  │
│  ║  └─ 2.2 Audio quality analysis tests (test_audio_quality_unit.py)     ║  │
│  ║                                                                        ║  │
│  ║  Stage 3: Verify Build        Compile everything                       ║  │
│  ║  ├─ 3.1 Faust DSP compilation                                         ║  │
│  ║  └─ 3.2 Plugin build                                                  ║  │
│  ╚═══════════════════════════════════════════════════════════════════════╝  │
│                            │                                                 │
│                            ▼                                                 │
│  ╔═══════════════════════════════════════════════════════════════════════╗  │
│  ║  VALIDATION - "Are we building the right product?"                    ║  │
│  ╠═══════════════════════════════════════════════════════════════════════╣  │
│  ║  Stage 4: Validate Audio      Run module quality tests                 ║  │
│  ║  ├─ 4.1 Module tests (stability, sensitivity, quality)               ║  │
│  ║  └─ 4.2 Check test results                                            ║  │
│  ║                                                                        ║  │
│  ║  Stage 5: Validate Report     Generate detailed analysis               ║  │
│  ║  ├─ 5.1 Full validation report (THD, AI analysis)                     ║  │
│  ║  └─ 5.2 Prepare for deployment                                        ║  │
│  ╚═══════════════════════════════════════════════════════════════════════╝  │
│                            │                                                 │
│                            ▼                                                 │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │  Stage 6: Deploy              Publish report (main branch only)        │  │
│  │  └─ Deploy to GitHub Pages                                            │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Stage 1: Verify Manifest

Fast pre-build checks that catch common issues before compilation.

#### 1.1 Manifest Verification (`scripts/verify_manifest.py`)

Verifies `plugin.json` against VCV Library submission requirements:

| Check | Description | Severity |
|-------|-------------|----------|
| **Valid Tags** | All module tags must be from VCV's 57 official tags | ERROR |
| **Trademark Detection** | Names/descriptions cannot contain brand names (Moog, Roland, etc.) | ERROR |
| **Module Sync** | All modules in code must be in manifest and vice versa | ERROR |
| **Slug Format** | Slugs must be valid identifiers (alphanumeric + underscore) | ERROR |

**Valid Tags** (57 total, case-insensitive):
```
Arpeggiator, Attenuator, Blank, Chorus, Clock generator, Clock modulator,
Compressor, Controller, Delay, Distortion, Drum, Dual, Dynamics, Effect,
Envelope follower, Envelope generator, Equalizer, Expander, External,
Filter, Flanger, Function generator, Granular, Hardware clone, Limiter,
Logic, Low-frequency oscillator, Low-pass gate, Mid-side, Mixer, Multiple,
Noise, Oscillator, Panning, Phaser, Physical modeling, Polyphonic, Quad,
Quantizer, Random, Recording, Reverb, Ring modulator, Sample and hold,
Sampler, Sequencer, Slew limiter, Speech, Switch, Synth voice, Tuner,
Utility, Visual, Vocoder, Voltage-controlled amplifier, Waveshaper
```

**Trademark Patterns Detected**:
- Brand names: moog, roland, korg, yamaha, oberheim, sequential, arp, buchla, etc.
- Product names: tb-303, tr-808, tr-909, minimoog, prophet, jupiter, juno, etc.

#### 1.2 Manifest Verification Tests (`test/test_manifest_verification.py`)

33 unit tests covering all verification scenarios:

```bash
# Run manifest verification tests
just verify-manifest-tests
```

| Test Category | Tests | Description |
|---------------|-------|-------------|
| Valid Tags | 8 | All 57 official tags accepted |
| Invalid Tags | 5 | Typos, made-up tags rejected |
| Trademark Detection | 12 | Brand names, product names, edge cases |
| Slug Validation | 4 | Valid/invalid slug formats |
| Full Manifest | 4 | Integration tests with complete manifests |

### Stage 2: Verify Test Infrastructure

Tests the test infrastructure itself to ensure verification tools work correctly.

#### 2.1 Utility Function Tests (`test/test_utils_unit.py`)

32 unit tests for shared test utilities:

| Test Class | Tests | What It Verifies |
|------------|-------|------------------|
| `TestGetProjectRoot` | 3 | Path resolution works correctly |
| `TestLoadModuleConfig` | 4 | Module configs load with defaults/overrides |
| `TestExtractAudioStats` | 7 | RMS, peak, DC offset, NaN/Inf detection |
| `TestDbConversions` | 8 | dB ↔ linear conversions accurate |
| `TestCamelToSnake` | 5 | Naming convention conversion |
| `TestExtractModuleDescription` | 3 | DSP comment extraction |
| `TestIntegration` | 2 | Real project files work |

```bash
# Run test infrastructure verification
just verify-test-infra
```

#### 2.2 Audio Quality Analysis Tests (`test/test_audio_quality_unit.py`)

31 unit tests for audio analysis functions:

| Test Class | Tests | What It Verifies |
|------------|-------|------------------|
| `TestMeasureTHD` | 6 | THD measurement accuracy |
| `TestDetectAliasing` | 3 | Aliasing detection |
| `TestAnalyzeHarmonics` | 4 | Even/odd harmonic analysis |
| `TestComputeSpectralRichness` | 5 | Entropy, flatness, HNR, crest factor |
| `TestAnalyzeEnvelope` | 4 | Attack/decay/release detection |
| `TestAudioQualityReport` | 3 | Report data structure |
| `TestLoadModuleConfig` | 2 | Audio quality thresholds |
| `TestEdgeCases` | 3 | Short audio, DC offset, zero signal |
| `TestIntegration` | 1 | Full analysis pipeline |

### Stage 3: Verify Build

Compiles Faust DSP and C++ code.

#### 3.1 Faust DSP Compilation

Tests all `.dsp` files compile without errors:

```bash
just test-faust
```

#### 3.2 Plugin Build

Full CMake build of the plugin:

```bash
just build
```

### Stage 4: Validate Audio Quality

Runs comprehensive module quality tests.

#### Module Tests (`test/test_framework.py`)

| Test | What It Checks |
|------|----------------|
| `compilation` | Module loads and lists parameters |
| `basic_render` | Renders audio without NaN/Inf |
| `audio_stability` | DC offset < 1%, clipping < threshold, not silent |
| `gate_response` | Gate parameter affects output (instruments) |
| `pitch_tracking` | V/Oct produces correct octave ratio |
| `parameter_sensitivity` | All parameters affect output |
| `regression` | Output matches baseline (if exists) |

#### Quality Thresholds

Default thresholds (can be overridden per-module in `test_config.json`):

| Threshold | Default | Description |
|-----------|---------|-------------|
| `thd_max_percent` | 15.0 | Maximum THD allowed |
| `clipping_max_percent` | 1.0 | Maximum clipping ratio |
| `hnr_min_db` | 0.0 | Minimum harmonic-to-noise ratio |
| `allow_hot_signal` | false | Allow higher output (wavefolders) |

### Stage 5: Validate Full Report

Generates detailed analysis report with spectrograms and AI assessment.

```bash
# Generate full report (requires GEMINI_API_KEY)
just test-report-full
```

Report includes:
- Audio spectrograms for each module
- THD measurements
- Harmonic analysis
- AI-powered subjective assessment (Gemini)
- Parameter sensitivity analysis

### Stage 6: Deploy

Deploys quality report to GitHub Pages (main branch only).

**Report URL**: `https://<username>.github.io/<repo>/`

### Running Locally

```bash
# VERIFICATION (structural correctness)
just verify-manifest         # Stage 1.1: Verify plugin.json
just verify-manifest-tests   # Stage 1.2: Manifest verification tests
just verify-test-infra       # Stage 2: Test infrastructure
just test-faust              # Stage 3.1: Faust DSP compilation
just build                   # Stage 3.2: Plugin build

# VALIDATION (audio quality)
just validate-modules        # Stage 4: Module audio tests
just test-report-full        # Stage 5: Full quality report

# COMBINED COMMANDS
just verify                  # All verification stages (fast, no build)
just verify-all              # Same as verify
just ci                      # Full CI pipeline locally (verify -> build -> validate)
just test                    # Same as ci
```

### Pull Request Checks

PRs trigger additional analysis:

| Job | Trigger | Description |
|-----|---------|-------------|
| `sensitivity` | PRs only | Full parameter sensitivity analysis |

### CI Configuration

The pipeline is defined in `.github/workflows/test.yml`.

#### Trigger Conditions

```yaml
on:
  push:
    branches: [main, develop]
    paths:
      - 'src/**/*.dsp'
      - 'src/**/*.cpp'
      - 'src/**/*.hpp'
      - 'faust/**'
      - 'test/**'
      - 'scripts/**'
      - 'plugin.json'
      - 'CMakeLists.txt'
      - 'Justfile'
      - '.github/workflows/test.yml'
  pull_request:
    branches: [main]
```

#### Required Secrets

| Secret | Required For | Description |
|--------|--------------|-------------|
| `GEMINI_API_KEY` | Stage 5 | AI-powered audio analysis |

### Failure Modes

| Stage | Failure | Effect |
|-------|---------|--------|
| 1. Verify Manifest | Invalid tags/trademarks | Blocks all subsequent stages |
| 2. Verify Test Infra | Test infrastructure broken | Blocks build |
| 3. Verify Build | Compilation error | Blocks validation |
| 4. Validate Audio | Module quality issues | Blocks report deployment |
| 5. Validate Report | Generation issues | Continues (non-blocking) |
| 6. Deploy | Pages deployment | Non-blocking |

### VCV Library Submission Checklist

Before submitting to VCV Library, ensure:

- [ ] `just verify-manifest` passes (no errors)
- [ ] `just verify-manifest-tests` passes (all 33 tests)
- [ ] `just verify-test-infra` passes (all 63 tests)
- [ ] `just build` succeeds
- [ ] `just validate-modules` passes
- [ ] No trademark names in module names/descriptions
- [ ] All tags are from official VCV tag list
- [ ] All modules in code are listed in plugin.json

---

## Adding a New Module

### Step-by-Step: Faust Module

#### 1. Create Faust DSP

Create `src/modules/MyModule/my_module.dsp`:

```faust
import("stdfaust.lib");

declare name "My Module";
declare author "WiggleRoom";
declare description "Description here";

// Parameters (alphabetically indexed)
amount = hslider("amount", 0.5, 0, 1, 0.01) : si.smoo;
freq = hslider("freq", 440, 20, 20000, 1) : si.smoo;

// DSP
process = os.osc(freq) * amount;
```

#### 2. Create C++ Wrapper

Create `src/modules/MyModule/MyModule.cpp`:

```cpp
#include "rack.hpp"
#include "FaustModule.hpp"
#define FAUST_MODULE_NAME MyModule
#include "my_module.hpp"

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct MyModule : FaustModule<VCVRackDSP> {
    enum ParamId { AMOUNT_PARAM, FREQ_PARAM, PARAMS_LEN };
    enum InputId { FREQ_CV_INPUT, INPUTS_LEN };
    enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    MyModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(AMOUNT_PARAM, 0.f, 1.f, 0.5f, "Amount");
        configParam(FREQ_PARAM, 20.f, 20000.f, 440.f, "Frequency", " Hz");
        configInput(FREQ_CV_INPUT, "Frequency CV");
        configOutput(AUDIO_OUTPUT, "Audio");

        // Faust params alphabetically: amount=0, freq=1
        mapParam(AMOUNT_PARAM, 0);
        mapParam(FREQ_PARAM, 1);
        mapCVInput(FREQ_CV_INPUT, 1, true);  // V/Oct
    }

    void process(const ProcessArgs& args) override {
        if (!initialized) {
            faustDsp.init(static_cast<int>(args.sampleRate));
            initialized = true;
        }

        updateFaustParams();

        float* nullInputs[1] = { nullptr };
        float output = 0.f;
        float* outputPtr = &output;

        faustDsp.compute(1, nullInputs, &outputPtr);
        outputs[AUDIO_OUTPUT].setVoltage(output * 5.f);
    }
};

struct MyModuleWidget : ModuleWidget {
    MyModuleWidget(MyModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/MyModule.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));

        float x = box.size.x / 2.f;
        addParam(createParamCentered<RoundBlackKnob>(Vec(x, 60), module, MyModule::FREQ_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(x, 120), module, MyModule::AMOUNT_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x, 180), module, MyModule::FREQ_CV_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(x, 240), module, MyModule::AUDIO_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelMyModule = createModel<WiggleRoom::MyModule, WiggleRoom::MyModuleWidget>("MyModule");
```

#### 3. Create CMakeLists.txt

Create `src/modules/MyModule/CMakeLists.txt`:

```cmake
add_faust_dsp(my_module.dsp my_module.hpp)

add_library(MyModule_Module STATIC MyModule.cpp ${FAUST_HEADER})
target_link_libraries(MyModule_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(MyModule_Module PRIVATE CommonLib)
endif()
target_include_directories(MyModule_Module PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/faust_gen)
```

#### 4. Register Module

In `src/plugin.cpp`:

```cpp
#ifdef HAS_MYMODULE
extern Model* modelMyModule;
#endif

void init(Plugin* p) {
    // ...
    #ifdef HAS_MYMODULE
    p->addModel(modelMyModule);
    #endif
}
```

In root `CMakeLists.txt`, add to the compile definitions section:

```cmake
target_compile_definitions(WiggleRoom PRIVATE HAS_MYMODULE)
```

#### 5. Add to plugin.json

```json
{
  "slug": "MyModule",
  "name": "My Module",
  "description": "Description here",
  "tags": ["Oscillator"]
}
```

#### 6. Create Panel SVG

Create `res/MyModule.svg` (6-12 HP width recommended).

#### 7. Add to Test Harness

In `test/faust_render.cpp`:

```cpp
// Add forward declaration
std::unique_ptr<AbstractDSP> createMyModule();

// Add to factory
if (moduleName == "MyModule") return createMyModule();

// Add to module list
return {"...", "MyModule"};

// Add to type detection if needed
if (name == "MyModule") return ModuleType::Instrument;
```

In `test/dsp_wrappers.cpp`:

```cpp
#undef __mydsp_H__

#define FAUST_MODULE_NAME MyModule
#include "my_module.hpp"
std::unique_ptr<AbstractDSP> createMyModule() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_MyModule::VCVRackDSP>>();
}
```

In `test/CMakeLists.txt`:

```cmake
set(FAUST_MODULES
    # ...existing modules...
    MyModule
)

# Add mapping
elseif(MODULE STREQUAL "MyModule")
    set(DSP_FILE "my_module.dsp")
    set(HPP_FILE "my_module.hpp")
endif()
```

#### 8. Build and Test

```bash
just build
python3 test/test_framework.py --module MyModule -v
just install
```

---

## Conventions Reference

### VCV Rack Voltage Standards

| Signal Type | Range | Notes |
|-------------|-------|-------|
| Audio | ±5V | 10Vpp |
| CV (unipolar) | 0-10V | Gates, envelopes |
| CV (bipolar) | ±5V | LFO, modulation |
| V/Oct | 0V = C4 | 1V/octave |
| Gate | 0/10V | 0 = off, 10 = on |
| Trigger | 0→10V | Rising edge |

### File Naming

| Type | Convention | Example |
|------|------------|---------|
| Module directory | PascalCase | `src/modules/MyModule/` |
| C++ file | PascalCase | `MyModule.cpp` |
| Faust DSP | snake_case | `my_module.dsp` |
| Generated header | snake_case | `my_module.hpp` |
| Panel SVG | PascalCase | `MyModule.svg` |

### Code Style

- Use `WiggleRoom` namespace for all module code
- Enum values use `SCREAMING_SNAKE_CASE`
- Member variables use `camelCase`
- Follow existing patterns in the codebase

---

## Agent-Based Development

WiggleRoom includes a multi-agent system for structured Faust module development. The agents automate verification, quality evaluation, and provide actionable fix instructions.

### Agent Definitions

Agent definitions for Claude Code are located in `.claude/agents/`:

| Agent | File | Purpose |
|-------|------|---------|
| **Verifier** | [`.claude/agents/verifier.md`](../.claude/agents/verifier.md) | Builds modules and runs comprehensive tests |
| **Judge** | [`.claude/agents/judge.md`](../.claude/agents/judge.md) | Evaluates results and generates fix instructions |
| **Faust Dev** | [`.claude/agents/faust-dev.md`](../.claude/agents/faust-dev.md) | Claude's role for applying DSP fixes |

### Agent Architecture

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│  Verifier Agent │────▶│   Judge Agent   │────▶│ Faust Dev Agent │
│                 │     │                 │     │    (Claude)     │
│ - Build         │     │ - Evaluate      │     │                 │
│ - Render tests  │     │ - Prioritize    │     │ - Read DSP      │
│ - Quality tests │     │ - Score         │     │ - Apply fixes   │
│ - Param sweep   │     │ - Generate fix  │     │ - Rebuild       │
│ - AI analysis   │     │   instructions  │     │                 │
└─────────────────┘     └─────────────────┘     └─────────────────┘
         │                                               │
         └───────────────────────────────────────────────┘
                        (iterate until pass)
```

### Agent Commands

```bash
# Single verification + judgment iteration
just agent-verify ModuleName

# Get fix instructions (for Claude to execute)
just agent-fix ModuleName

# Run full development loop (iterates until pass or max iterations)
just agent-loop ModuleName [iterations]

# Get JSON output for programmatic use
just agent-json ModuleName
```

### Development Loop Workflow

When Claude Code acts as the Faust Dev Agent:

1. **Run verification**: `just agent-fix ModuleName`
2. **Receive fix instructions** with priority issues and Faust code hints
3. **Read the DSP file** specified in the instructions
4. **Apply targeted fixes** one issue at a time (CRITICAL first, then HIGH)
5. **Rebuild**: `just build`
6. **Re-verify**: `just agent-verify ModuleName`
7. **Repeat** until verdict is PASS

### Severity Levels

| Severity | Action | Examples |
|----------|--------|----------|
| **CRITICAL** | Fix immediately | Build failure, silent output, severe clipping (>15%) |
| **HIGH** | Fix before release | Major clipping (5-15%), very low quality score (<60) |
| **MEDIUM** | Should fix | Parameter issues, AI-detected problems |
| **LOW** | Optional | Minor suggestions, optimizations |

### Implementation Files

The Python implementations that power the agents:

| File | Description |
|------|-------------|
| `test/agents/verifier_agent.py` | Verification logic and test execution |
| `test/agents/judge_agent.py` | Result evaluation and fix generation |
| `test/agents/orchestrator.py` | Development loop coordination |

### Programmatic Usage

```python
from test.agents import Orchestrator, run_development_loop

# Quick development loop
session = run_development_loop("ChaosFlute")

# Or with more control
orchestrator = Orchestrator(max_iterations=10)
judgment, verification = orchestrator.run_iteration("ChaosFlute", verbose=True)

if judgment.verdict != "pass":
    fix_instructions = orchestrator.generate_fix_instructions("ChaosFlute", judgment)
    print(fix_instructions.to_prompt())
```
