# {ModuleName} Module Specification

## Overview

{Brief description of what the module does - 1-2 sentences}

## Type

{instrument / filter / effect / resonator / utility}

## HP Width

{8 / 12 / 16 / 20} HP

## Sonic Character

{Describe the sound in musical terms:}
- Warm, cold, harsh, soft, ethereal, punchy, aggressive, gentle
- Analog-style, digital, lo-fi, pristine, gritty
- Any specific tonal qualities

## Parameters

List the user-controllable parameters:

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| param1 | What it controls | 0-1 | 0.5 |
| param2 | What it controls | 20-20000 Hz | 1000 |

## Inputs

| Input | Type | Description |
|-------|------|-------------|
| gate | Control (0-10V) | Gate/trigger signal |
| volts | V/Oct | Pitch CV (0V = C4) |
| cv1 | CV | Modulation for param1 |

## Outputs

| Output | Type | Description |
|--------|------|-------------|
| out_l | Audio | Left channel |
| out_r | Audio | Right channel |

## Algorithm / DSP Approach

{Describe the DSP algorithm to use:}
- Physical modeling (Karplus-Strong, waveguide, modal)
- Subtractive (oscillator → filter → amp)
- FM/PM synthesis
- Granular, spectral, additive
- Specific techniques or papers to reference

## Inspiration / References

{List any existing synths, effects, or modules that inspire this:}
- "Similar to Mutable Instruments Rings"
- "Based on the Juno-60 chorus"
- "Inspired by Buchla Easel"

## Special Requirements

{Any specific technical requirements:}
- Must be polyphonic-ready
- Low CPU usage priority
- Specific frequency response
- Particular envelope behavior

## Test Scenarios

{Describe how to test this module:}
1. Default settings - should produce {expected sound}
2. Extreme settings - should {behavior}
3. With modulation - should {behavior}
