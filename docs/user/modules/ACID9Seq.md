# ACID9Seq

## Overview

ACID9Seq is a WiggleRoom module in the **Sequencers & Clocks** category. A companion sequencer for ACID9Voice with a planetary display, two "gears" of pattern data, and a Scale Bus link for diatonic note generation.

## Signal Flow

- ACID9Seq is a sequencer — it does not process audio inline.
- Inputs accept clock, reset, a 16-channel polyphonic Scale Bus, V/Oct transpose, freeze, and inject (randomize) triggers.
- Outputs supply 1 V/Oct pitch and gate, intended to drive a synth voice (ACID9Voice or any V/Oct-compatible voice).

## Typical Uses

- Drive an ACID9Voice or other V/Oct voice from the Pitch and Gate outputs.
- Send a Scale Bus from TheArchitect (or compatible) into the Scale Bus input to keep notes diatonic to the rest of the patch.
- Use Freeze and Inject as performance gestures to fix or randomize Gear B mid-sequence.

## Tips

- Modulate V/Oct Transpose with a slow LFO or sequenced CV for evolving key/chord shifts that still respect the Scale Bus.
- Use the planetary display as a visual cue for where in the cycle the sequencer is sitting.
