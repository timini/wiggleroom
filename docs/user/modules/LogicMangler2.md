# LogicMangler2

## Overview

LogicMangler2 is a WiggleRoom module in the **Sequencers & Clocks** category. A 2-input truth table logic processor with cell locking, density control and randomisation. It works standalone or as an expander for [EucSeq2](EucSeq2.md).

It runs the same truth table engine as the 4-channel [LogicMangler](LogicMangler.md), sized for 2 gate streams. With 2 inputs the table has 4 rows, one per input combination.

## Signal Flow

- LogicMangler2 processes gates and triggers, not audio.
- Inputs accept 2 Gate signals plus per-channel Probability CV.
- Outputs emit a processed Gate and Trigger per channel.
- Placed to the right of EucSeq2 it picks up that module's gates over the expander connection, so the Gate inputs can be left unpatched.

## Controls

| Control | Scope | Description |
|---------|-------|-------------|
| Random | Global | Randomises the whole truth table, skipping locked cells |
| Mutate | Global | Flips a small number of cells rather than rerolling everything |
| Undo / Redo | Global | Steps back and forward through table changes |
| Probability | Per channel | Chance that an output gate passes |
| Density | Per channel | Biases the randomiser toward sparser or busier tables |
| Retrigger | Per channel | Re-fires the trigger output on repeated hits |

## Typical Uses

- Derive counter-rhythms from an existing Euclidean pattern instead of programming a second sequencer.
- Lock the cells you like, then hit Random repeatedly to explore variations around them.
- Use Density to move a pattern between sparse accents and near-constant gates without touching the source.

## Tips

- Mutate is the control to reach for when a pattern is close but not quite there. Random throws away everything unlocked.
- Undo and Redo make the randomiser safe to explore with, so audition freely.
- Standalone, patch any gate source into the Gate inputs. The truth table does not care where the gates came from.
