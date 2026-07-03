# SpectraHenge

## Overview

SpectraHenge is a WiggleRoom module in the **Filters & Effects** category. A four-node spectral processor with stereo audio inputs, per-node X/Y CV, two global LFO inputs, and a send/return loop for inserting external effects inside the processing chain.

## Signal Flow

- Patch mono or stereo audio into the four input pairs; SpectraHenge processes them through its spectral nodes and outputs the result.
- Send a node's audio out through the **Send** jack to an external effect, then bring it back through **Return L/R** to fold the processed signal back into SpectraHenge's mix.
- CV inputs modulate the X/Y position of each node independently; LFO A/B add global X and Y drift across all nodes.

## Typical Uses

- Use as a multi-input spectral mixer where each input occupies a movable region of the spectrum.
- Insert a reverb, delay, or filter in the send/return loop to colour SpectraHenge's internal signal path.
- Animate the node positions with LFOs or sequenced CV for evolving, location-based timbral shifts.

## Tips

- Keep node Q moderate when sending heavy material through the return loop to avoid resonant build-up.
- Use the global LFO inputs for slow movement and per-node CV for fast, targeted shifts.
