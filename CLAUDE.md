# WiggleRoom - VCV Rack Plugin

## Development Workflow

Always use feature-dev agents for feature development tasks. When implementing new features or making significant changes to the codebase, invoke the `/feature-dev` skill to ensure guided feature development with proper codebase understanding and architecture focus.

**For comprehensive development documentation, see [DEVELOPMENT.md](DEVELOPMENT.md).**

## Project Structure

```
WiggleRoom/
├── cmake/                    # CMake helpers & toolchains
│   ├── RackSDK.cmake         # SDK finder/downloader
│   └── Toolchain-*.cmake     # Cross-compilation toolchains
├── faust/                    # Faust architecture file
│   └── vcvrack.cpp           # VCVRackDSP wrapper template
├── res/                      # SVG panel graphics
├── src/
│   ├── common/               # Shared utilities
│   │   ├── DSP.hpp           # DSP utilities (V/Oct, smoothing)
│   │   └── FaustModule.hpp   # Base class for Faust modules
│   ├── modules/              # Auto-discovered modules
│   │   └── ModuleName/
│   │       ├── ModuleName.cpp
│   │       ├── CMakeLists.txt
│   │       └── module_name.dsp  # (Faust modules only)
│   └── plugin.cpp            # Plugin entry point
├── test/                     # Testing framework
│   ├── agents/               # Multi-agent development system
│   │   ├── verifier_agent.py # Build and test runner
│   │   ├── judge_agent.py    # Quality evaluator
│   │   ├── orchestrator.py   # Development loop coordinator
│   │   └── faust_dev_agent.md # Claude instructions
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

## Testing

```bash
# Run all tests (includes audio quality tests)
python3 test/test_framework.py

# Test specific module
python3 test/test_framework.py --module ModuleName -v

# Run tests without audio quality analysis (faster)
just test-modules-fast

# Audio quality analysis (THD, aliasing, harmonics, envelope)
just test-quality ModuleName

# Generate JSON quality report
just test-quality-report ModuleName

# List module parameters
./build/test/faust_render --module ModuleName --list-params
```

See [DEVELOPMENT.md](DEVELOPMENT.md#testing-framework) for full testing documentation.

## Iterative Module Development

When debugging or improving Faust DSP modules, use this systematic approach:

```bash
# 1. Baseline: Run all quality tests
python3 test/audio_quality.py --module ModuleName --report -v

# 2. Get AI assessment (includes DSP context for artistic intent)
GEMINI_API_KEY=key python3 test/ai_audio_analysis.py --module ModuleName -v

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

## Agent-Based Development System

The project includes a multi-agent system for structured Faust module development. This provides automated verification, quality judgment, and actionable fix instructions.

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

**For Claude Code (acting as Faust Dev Agent):**

1. **Run verification**: `just agent-fix ModuleName`
2. **Receive fix instructions** with priority issues and Faust code hints
3. **Read the DSP file** specified in the instructions
4. **Apply targeted fixes** one issue at a time (CRITICAL first, then HIGH)
5. **Rebuild**: `just build`
6. **Re-verify**: `just agent-verify ModuleName`
7. **Repeat** until verdict is PASS

**Example fix instructions output:**
```markdown
# Fix Instructions for ChaosFlute

DSP File: `src/modules/ChaosFlute/chaos_flute.dsp`

## Priority Issues to Fix

### 1. [CRITICAL] clipping
**Issue:** Severe clipping: 32.0% of samples
**Fix:** Add frequency-dependent gain compensation
**Faust hint:**
freq_compensation = min(1.0, freq / 261.62);
signal = raw_signal * freq_compensation;
```

### Severity Levels

| Severity | Action | Description |
|----------|--------|-------------|
| CRITICAL | Fix immediately | Clipping, silent output, build failures |
| HIGH | Fix before release | Major quality issues, parameter problems |
| MEDIUM | Should fix | AI-detected issues, minor quality problems |
| LOW | Optional | Suggestions, minor improvements |

### Common Fix Patterns (Reference)

**Clipping:**
```faust
// Reduce output gain
output_gain = 0.5;

// Add soft limiter
output = signal : ma.tanh : *(output_gain);

// Frequency-dependent compensation (waveguide models)
freq_compensation = min(1.0, freq / 261.62);
```

**Transient Clicks:**
```faust
// Smooth gate transitions
gate_smooth = gate : si.smooth(0.995);

// Smooth envelope output
envelope = en.asr(a, s, r, gate) : si.smooth(0.99);
```

**DC Offset:**
```faust
// Add DC blocker
output = signal : fi.dcblocker;
```

**Silent Output:**
```faust
// Ensure minimum excitation
min_excitation = 0.05;
excitation = min_excitation + param * (max_excitation - min_excitation);
```

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

### Files

| File | Description |
|------|-------------|
| `test/agents/verifier_agent.py` | Runs builds and all test suites |
| `test/agents/judge_agent.py` | Evaluates results, generates fix instructions |
| `test/agents/orchestrator.py` | Coordinates the development loop |
| `test/agents/faust_dev_agent.md` | Instructions for Claude as Faust Dev Agent |

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
   - `res/MyModule.svg` (panel)

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

4. **Add to tests:**
   - `test/faust_render.cpp`: Factory declaration and function
   - `test/dsp_wrappers.cpp`: DSP wrapper
   - `test/CMakeLists.txt`: Module mapping

5. **Build and test:**
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
