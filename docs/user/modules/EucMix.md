# EucMix

## Overview

EucMix is a WiggleRoom module in the **Sequencers & Clocks** category. A 4×4 CV summing matrix that works standalone or as a CV-mixing expander for the EucSeq chain.

## Signal Flow

- EucMix is a CV mixer — it is designed for control-rate CV, not audio-rate signals.
- Inputs accept four CV sources.
- Outputs supply four mixed CV signals, each a configurable sum of the inputs.

## Typical Uses

- Sum and route CV from up to four sources into four destinations with independent weightings.
- Use as the CV-mix expander adjacent to EucSeq to combine its per-channel CV outputs.
- Mix LFO, sequencer CV, and envelope sources to drive complex modulation on a single destination.

## Tips

- Keep individual contributions modest — summed CV can easily exceed ±10 V if all inputs are at full scale.
- Use attenuators upstream when you want fine control over the contribution of a noisy CV source.
