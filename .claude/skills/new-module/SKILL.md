---
name: new-module
description: Create a new VCV Rack module (Faust DSP or native C++) using architect → builder → verify/fix cycle
argument-hint: <ModuleName> [description]
---

# Create New Module: $ARGUMENTS

## Workflow

This skill creates a new VCV Rack module through a multi-phase agent cycle.

### Phase 0: Determine Implementation Type

**IMPORTANT:** Before starting implementation, determine whether this module should be:

| Type | Use When | Examples |
|------|----------|----------|
| **Faust DSP** | Complex audio DSP, physical modeling, filters, effects with many parameters | Filters, reverbs, synth voices, physical models |
| **Native C++** | Simple utilities, CV processing, logic, sequencers, or when Faust adds unnecessary complexity | Crossfaders, mixers, gates, triggers, clock dividers |

**Ask the user if unclear.** Simple CV utilities (mixing, crossfading, logic) should almost always be native C++. Complex audio processing benefits from Faust's DSP primitives and automatic parameter handling.

For **native C++ modules**:
- Skip the Faust-specific steps (no .dsp file, no test infrastructure updates)
- No test_config.json needed (test framework is Faust-specific)
- Simpler CMakeLists.txt (no `add_faust_dsp()`)

### Phase 1: Create Feature Branch

```bash
git checkout -b feature/{module-name-lowercase}
```

This ensures all module development happens on an isolated branch.

### Phase 2: Save Specification

First, save the module specification to `specs/{ModuleName}.md`:

```markdown
# {ModuleName} Module Specification

## Overview
{Brief description of what the module does}

## Type
[instrument / filter / effect / resonator / utility]

## Sonic Character
{Describe the sound - warm, harsh, ethereal, punchy, etc.}

## Parameters
- **param1**: Description (range: 0-1, default: 0.5)
- **param2**: Description

## Inputs
- **gate**: Gate signal for triggering
- **volts**: V/Oct pitch CV
- [other inputs]

## Outputs
- **out_l/out_r**: Stereo audio output

## Inspiration / References
{Any DSP algorithms, synths, or effects to reference}

## Special Requirements
{Any specific technical requirements}
```

### Phase 3: Design (Architect) - Faust modules only

For Faust modules, spawn the architect agent:

```
Task tool:
  subagent_type: "module-dev:architect"
  description: "Design {ModuleName}"
  prompt: "Read the specification at specs/{ModuleName}.md and create a detailed technical design for implementation."
```

For native C++ modules, skip this phase and implement directly.

### Phase 4: Build (Builder)

For Faust modules, spawn the builder agent:

```
Task tool:
  subagent_type: "module-dev:builder"
  description: "Build {ModuleName}"
  prompt: "Create all files for {ModuleName} based on this design:\n\n{ARCHITECT_OUTPUT}"
```

For native C++ modules, create the files directly (see File Checklist below).

### Phase 5: Verify/Fix Loop (Faust modules only)

Run the standard module-dev verification loop:

```
Task tool:
  subagent_type: "module-dev:verifier"
  description: "Verify {ModuleName}"
  prompt: "Verify the {ModuleName} module. Return metrics and verdict."
```

If NEEDS_WORK:
```
Task tool:
  subagent_type: "module-dev:judge"
  description: "Judge {ModuleName}"
  prompt: "Evaluate these verification results:\n\n{VERIFIER_OUTPUT}"
```

Then fix and repeat until PASS.

## Quick Reference - Just Commands

| Command | Purpose |
|---------|---------|
| `just build` | Build the plugin |
| `just validate {MODULE}` | Full validation |
| `just faceplate {MODULE}` | Generate panel artwork |
| `just install` | Install to VCV Rack |
| `just run` | Install and launch Rack |

## File Checklist

### All Modules (Faust and Native C++)
**New Files:**
- [ ] `src/modules/{ModuleName}/{ModuleName}.cpp` - Module implementation
- [ ] `src/modules/{ModuleName}/CMakeLists.txt` - Build config
- [ ] `res/{ModuleName}.png` - Faceplate (generate with `just faceplate`)

**Updated Files:**
- [ ] `src/plugin.cpp` - extern + addModel (with HAS_* guard)
- [ ] `CMakeLists.txt` - HAS_* compile definition
- [ ] `plugin.json` - module entry
- [ ] `README.md` - add to module table and update count
- [ ] `scripts/generate_faceplate.py` - HP width

### Faust Modules Only (additional files)
**New Files:**
- [ ] `src/modules/{ModuleName}/{lowercase}.dsp` - Faust DSP code
- [ ] `src/modules/{ModuleName}/test_config.json` - Test configuration

**Updated Files:**
- [ ] `test/faust_render.cpp` - factory declaration
- [ ] `test/dsp_wrappers.cpp` - DSP wrapper
- [ ] `test/CMakeLists.txt` - module mapping

## Example Usage

```
/new-module ChaosSynth A chaotic subtractive synthesizer with Lorenz attractor modulation
```

This will:
1. Create branch `feature/chaossynth`
2. Save spec to `specs/ChaosSynth.md`
3. Architect designs the module
4. Builder creates all files
5. Verifier/Judge/Dev loop until passing

After module passes verification, you can create a PR:
```bash
git add .
git commit -m "feat: Add ChaosSynth module"
git push -u origin feature/chaossynth
gh pr create --title "Add ChaosSynth module" --body "..."
```

## Module Types Reference

| Type | Typical Inputs | Typical Outputs | THD Threshold |
|------|----------------|-----------------|---------------|
| instrument | gate, volts, velocity | out_l, out_r | 15-100% |
| filter | in_l, in_r, cutoff_cv | out_l, out_r | 5-15% |
| effect | in_l, in_r, mix_cv | out_l, out_r | varies |
| resonator | excite, freq_cv | out_l, out_r | 15-50% |
| utility | various | various | N/A |
