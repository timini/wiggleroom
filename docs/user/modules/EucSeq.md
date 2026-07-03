# EucSeq

## Overview

EucSeq is a WiggleRoom module in the **Sequencers & Clocks** category. A 4-channel Euclidean sequencer with per-channel hits/steps, per-step CV values, probability gates, and an expander connection out to LogicMangler.

## Signal Flow

- EucSeq is a sequencer — it does not process audio inline.
- Inputs accept clock and per-channel CV for hits, steps, and probability.
- Each of the four channels emits Gate, Trigger, LFO, and CV outputs, intended to drive voices, drum modules, or modulation destinations.

## Typical Uses

- Drive four drum voices from the Gate outputs for layered Euclidean rhythms.
- Use the per-step CV output to send pitch or filter modulation in lockstep with the gate pattern.
- Daisy-chain into LogicMangler (as an expander) to apply truth-table logic to the gate streams.

## Tips

- Slowly modulate Hits CV or Steps CV with an LFO for evolving Euclidean rotations.
- Mute a channel's gate while leaving its CV output running to keep modulation without firing voices.
