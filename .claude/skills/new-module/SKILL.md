---
name: new-module
description: Create a new Faust DSP module from a specification using architect → builder → verify/fix cycle
argument-hint: <ModuleName> [description]
---

# Create New Module: $ARGUMENTS

## Workflow

This skill creates a new Faust DSP module through a 3-phase agent cycle.

### Phase 1: Save Specification

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

### Phase 2: Design (Architect)

Spawn the architect agent to create a detailed technical design:

```
Task tool:
  subagent_type: "module-dev:architect"
  description: "Design {ModuleName}"
  prompt: "Read the specification at specs/{ModuleName}.md and create a detailed technical design for implementation."
```

Review the design. If it needs changes, iterate with the architect.

### Phase 3: Build (Builder)

Spawn the builder agent to create all files:

```
Task tool:
  subagent_type: "module-dev:builder"
  description: "Build {ModuleName}"
  prompt: "Create all files for {ModuleName} based on this design:\n\n{ARCHITECT_OUTPUT}"
```

### Phase 4: Verify/Fix Loop

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

The builder must create/update these files:

### New Files
- [ ] `src/modules/{ModuleName}/{lowercase}.dsp` - Faust DSP
- [ ] `src/modules/{ModuleName}/{ModuleName}.cpp` - VCV wrapper
- [ ] `src/modules/{ModuleName}/CMakeLists.txt` - Build config
- [ ] `src/modules/{ModuleName}/test_config.json` - Test config

### Updated Files
- [ ] `src/plugin.cpp` - extern + addModel
- [ ] `CMakeLists.txt` - HAS_* definition
- [ ] `plugin.json` - module entry
- [ ] `test/faust_render.cpp` - factory declaration
- [ ] `test/dsp_wrappers.cpp` - DSP wrapper
- [ ] `scripts/generate_faceplate.py` - HP width (for faceplate)

## Example Usage

```
/new-module ChaosSynth A chaotic subtractive synthesizer with Lorenz attractor modulation
```

This will:
1. Save spec to `specs/ChaosSynth.md`
2. Architect designs the module
3. Builder creates all files
4. Verifier/Judge/Dev loop until passing

## Module Types Reference

| Type | Typical Inputs | Typical Outputs | THD Threshold |
|------|----------------|-----------------|---------------|
| instrument | gate, volts, velocity | out_l, out_r | 15-100% |
| filter | in_l, in_r, cutoff_cv | out_l, out_r | 5-15% |
| effect | in_l, in_r, mix_cv | out_l, out_r | varies |
| resonator | excite, freq_cv | out_l, out_r | 15-50% |
| utility | various | various | N/A |
