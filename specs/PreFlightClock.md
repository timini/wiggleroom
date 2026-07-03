# PreFlightClock Module Specification

## Overview

Master clock module with an Ableton-style count-in sequence. On pressing Play, it fires a global Reset, plays 4 audible metronome beeps, then pulls Run high on the downbeat. Solves the common pain point of sequencers missing their first step on startup.

## Type

utility

## HP Width

10 HP

## Sonic Character

- Metronome beeps: clean sine tones with exponential decay
- Count-in: 440Hz, 50ms decay
- Downbeat: 880Hz, 100ms decay
- Not a musical voice — functional audio output only

## Parameters

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| BPM | Master tempo | 30-300 BPM | 120 |
| Play | Momentary button to start count-in | Button | - |
| Stop | Momentary button to stop and reset | Button | - |

## Inputs

| Input | Type | Description |
|-------|------|-------------|
| BPM CV | CV (0-10V) | Modulates BPM (0-10V adds 0-270 BPM) |
| Play Trig | Trigger | External play trigger |
| Stop Trig | Trigger | External stop trigger |

## Outputs

| Output | Type | Description |
|--------|------|-------------|
| Metro | Audio | Metronome beep output (count-in + downbeat) |
| Reset | Trigger | Fires once on Play (first beat of count-in) |
| Run | Gate | HIGH when running (after count-in completes) |
| x1 | Trigger | Master clock (1 pulse per beat) |
| x2 | Trigger | 2 pulses per beat |
| x3 | Trigger | 3 pulses per beat |
| x4 | Trigger | 4 pulses per beat |
| x8 | Trigger | 8 pulses per beat |
| x16 | Trigger | 16 pulses per beat |
| x32 | Trigger | 32 pulses per beat |
| /1.5 | Trigger | 1 pulse every 1.5 beats |
| /2 | Trigger | 1 pulse every 2 beats |
| /3 | Trigger | 1 pulse every 3 beats |
| /4 | Trigger | 1 pulse every 4 beats |
| /8 | Trigger | 1 pulse every 8 beats |
| /16 | Trigger | 1 pulse every 16 beats |
| /32 | Trigger | 1 pulse every 32 beats |

## Algorithm / DSP Approach

- **Phase accumulator**: Single `double beatPhase` incremented by `BPM / (60 * sampleRate)` each sample
- **Edge detection**: `floor(beatPhase * R) != floor(prevBeatPhase * R)` for each ratio R
- **State machine**: STOPPED → COUNTING_IN → RUNNING (3 states)
- **Metronome synthesis**: `sin(2π * phase) * envelope` with exponential decay
- **Pulse outputs**: 1ms trigger pulses via `dsp::PulseGenerator`
- **Run output**: Continuous gate (10V while RUNNING, 0V otherwise)

## Inspiration / References

- Ableton Live's count-in feature
- Hardware drum machine count-in sequences
- VCV Rack Clocked module (Impromptu) — but with integrated count-in

## Special Requirements

- Clock phase accumulator must use `double` precision to avoid drift at high BPM over long sessions
- All clock outputs must be derived from a single phase accumulator for perfect sync
- Reset must fire exactly once per Play press (on first beat)
- Count-in must be exactly 4 beats before Run goes high
- Stop must immediately kill Run and metronome (no fade)
- State persisted via JSON for patch save/load

## Test Scenarios

1. Default settings (120 BPM) — should produce steady quarter-note clock on x1
2. Press Play — should hear 4 beeps then Run goes high
3. Press Stop during count-in — should immediately stop, no Run pulse
4. BPM CV sweep — tempo should smoothly track CV
5. External play/stop triggers — should behave identically to buttons
6. All multiplied outputs — should fire at correct integer multiples of master clock
7. All divided outputs — should fire at correct subdivisions of master clock
