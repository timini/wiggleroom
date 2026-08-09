# EucBank3

## Overview

EucBank3 is a WiggleRoom module in the **Sequencers & Clocks** category. Sixteen-slot pattern storage and recall for the 3-channel EucLogic expander chain.

It is the 3-channel counterpart to [EucBank](EucBank.md).

## Signal Flow

- EucBank3 stores and recalls patterns. It has no signal outputs of its own.
- Inputs accept Step and Reset, so slot selection can be advanced from a clock or reset to the first slot.
- It reads and writes the state of the connected [EucSeq3](EucSeq3.md) chain over the expander connection.

## Controls

| Control | Description |
|---------|-------------|
| Bank | Selects which of the sixteen slots is active |
| Save | Writes the current chain state into the selected slot |
| Load | Recalls the selected slot into the chain |

## Typical Uses

- Store a verse pattern and a chorus pattern, then switch between them mid-performance.
- Patch a slow clock into Step to walk through slots automatically for a long-form arrangement.
- Capture happy accidents from LogicMangler3's randomiser before exploring further.

## Tips

- Reset returns to the first slot, so a song-position reset can restore the opening pattern.
- Save overwrites without confirmation. Step to an empty slot first if you want to keep what is loaded.
- Slots persist with the patch, so a saved bank travels with the file.
