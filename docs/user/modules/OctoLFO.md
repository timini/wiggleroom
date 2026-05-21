# OctoLFO

## Overview

OctoLFO is a WiggleRoom module in the **Utilities** category. An 8-channel clock-synced LFO with per-channel waveshaping (Skew, Curve, Fold, Scale, Phase NSEW) and a mini scope per channel.

## Signal Flow

- OctoLFO is a modulation source — it does not process audio inline.
- Inputs accept a clock and a reset.
- Each of the eight channels emits an LFO output, intended to modulate parameters on other modules.

## Typical Uses

- Drive filter cutoffs, oscillator FM, panner positions, or any CV-controllable parameter across eight related destinations.
- Use the NSEW phase offsets to create quadrature relationships between channels (good for stereo motion, vector morphing).
- Save patch variations to compare wave-shape settings against a fixed clock division.

## Tips

- Use attenuators on the LFO outputs to keep modulation depth musical.
- Set Phase NSEW on a pair of channels for quadrature stereo motion.
