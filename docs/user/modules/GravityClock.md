# GravityClock

## Overview

GravityClock is a WiggleRoom module in the **Sequencers & Clocks** category. Clock-synced bouncing-ball physics that emits an LFO tracking the ball's position and an impact trigger when it strikes the ground, locked to musical divisions.

## Signal Flow

- GravityClock is a modulation/trigger generator — it does not process audio inline.
- Inputs accept a clock and a ratio CV.
- Outputs are the ball-position LFO and an impact trigger.

## Typical Uses

- Patch the impact trigger into a drum voice or envelope generator for ratcheting, decaying hit patterns.
- Use the position LFO to modulate filter cutoffs or pan for movement that decays with each bounce.
- Combine with another clock divider to layer impacts against a straight grid.

## Tips

- Modulate the Ratio CV slowly with an LFO for ball physics that breathe over time.
- Use attenuators on the LFO output to keep modulation depth musical.
