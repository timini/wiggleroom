# Faust Module Testing Framework

Automated testing system for WiggleRoom's Faust DSP modules. Detects parameter bugs, audio stability issues, and regressions before they reach users.

## Quick Start

```bash
# Run all module tests (verbose by default)
just test-modules

# Test a specific module
just test-modules module=ChaosFlute

# CI mode with JSON output
just test-modules-ci

# Generate baseline files for regression testing
just test-generate-baselines

# Run parameter sensitivity analysis with visual reports
just test-sensitivity
```

## Testing Strategy

### Philosophy

The testing framework validates that Faust DSP modules behave correctly without requiring manual listening tests. It catches common issues:

1. **Dead parameters** - Knobs that don't affect the sound
2. **Broken envelopes** - Gate/trigger signals that don't work
3. **Audio instability** - NaN, Inf, excessive clipping, DC offset
4. **Regressions** - Unintended changes to module behavior

### Test Pyramid

```
                    ┌─────────────┐
                    │  Regression │  ← Baseline comparison
                    ├─────────────┤
                 ┌──┤ Sensitivity │  ← All params affect output
                 │  ├─────────────┤
                 │  │    Gate     │  ← Envelope responds to gate
              ┌──┤  ├─────────────┤
              │  │  │  Stability  │  ← No NaN/Inf/clipping/DC
           ┌──┤  │  ├─────────────┤
           │  │  │  │   Render    │  ← Can generate audio
        ┌──┤  │  │  ├─────────────┤
        │  │  │  │  │ Compilation │  ← Module loads correctly
        └──┴──┴──┴──┴─────────────┘
```

Each layer builds on the previous. A module must pass compilation before render tests make sense.

## Commands Reference

### `just test-modules`

Runs the comprehensive test suite on all modules (verbose output by default).

```bash
just test-modules                    # Test all modules
just test-modules module=ChaosFlute  # Test specific module
```

**Output:**
```
Testing 11 module(s)...
  Testing: LadderLPF... OK
  Testing: BigReverb... OK
  ...
SUMMARY: 11/11 modules passed
```

**Exit codes:**
- `0` - All tests passed
- `1` - Some tests failed
- `2` - Error running tests (missing executable, etc.)

### `just test-modules-ci`

CI-optimized mode. Outputs JSON for machine parsing.

```bash
just test-modules-ci
```

**Output:** JSON report to stdout with structure:
```json
{
  "timestamp": "2024-01-15T10:30:00",
  "summary": {"total": 11, "passed": 11, "failed": 0},
  "modules": [
    {
      "name": "LadderLPF",
      "passed": true,
      "duration_ms": 1576,
      "tests": [
        {"name": "compilation", "passed": true, "message": "..."},
        ...
      ]
    }
  ]
}
```

### `just test-generate-baselines`

Creates baseline files for regression testing. Run this after confirming modules sound correct.

```bash
just test-generate-baselines
```

**Creates:** `test/baselines/<ModuleName>_baseline.json` for each module containing:
- Default parameter values
- Audio content hash
- Statistical features (RMS, peak, etc.)

### `just test-sensitivity`

Runs deep parameter sensitivity analysis with visual reports.

```bash
just test-sensitivity
```

**Creates:** `test/sensitivity/` directory with:
- `sensitivity_report.html` - Interactive report with charts
- `<ModuleName>_sensitivity.json` - Raw sensitivity scores
- Parameter sweep visualizations

### `just test-audio`

Generates spectrograms for visual inspection of all parameter combinations.

```bash
just test-audio                    # Quick test (3 values per param)
just test-audio-full               # Full grid (5 values per param)
just test-audio module=ChaosFlute  # Single module only
```

**Creates:** `test/output/` directory with:
- `wav/` - Rendered audio files
- `spectrograms/` - PNG spectrogram images
- `report.html` - Visual comparison grid with parameter visualization

### `just test-report`

Regenerates the HTML report from existing rendered files (no re-rendering).

```bash
just test-report
```

Use this to update the report HTML (e.g., after UI improvements) without waiting for audio to re-render.

### `just test-faust`

Verifies all Faust DSP files compile without errors.

```bash
just test-faust
```

Runs `faust` compiler on each `.dsp` file in `src/modules/`.

### `just test`

Runs the full test suite (Faust compilation + module tests).

```bash
just test
```

Equivalent to running `just test-faust` then `just test-modules`.

## Test Details

### 1. Compilation Test

