# XFade Module Specification

## Overview
A simple CV crossfader utility that takes two CV inputs and mixes them with a crossfade control. The crossfader position determines the mix ratio such that the outputs always sum to standard CV ranges. Both inputs are multiplied by complementary factors (A × (1-x) + B × x where x is the crossfade position), ensuring smooth transitions and predictable output levels.

## Type
utility

## Sonic Character
Clean, transparent utility module - no coloration or processing beyond the mixing operation.

## Parameters
- **xfade**: Crossfade position (range: 0-1, default: 0.5). At 0 = 100% input A, at 1 = 100% input B, at 0.5 = 50% each.

## Inputs
- **in_a**: CV input A (±10V range)
- **in_b**: CV input B (±10V range)
- **xfade_cv**: CV control for crossfade position (0-10V maps to 0-1, with attenuverter)

## Outputs
- **out**: Mixed CV output

## Algorithm
Simple linear crossfade:
```
output = in_a * (1 - xfade) + in_b * xfade
```

Where xfade is the sum of the knob position and the CV input (clamped to 0-1).

## Special Requirements
- No audio rate processing needed - this is a CV utility
- Should handle DC signals cleanly (no DC blocking)
- Crossfade CV input should be normalized to work with standard 0-10V CV
- Small HP width (4HP) since it's a simple utility
