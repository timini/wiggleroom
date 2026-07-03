# PreFlightClock

## Overview

PreFlightClock is a WiggleRoom module in the **Sequencers & Clocks** category. Master clock with an Ableton-style count-in: fires a reset, plays four metronome beeps, then starts the run on the downbeat.

## Signal Flow

- PreFlightClock is a clock generator — it does not process audio inline.
- Inputs accept BPM CV (0–10 V = 30–300 BPM), play trigger, and stop trigger.
- Outputs provide a metronome click, reset pulse, run gate, and multiplied/divided clocks (x1/x2/x3/x4/x8/x16/x32 and /1.5 / /2 / /3 / /4 / /8 / /16 / /32).

## Typical Uses

- Use as the master clock for a sequenced patch; route the run gate to start/stop downstream sequencers cleanly on the downbeat.
- Drive multiple time-base destinations from the divider outputs (e.g. x4 for hats, /4 for chord changes).
- Send the metronome output to an audio mixer when recording so takes line up with the count-in.

## Tips

- The reset fires before the count-in begins, so sequencers reset to step 1 in time for the first beat.
- Modulate BPM CV slowly with an LFO for tempo wobble; instantaneous jumps can produce uneven first ticks.
