# EucSeq3

## Overview

EucSeq3 is a WiggleRoom module in the **Sequencers & Clocks** category. A 3-channel Euclidean sequencer with per-channel hits and steps, per-step CV values, probability gates, and an expander connection out to LogicMangler3.

It runs the same Euclidean engine as the 4-channel [EucSeq](EucSeq.md), in a narrower panel for patches that only need 3 rhythm streams.

## Signal Flow

- EucSeq3 is a sequencer, it does not process audio inline.
- Inputs accept Clock, Reset and Run, plus per-channel CV for Hits, Steps and Probability. A Scale Bus input carries scale information through the chain.
- Each of the 3 channels emits Gate, Trigger and CV outputs, intended to drive voices, drum modules or modulation destinations.

## Controls

| Control | Scope | Description |
|---------|-------|-------------|
| Master Speed | Global | Clock multiplier or divider for the whole module |
| Swing | Global | Shifts alternate steps late for shuffle feel |
| Steps | Per channel | Length of the Euclidean pattern |
| Hits | Per channel | Number of hits distributed across those steps |
| Quantise | Per channel | Quantises the CV output to the active scale |
| Probability | Per channel | Chance that a given hit actually fires |
| Retrigger | Per channel | Re-fires the trigger output on repeated hits |
| Bipolar | Per channel | Switches the CV output between unipolar and bipolar |
| Random CV | Per channel | Amount of randomisation applied to per-step CV |
| Scale | Per channel | Scales the CV output range |

## Typical Uses

- Drive 3 drum voices from the Gate outputs for layered Euclidean rhythms.
- Use the per-step CV output to send pitch or filter modulation in lockstep with the gate pattern.
- Chain into LogicMangler3 to apply truth-table logic to the gate streams, then into EucMix3 and EucBank3.

## Tips

- Slowly modulate Hits CV or Steps CV with an LFO for evolving Euclidean rotations.
- Set Probability below full and use Retrigger to get sparse patterns that still feel busy.
- Feed the Scale Bus from [TheArchitect](TheArchitect.md) to keep CV output in key across the whole chain.