**What it checks:** Module can be instantiated and queried for parameters.

**How it works:**
```bash
./faust_render --module ModuleName --list-params
```

**Failure indicates:** Faust compilation error, missing factory function, or crash during initialization.

### 2. Basic Render Test

**What it checks:** Module can process audio without crashing.

**How it works:**
- Renders 2 seconds of audio at 48kHz
- Generates appropriate input signal based on module type
- Checks output for NaN/Inf values

**Module types and their input signals:**

| Type | Modules | Input Signal |
|------|---------|--------------|
| Filter | LadderLPF, InfiniteFolder | Continuous 440Hz saw wave |
| Resonator | SpectralResonator | Periodic noise bursts (20ms every 300ms) |
| Effect | BigReverb, SaturationEcho, TriPhaseEnsemble | 100ms noise burst then silence |
| Instrument | ModalBell, PluckedString, ChaosFlute, SpaceCello, TheAbyss | Gate/trigger signal only |

### 3. Audio Stability Test

**What it checks:** Output audio quality metrics.

**Thresholds:**

| Metric | Threshold | Failure Indicates |
|--------|-----------|-------------------|
| DC Offset | > 1% | Missing DC blocker |
| Clipping | > 1% samples | Gain staging issue |
| Silence | > 95% samples | Broken audio path |

**Exception:** Wavefolders (InfiniteFolder) get 15% clipping threshold since they're designed to produce full-scale signals.

### 4. Gate Response Test

**What it checks:** Modules with `gate` parameter respond to gate signals.

**How it works:**
1. Render with `gate=0` (no trigger)
2. Render with `gate=max` (triggered)
3. Compare RMS levels

**Failure indicates:** Envelope not responding to gate. Common bug: `gate > 1.0` condition when gate maxes at 1.0.

**Skipped for:** Modules without a `gate` parameter (filters, effects).

### 5. Pitch Tracking Test

**What it checks:** V/Oct pitch tracking works correctly.

**How it works:**
1. Render at `volts=0` (base pitch, typically C4)
2. Render at `volts=1` (one octave up)
3. Detect fundamental frequency using autocorrelation
4. Verify frequency ratio is approximately 2.0 (±15%)

**Failure indicates:** Pitch not tracking V/Oct correctly. Could be:
- `volts` parameter not connected to frequency calculation
- Wrong scaling (should be `freq = base * 2^volts`)
- Frequency clamping cutting off high notes

**Skipped for:** Filters, effects, and modules without `volts` parameter.

### 6. Parameter Sensitivity Test

**What it checks:** Every parameter affects the output.

**How it works:**
For each parameter (excluding gate/trigger/volts):
1. Render with parameter at minimum
2. Render with parameter at maximum
3. Compare audio hash and RMS

**Failure indicates:** Dead parameter - knob does nothing. Could be:
- Parameter not connected in DSP code
- Parameter range too small to matter
- Bug in parameter routing

### 7. Regression Test

**What it checks:** Output matches saved baseline.

**How it works:**
1. Load baseline file (hash + stats)
2. Render with same parameters
3. Compare hash (exact match) or stats (fuzzy match)

**Failure indicates:** Module behavior changed. Could be intentional (update baseline) or accidental (bug introduced).

**Skipped if:** No baseline file exists for module.

## C++ Renderer Reference

The `faust_render` executable renders audio from any Faust module.

### Basic Usage

```bash
# Render with defaults
./build/test/faust_render --module LadderLPF --output test.wav

# Set duration and sample rate
./build/test/faust_render --module TheAbyss --output test.wav \
    --duration 4.0 --sample-rate 96000

# Set parameters
./build/test/faust_render --module ChaosFlute --output test.wav \
    --param pressure=0.8 --param growl=0.5 --param reverb=0.3
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `--module NAME` | Module to render (required) | - |
| `--output FILE` | Output WAV file | output.wav |
| `--duration SECS` | Duration in seconds | 2.0 |
| `--sample-rate RATE` | Sample rate in Hz | 48000 |
| `--param NAME=VALUE` | Set parameter (repeatable) | - |
| `--list-modules` | List available modules | - |
| `--list-params` | List module parameters | - |
| `--no-auto-gate` | Disable automatic gate handling | false |

### Examples

```bash
# List all modules
./build/test/faust_render --list-modules

# List parameters for a module
./build/test/faust_render --module PluckedString --list-params

# Render a bright, short pluck
./build/test/faust_render --module PluckedString --output pluck.wav \
    --param damping=0.1 --param brightness=0.9 --param position=0.2

