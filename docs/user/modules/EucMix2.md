# EucMix2

## Overview

EucMix2 is a WiggleRoom module in the **Sequencers & Clocks** category. A 2x2 CV summing matrix, usable standalone or as an expander for CV mixing in the [EucSeq2](EucSeq2.md) chain.

It is the 2-channel counterpart to [EucMix](EucMix.md), which is 4x4.

## Signal Flow

- EucMix2 sums control voltages, it is not an audio mixer.
- 2 CV inputs feed a grid of 4 attenuverters. Each output is the sum of every input scaled by its cell in that output's column.
- A Scale Bus input passes scale information through to downstream modules.
- 2 CV outputs carry the mixed result.

## Controls

| Control | Description |
|---------|-------------|
| Matrix cells | One attenuverter per input and output pair, 4 in total. Negative values invert that input's contribution |

## Typical Uses

- Cross-blend the per-step CV outputs of EucSeq2 so each voice tracks a weighted mix of all 2 patterns.
- Build a simple CV crossfade by setting one column to full and its neighbour to zero, then modulate between them.
- Invert one contribution to make two destinations move in opposition from a single source.

## Tips

- A diagonal of full-scale cells with everything else at zero passes each input straight through, which is a useful starting point.
- Because cells are attenuverters, summing an input with its own inversion cancels it. That is handy for carving notches out of a modulation source.
- Unpatched inputs contribute nothing, so a partly patched matrix behaves predictably.
