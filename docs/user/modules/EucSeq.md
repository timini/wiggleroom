# EucSeq

## Overview

EucSeq is a WiggleRoom module in the **Sequencers & Clocks** category. A 4-channel Euclidean sequencer with per-channel hits/steps, per-step CV values, probability gates, and an expander connection out to LogicMangler.

## Signal Flow

- EucSeq is a sequencer — it does not process audio inline.
- Inputs accept clock and per-channel CV for hits, steps, and probability.
- Each of the four channels emits Gate, Trigger, CV, and LFO outputs, intended to drive voices, drum modules, or modulation destinations.
- The LFO output is a unipolar 0V to 10V staircase that ramps across the pattern, reaching full scale on the last step. It tracks the step being played, so it stays in phase with that channel's Gate and CV.

## Typical Uses

- Drive four drum voices from the Gate outputs for layered Euclidean rhythms.
- Use the per-step CV output to send pitch or filter modulation in lockstep with the gate pattern.
- Daisy-chain into LogicMangler (as an expander) to apply truth-table logic to the gate streams.

## Tips

- Slowly modulate Hits CV or Steps CV with an LFO for evolving Euclidean rotations.
- Mute a channel's gate while leaving its CV output running to keep modulation without firing voices.
- Patch a channel's LFO output to a filter or VCA to sweep it in lockstep with that channel's pattern. Shorter patterns give faster sweeps.