# Render without auto-gate (for testing gate manually)
./build/test/faust_render --module ModalBell --output bell.wav \
    --param gate=1.0 --no-auto-gate
```

## Sensitivity Analysis

The sensitivity analyzer (`test/analyze_sensitivity.py`) provides deeper parameter analysis.

### How It Works

1. For each parameter, sweep from min to max (11 steps)
2. Hold all other parameters at default values
3. Extract audio features at each step:
   - RMS (loudness)
   - Spectral centroid (brightness)
   - Spectral bandwidth (spread)
   - Spectral rolloff (high frequency content)
   - Zero crossing rate (noisiness)
   - Spectral flux (change over time)
4. Compute sensitivity score as coefficient of variation across features
5. Flag parameters with score < 0.05 as potentially broken

### Interpreting Results

| Score | Interpretation |
|-------|----------------|
| > 0.5 | High sensitivity - parameter has major effect |
| 0.1 - 0.5 | Normal sensitivity - parameter works correctly |
| 0.05 - 0.1 | Low sensitivity - subtle effect, may be intentional |
| < 0.05 | Very low - likely broken or range too small |

### Running Analysis

```bash
# Analyze all modules
python3 test/analyze_sensitivity.py --output test/sensitivity

# Analyze specific module
python3 test/analyze_sensitivity.py --module ChaosFlute --output test/sensitivity
```

## CI Integration

### GitHub Actions

The `.github/workflows/test.yml` workflow runs on every push:

```yaml
- name: Run module tests
  run: python3 test/test_framework.py --ci --output test/results.json

- name: Check test results
  run: |
    python3 -c "
    import json
    with open('test/results.json') as f:
        results = json.load(f)
    if results['summary']['failed'] > 0:
        exit(1)
    "
```

### Adding to Your CI

```bash
# Install dependencies
pip install numpy scipy librosa

# Build
cmake -B build && cmake --build build

# Run tests (exit code indicates pass/fail)
python3 test/test_framework.py --ci --output results.json
```

## Adding Tests for New Modules

When you add a new Faust module:

1. **Add factory function** to `test/faust_render.cpp`:
   ```cpp
   std::unique_ptr<AbstractDSP> createMyModule();
   ```

2. **Register in factory**:
   ```cpp
   if (moduleName == "MyModule") return createMyModule();
   ```

3. **Add to module list**:
   ```cpp
   return {"LadderLPF", ..., "MyModule"};
   ```

4. **Set module type** (for input signal generation):
   ```cpp
   if (name == "MyModule") {
       return ModuleType::Instrument;  // or Filter, Resonator, Effect
   }
   ```

5. **Run tests**:
   ```bash
   just build
   just test-modules
   ```

6. **Generate baseline** (after confirming it sounds correct):
   ```bash
   just test-generate-baselines
   ```

## Troubleshooting

### "Executable not found"

Build the project first:
```bash
just build
```

### "Module not found"

Check the module is registered:
```bash
./build/test/faust_render --list-modules
```

### Test fails but module sounds fine

Some failures are false positives:

- **Clipping on wavefolders** - Add to `HOT_SIGNAL_MODULES` in test_framework.py
- **Low sensitivity on subtle params** - May be intentional, adjust threshold
- **Gate test on self-oscillating modules** - Some modules produce sound without gate

### Sensitivity analysis shows 0.000

Common causes:
1. `gate > 1.0` bug - gate threshold too high
2. Parameter not connected in Faust code
3. Parameter affects inaudible frequencies
4. Parameter only affects transient (use longer duration)

## File Structure

```
test/
├── README.md                 # This file
├── faust_render.cpp          # C++ audio renderer
├── AbstractDSP.hpp           # DSP interface for Faust modules
├── dsp_wrappers.cpp          # Factory functions for each module
├── test_framework.py         # Main test runner
├── analyze_sensitivity.py    # Parameter sensitivity analyzer
├── analyze_audio.py          # Spectrogram generator
├── run_tests.py              # Batch rendering orchestrator
├── requirements.txt          # Python dependencies
├── CMakeLists.txt            # Build configuration
├── baselines/                # Regression test baselines
│   └── <Module>_baseline.json
├── output/                   # Generated test artifacts
│   ├── wav/                  # Rendered audio files
│   └── spectrograms/         # Visual spectrograms
└── sensitivity/              # Sensitivity analysis reports
    └── sensitivity_report.html
```
