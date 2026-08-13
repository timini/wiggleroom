# Stems Module Specification

## Overview

Stems records audio into a buffer, decomposes it into four beat-synced layers, and uses those layers three ways at once: as playable loops, as the source material for a morphing wavetable oscillator, and as the pitch reference for a scale-detecting quantizer. The synth voice and the loop are then mixed into a granular texturizer. It is a self-contained ambient generative instrument that turns any recorded fragment into an evolving accompaniment.

## Type

instrument / effect (Native C++, inherits from `rack::Module`, NOT Faust)

Faust is the wrong tool here: the module needs a multi-second audio buffer, a background worker thread, FFT analysis, dynamic wavetable construction and a grain scheduler with per-grain state. `FaustModule` is single-instance, monophonic and has no threading model.

## HP Width

34 HP

The control surface is large. If 34 HP proves unwieldy, the granular section is the natural candidate to move behind a page/shift control, since it is the least likely to need simultaneous hands-on adjustment.

## Sonic Character

- Ambient, evolving, generative. Designed to be left running and nudged, not performed note by note
- The wavetable voice inherits the harmonic fingerprint of the source material, so it always sits in key with the loop rather than against it
- Lowpass gate gives plucked, struck decays rather than sustained pads
- Granular stage ranges from subtle smearing to complete dissolution of the source into cloud texture
- Character depends entirely on what you feed it: field recordings become drifting drones, drum loops become pitched metallic sequences
- Not clean or hi-fi by intent. Separation artefacts, stretch artefacts and grain windowing are part of the sound

## Parameters

### Buffer and recording

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| rec_arm | Arms/starts recording. Momentary | button | off |
| rec_mode | Trigger-start, threshold-start, or overdub | 0-2 | 0 |
| rec_thresh | Input level that starts recording in threshold mode | -60 to 0 dB | -30 |
| buf_len | Buffer length in bars, quantised to the clock | 1-16 bars | 4 |

### Clock and sync

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| clock_div | Division/multiplication of the incoming clock | /16 to x16 | x1 |
| sync_mode | Repitch, time-stretch, or granular-sync | 0-2 | 1 |
| loop_start | Loop start point as a fraction of the buffer | 0-1 | 0 |
| loop_len | Loop length as a fraction of the buffer | 0.03-1 | 1 |

### Stem mixer

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| stem_1_level .. stem_4_level | Per-stem gain | -inf to +6 dB | 0 dB |
| stem_1_mute .. stem_4_mute | Per-stem mute | button | off |
| stem_select | Which stem feeds the analyser and wavetable | 1-4 | 4 |

### Pitch analysis and quantizer

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| scale_mode | Auto-detect from stem, or manual override | 0-1 | 0 |
| root | Manual root note, used when scale_mode is manual | 0-11 | 0 |
| scale | Manual scale selection | 0-N | 0 |
| quant_glide | Portamento applied to quantised CV output | 0-2 s | 0 |

### Wavetable oscillator

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| wt_window | Extraction window size around the playhead | 256-8192 samples | 2048 |
| wt_offset | Offset of the extraction window from the playhead | -1 to 1 | 0 |
| wt_morph | Frame interpolation rate. Low values smear, high values snap | 0-1 | 0.5 |
| wt_coarse | Oscillator pitch, semitones | -24 to +24 | 0 |
| wt_fine | Oscillator fine tune | -1 to +1 semitone | 0 |
| wt_level | Oscillator output level into the LPG | 0-1 | 0.7 |

### Lowpass gate

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| lpg_decay | Vactrol decay time | 20 ms to 4 s | 400 ms |
| lpg_colour | Continuum from VCA through Both to Lowpass | 0-1 | 0.5 |
| lpg_level | Resting level when untriggered | 0-1 | 0 |

### Granular stage

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| grain_balance | Loop versus synth voice into the granulator | 0-1 | 0.5 |
| grain_size | Grain duration | 1-500 ms | 80 |
| grain_density | Grain trigger rate | 0.1-100 Hz | 10 |
| grain_pitch | Grain transposition, semitones | -24 to +24 | 0 |
| grain_texture | Windowing shape and randomisation amount | 0-1 | 0.3 |
| grain_spread | Stereo dispersal | 0-1 | 0.5 |
| grain_space | Diffusion/reverb amount and decay | 0-1 | 0.3 |
| grain_mix | Dry/wet balance | 0-1 | 0.5 |

## Inputs

