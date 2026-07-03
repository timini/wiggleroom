# The Weaver (Arpeggiator) Module Specification

## Overview

The Weaver is an advanced, scale-aware arpeggiator that deeply integrates with the WiggleRoom Scale Bus protocol. It takes incoming chords (via polyphonic V/Oct) or auto-generates diatonic chords based on the Scale Bus, and weaves them into rhythmic melodic patterns. It features deep clock division/multiplication, standard and experimental arpeggio patterns, and the ability to manually address the arpeggio pool via CV.

## Type

sequencer / utility (Native C++, inherits from `rack::Module` — NOT Faust)

## HP Width

12 HP

## Sonic Character

- Rhythmic, precise, generative, and highly melodic.
- Can range from classic, driving synth basslines to fluid, chaotic, and ethereal cascading melodies depending on the selected pattern and clock rate.
- Strictly harmonic; never plays a wrong note thanks to the Scale Bus integration.

## Parameters

| Parameter      | Description                                                                                                  | Range    | Default  |
|----------------|--------------------------------------------------------------------------------------------------------------|----------|----------|
| Pattern        | Selects the arpeggiator playback algorithm (0-10)                                                            | 0-10     | 0 (Up)   |
| Octave Spread  | Number of octaves to extend the arpeggio pattern                                                             | 1-4      | 1        |
| Clock Div/Mult | Multiplies or divides the incoming clock rate (/16, /8, /4, /3, /2, x1, x2, x3, x4, x8, x16)                 | 0-10     | 5 (x1)   |
| Gate Length    | Duration of the output triggers/gates as fraction of tick period                                             | 1%-100%  | 50%      |
| Sync Mode      | 0 = Free running (internal clock from last measured period); 1 = Hard Sync (phase-locks to clock input edges)| 0-1      | 1 (Sync) |
| Hold/Latch     | Latches the currently held notes even after the input goes to 0V                                             | 0-1      | 0 (Off)  |
| Index          | Manually selects the note from the active arpeggio pool                                                      | 0-1      | 0        |

### Arpeggio Variations (Pattern)

0. **Up** — Lowest note to highest note.
1. **Down** — Highest note to lowest note.
2. **Up / Down** — Lowest to highest, then highest to lowest.
3. **Order Played** — Plays notes in the order the poly channels carry them.
4. **Random** — True random selection from the note pool.
5. **Brownian** — Randomly walks ±1 step in the pool.
6. **Converge (Outside In)** — Alternates between highest and lowest, moving inward.
7. **Diverge (Inside Out)** — Starts at the middle notes and moves outward.
8. **Pinky/Thumb** — Always alternates between the lowest note and a walking upper note.
9. **Scale Run** — Plays every note in the Scale Bus mask between lowest and highest held notes.
10. **Octave Jump** — Plays the base chord, randomly shifting individual notes ±1 octave.

## Inputs

| Input       | Type            | Description                                                                                   |
|-------------|-----------------|-----------------------------------------------------------------------------------------------|
| clock       | Gate/Trigger    | Main clock input. Measures period for Div/Mult.                                               |
| reset       | Trigger         | Resets the arpeggiator sequence to step 0.                                                    |
| notes_in    | V/Oct (Poly)    | Up to 16 channels of V/Oct defining the active chord.                                         |
| scale_bus   | 16-ch Poly      | Channels 0-11: scale mask (10V=active, 0V=inactive). Channel 15: root V/Oct.                  |
| pattern_cv  | CV              | Modulates the active arpeggio pattern (sums with Pattern knob; 1V ~= 1 step).                 |
| div_cv      | CV              | Modulates the Clock Div/Mult parameter (sums with knob; 1V ~= 1 step).                        |
| index_cv    | CV              | Note-select CV. When patched, the internal counter stops and CV directly addresses the pool. |

## Outputs

| Output    | Type          | Description                                                                         |
|-----------|---------------|-------------------------------------------------------------------------------------|
| out_cv    | Mono V/Oct    | Quantized pitch of the current arpeggiator step.                                    |
| out_gate  | Gate/Trigger  | Gate for the current step. Zero-latency when Sync is ON.                            |
| eoc       | Trigger       | End of Cycle. Fires (1 ms pulse) when the pattern wraps back to the first step.     |

