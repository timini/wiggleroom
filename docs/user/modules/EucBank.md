# EucBank

## Overview

EucBank is a WiggleRoom module in the **Sequencers & Clocks** category. A 16-slot pattern storage and recall manager for the EucSeq + LogicMangler expander chain.

## Signal Flow

- EucBank is a pattern bank — it has no audio or CV outputs of its own; it stores and recalls patterns on neighbouring modules via the expander connection.
- Inputs accept Step and Reset to advance and reset the active bank slot.
- Bank changes are communicated to adjacent EucSeq / LogicMangler modules through the WiggleRoom expander side-bus.

## Typical Uses

- Step through 16 stored patterns over the course of a song using a slow clock divider.
- Reset to slot 1 at song-section boundaries for predictable recall.
- Build a song-mode controller by sequencing the Step input from another sequencer's CV/trigger output.

## Tips

- Place EucBank directly next to EucSeq (and LogicMangler if used) so the expander bus connects.
- Drive Step with a clocked trigger source rather than continuous CV.