| Input | Type | Description |
|-------|------|-------------|
| audio_in_l | Audio | Left/mono input to the record buffer |
| audio_in_r | Audio | Right input. Normalled from left when unpatched |
| clock_in | Clock | Beat sync. Sets tempo and phase for all loop playback |
| reset_in | Trigger | Resets loop phase to the downbeat |
| rec_trig | Trigger | Starts/stops recording |
| lpg_trig | Trigger | Fires the lowpass gate envelope |
| quant_cv_in | CV (1V/Oct) | External pitch to be quantised to the detected scale |
| wt_offset_cv | CV | Modulates the wavetable extraction offset |
| grain_dens_cv | CV | Modulates grain density |
| grain_pitch_cv | CV | Modulates grain transposition |

`reset_in` is essential for a beat-synced module and is absent from the original concept document.

## Outputs

| Output | Type | Description |
|--------|------|-------------|
| main_l, main_r | Audio | Master stereo output, post granular |
| loop_l, loop_r | Audio | Stem mixer output, pre granular |
| voice_out | Audio | Wavetable voice post LPG, pre granular |
| quant_cv_out | CV (1V/Oct) | Quantised pitch. Normalled internally to the wavetable oscillator |
| downbeat_out | Trigger | Fires on each loop downbeat, for sequencing other modules |

Separate loop, voice and main outputs let the module be used as three instruments at once, and make each stage independently testable.

## Algorithm / DSP Approach

### Threading model

This is the architectural spine and must be settled before anything else is written.

- **Audio thread** (`process()`): stem mixing, playback, wavetable extraction and oscillation, LPG, granular. No allocation, no locks, no file or model I/O.
- **Worker thread**: separation, beat/downbeat analysis, scale detection. Results handed to the audio thread through lock-free single-producer single-consumer buffers with atomic pointer swap.
- Rack guarantees it never calls `Module` methods concurrently, but it makes no guarantees about threads you create. All shared state crosses the boundary through atomics or lock-free queues.

### Stage 1: Buffer and beat sync

Fixed-size preallocated ring buffer sized at module construction for the maximum supported length at the current sample rate. Never reallocated during playback.

Tempo and phase come from `clock_in`, so no beat tracking is required for sync itself. Beat *detection* on the recorded buffer is only needed to align the recording to the grid when the recording did not start on a downbeat.

Three sync strategies, user-selectable, because each fails differently:
- **Repitch**: vari-speed playback. Cheapest, always works, changes pitch. Musically valid and often preferable for ambient material.
- **Time-stretch**: preserves pitch. Introduces artefacts, costs CPU.
- **Granular-sync**: overlapping grain playback locked to the grid. Degrades gracefully at extreme ratios.

### Stage 2: Separation, tiered

The concept document assumes a neural separation model. That is achievable but carries constraints that decide the whole product (see `docs/specs/stems-architecture.md`). The spec therefore defines two tiers with an identical downstream interface, so the rest of the module neither knows nor cares which produced the stems.

**Tier 0, classical DSP. Always available, ships in the box.**
Harmonic/percussive separation by median filtering on the spectrogram (Fitzgerald, DAFx-10; margin extension by Driedger, Müller and Disch, 2014), combined with a band split. Produces four layers:

| Layer | Derivation |
|-------|-----------|
| Low | Below the split frequency, harmonic-dominant |
| Percussive | Median-filtered across frequency bins |
| Harmonic | Median-filtered across time frames |
| Residual | What neither mask claims |

Cheap, deterministic, no model files, no external dependencies, permissively implementable. For ambient generative use these four layers are musically sufficient: the instrument needs *material that differs*, not correctly labelled instruments.

**Tier 1, neural separation. Optional, user-enabled.**
Four-stem model via ONNX Runtime on the worker thread. Better labelled separation at significant cost in size and deployment complexity. Models are not bundled; the user points the module at a model directory.

Tier 0 is the MVP and must be complete and shippable on its own. Tier 1 is an enhancement, not a prerequisite.

### Stage 3: Pitch analysis and quantizer

Autocorrelation or YIN-family fundamental estimation over a sliding window of the selected stem, accumulated into a pitch-class histogram, matched against scale templates to yield root and mode.

Critical constraint absent from the concept: **scale detection is meaningless on an unpitched stem.** Running it on a drum layer produces noise. The module must compute a confidence score and, below threshold, hold the last confident result rather than emit garbage. If the user selects a percussive stem for analysis the UI must indicate that analysis is inactive.

Quantiser snaps `quant_cv_in` to the nearest note in the detected scale and emits it on `quant_cv_out`, normalled internally to the wavetable oscillator so the module is self-playing with nothing patched.

### Stage 4: Dynamic wavetable

A window of `wt_window` samples is taken from the selected stem centred on the current playhead plus `wt_offset`, windowed, normalised, and installed as the current oscillator frame. Frames update as the playhead moves; `wt_morph` sets crossfade time between successive frames.