## Algorithm / DSP Approach

Native C++ (no Faust). Uses `rack::dsp::SchmittTrigger` and `rack::dsp::PulseGenerator`.

1. **Note pool construction** (each sample; rate-limit internal if needed):
   - Read `notes_in` polyphonic V/Oct. If unpatched or empty, fall back to an auto-triad derived from the Scale Bus root (root, 3rd-in-scale, 5th-in-scale).
   - If Hold is ON and new notes arrive, replace the pool. If input goes to zero channels while Hold is ON, keep the last pool.
   - Apply Scale Bus quantization: for each note, if its chromatic position is not in the mask, snap to nearest masked chroma.
   - Apply Octave Spread: duplicate sorted pool +1, +2, +3 octaves as selected.
   - Sort ascending by voltage. Maintain a secondary "order-played" vector matching the raw poly channel order for the Order-Played pattern.

2. **Scale Bus reader** (matches protocol from `TheArchitect`):
   - Channels 0-11: `voltage > 0.5f` means that chromatic degree is active.
   - Channel 15: `round(voltage * 12) mod 12` = root (0=C, 1=C#, ...).
   - If unpatched or < 12 channels: fallback mask = `0xFFF` (all chromatic).

3. **Clock engine:**
   - Measure `clockPeriod` as time between `clock` rising edges (Schmitt trigger, 0.1 V / 1 V).
   - Derive `outputTickPeriod = clockPeriod / ratio`, where ratio comes from the 11-step Div/Mult table.
   - **Sync ON**: hard-resync on each input edge. For ratio ≥ 1 (xN), fire an output tick immediately on every input edge and emit `N-1` interpolated sub-ticks before the next edge. For ratio < 1 (/N), count input edges and fire every `N`-th edge.
   - **Sync OFF**: phase accumulator runs freely at `1/outputTickPeriod`; input edges only update the measured period.
   - Missing clock timeout: if no input tick for > 3 s, mark clock lost (internal clock still runs from last `clockPeriod`).

4. **Pattern engine:**
   - Maintains `arpIndex` into sorted pool and pattern-specific state (direction bit for Up/Down, Brownian counter, etc.).
   - Reset input jumps `arpIndex` back to 0 and clears pattern state.
   - On each output tick, advance according to pattern; emit EOC pulse when the pattern wraps.

5. **Index CV mode** (when `index_cv` is patched):
   - Map `clamp(voltage, 0, 10) / 10` across `[0, poolSize)`.
   - When the integer index changes (new note), fire a gate pulse. Pattern/clock logic is bypassed.

6. **Gate length:**
   - On tick fire: set `gatePhase = 0`. Gate is HIGH while `gatePhase < gateLength * outputTickPeriod`. Capped at 99% of period so back-to-back ticks always see a falling edge.

## Inspiration / References

- Mutable Instruments Edges / Marbles — generative CV addressing.
- Roland Jupiter-8 / Juno-60 — classic up/down/random patterns.
- Intellijel Metropolix, Make Noise René — addressable sequencing with scale constraint.

## Special Requirements

- **Zero-Latency Clock Sync**: in Sync ON mode the output gate and CV update on the same sample as the clock rising edge.
- **Scale Bus Fallback**: when `scale_bus` is unpatched, default mask is chromatic so the arpeggiator behaves as a plain unquantized arpeggiator.
- **Polyphonic Sorting**: efficient `std::sort` on a small pool (≤ 64 after octave spread); no dynamic allocation per sample beyond a reused `std::vector`.
- **Native C++ only** — reuses Scale Bus conventions from `TheArchitect` / `ACID9Seq`. No Faust.

## Test Scenarios

1. **Defaults (Sync ON, Pattern Up, x1 Clock)** — 3-note poly chord + clock → rigid ascending arp locked to input clock.
2. **Scale Bus quantization** — C-major triad in, Architect set to D major → C natural auto-quantized to C# on output.
3. **Addressable mode** — Index CV patched, clock unpatched, Sync OFF, LFO into `index_cv` → smooth sweep up/down chord tones, gate fires each time a new note boundary is crossed.