Two constraints the concept does not address:

1. **Extract before time-stretching, not after.** Stretch artefacts in the wavetable are far more audible than in the loop, because the wavetable is played back cyclically at audio rate and any artefact becomes a stable timbral feature. Extraction reads the unstretched stem at the position corresponding to the current playhead.
2. **Window size must be pitch-independent.** The window is resampled to a fixed power-of-two frame size, so `wt_window` changes the amount of source material captured, not the oscillator's pitch.

### Stage 5: Lowpass gate

Vactrol-modelled lowpass gate. `lpg_colour` sweeps the VCA/Both/Lowpass continuum. Reuses the model being specified in `specs/TheLantern.md` (PR #66, not yet merged); the two modules should share one implementation rather than each growing their own. If that spec does not land, this module needs its own vactrol model and the reference should be removed.

### Stage 6: Granular

Grain scheduler over a mix of the loop bus and the voice bus, set by `grain_balance`. Fixed maximum concurrent grain count with a preallocated grain pool, so worst-case CPU is bounded and known. Followed by a diffusion/reverb stage.

## Inspiration / References

- Mutable Instruments Clouds and Beads for the granular stage. Clouds' source is MIT licensed and directly adaptable
- Make Noise Morphagene for the buffer/splice concept
- Buchla 292 for the lowpass gate character
- Fitzgerald, "Harmonic/Percussive Separation using Median Filtering", DAFx-10
- Full architecture, library evaluation, licensing analysis and a fact-check of the originating concept document: `docs/specs/stems-architecture.md`

## Special Requirements

- **Real-time safety**: no allocation, locking, logging or I/O on the audio thread. All buffers preallocated at construction or on the worker thread.
- **Bounded CPU**: the grain pool is fixed size. Wavetable extraction is amortised across blocks rather than done in one spike when the playhead crosses a frame boundary.
- **Memory budget**: Tier 0 must run in under 200 MB including the buffer and all four stem copies. Tier 1's model footprint is explicitly out of that budget and is disclosed to the user.
- **Patch persistence**: the audio buffer is **not** saved into the patch file by default. Four stems of stereo 48 kHz audio at 16 bars is well over 100 MB and would make patches unusable. Offer explicit save-to-disk as an opt-in, storing a reference in the patch.
- **Sample rate**: must handle sample rate changes without dropouts, resampling the buffer or invalidating and re-separating as appropriate.
- **Graceful degradation**: the module must remain playable while separation is running. Until stems are ready, all four mixer channels play the unseparated buffer. Separation failure is non-fatal and falls back to the same state.
- **Licensing**: every third-party dependency must be compatible with GPL-3.0-or-later. Several obvious candidate libraries are not, or impose obligations that foreclose other distribution routes. See the architecture document before adding any dependency.
- **No bundled model weights** in the VCV Library package.

## Test Scenarios

1. **Record and play**: record 4 bars against a 120 BPM clock, confirm the loop plays back phase-locked and that `reset_in` returns it to the downbeat.
2. **Tier 0 separation**: feed a loop with clear kick and sustained pad. Percussive layer should contain the kick with the pad strongly attenuated, harmonic layer the reverse. Measure energy ratio in each band rather than judging by ear.
3. **Separation latency**: confirm audio never drops out while separation runs, and that the module is playable throughout.
4. **Scale detection, pitched source**: feed material in a known key. Detected root and scale must match within a few seconds.
5. **Scale detection, unpitched source**: select the percussive layer. Confidence must drop below threshold and the last good scale must be held. No random quantiser output.
6. **Quantizer**: sweep `quant_cv_in` with a slow ramp, confirm output is a staircase restricted to detected scale degrees.
7. **Wavetable tracking**: confirm the wavetable changes as the playhead advances, and that the oscillator's pitch is set by `quant_cv_out` and not by `wt_window`.
8. **Wavetable purity**: with `sync_mode` set to time-stretch at an extreme ratio, confirm the wavetable is extracted pre-stretch and does not inherit stretch artefacts.
9. **LPG**: short trigger with `lpg_colour` mid. Output should decay with simultaneous loss of brightness and level.
10. **Granular bounds**: `grain_density` at maximum with `grain_size` at maximum. Confirm the grain pool caps concurrency and CPU stays bounded.
11. **Empty buffer**: every control must be safe to move before anything is recorded. No NaN, no denormals, no output spikes.
12. **Sample rate change**: switch 44.1 kHz to 96 kHz mid-playback. No crash, no dropout, loop stays in time.
13. **Patch save/load**: save and reload a patch. Confirm parameters restore, confirm the patch file has not ballooned.
