# SpectraHenge Module Specification

## Overview

4-channel spatial/spectral mixer that places audio sources on an interactive X/Y grid. X-axis controls stereo panning (equal-power), Y-axis morphs an SVF filter between LPF, BPF, and HPF. All 4 channels sum to a stereo output with soft-limiting.

## Type

effect

## HP Width

16 HP

## Sonic Character

- Pristine, precise spectral processing with spatial imaging
- Smooth voltage-controlled filter morphing between clean LP, resonant BP, and transparent HP
- Equal-power panning preserves perceived loudness across the stereo field
- Resonance (Q 0.5–10) adds crystalline shimmer when modulated

## Parameters

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| Node 1 X | Pan position for channel 1 | 0–1 (L→R) | 0.25 |
| Node 1 Y | Filter morph for channel 1 | 0–1 (LPF→BPF→HPF) | 0.25 |
| Node 2 X | Pan position for channel 2 | 0–1 (L→R) | 0.75 |
| Node 2 Y | Filter morph for channel 2 | 0–1 (LPF→BPF→HPF) | 0.75 |
| Node 3 X | Pan position for channel 3 | 0–1 (L→R) | 0.25 |
| Node 3 Y | Filter morph for channel 3 | 0–1 (LPF→BPF→HPF) | 0.75 |
| Node 4 X | Pan position for channel 4 | 0–1 (L→R) | 0.75 |
| Node 4 Y | Filter morph for channel 4 | 0–1 (LPF→BPF→HPF) | 0.25 |
| Q | SVF resonance (shared across all channels) | 0.5–10 | 1.0 |

Node X/Y parameters are hidden knobs driven by the interactive display — users drag nodes directly on the X/Y grid.

## Inputs

| Input | Type | Description |
|-------|------|-------------|
| In 1–4 | Audio (4x) | Channel audio inputs (±5V) |
| CV X1–4 | CV (4x) | Pan CV per channel (±10V) |
| CV Y1–4 | CV (4x) | Filter tilt CV per channel (±10V) |
| LFO A | CV | Global X drift modulation (±5V, scaled ×0.05) |
| LFO B | CV | Global Y drift modulation (±5V, scaled ×0.05) |

## Outputs

| Output | Type | Description |
|--------|------|-------------|
| Left | Audio | Stereo left mix of all 4 filtered/panned channels |
| Right | Audio | Stereo right mix of all 4 filtered/panned channels |

## Algorithm / DSP Approach

- **SVF filter morphing**: State Variable Filter per channel with tilt parameter (0=LPF, 0.5=BPF, 1=HPF). Cutoff varies 80Hz–16kHz via exponential curve tied to tilt.
- **Equal-power panning**: `left = cos(pan × π/2)`, `right = sin(pan × π/2)` — prevents volume dips at center.
- **Processing pipeline per channel**: Audio In → SVF Filter Morph → Equal-Power Pan → Sum to L/R buses.
- **Output stage**: Soft limiting via `tanh()` saturation (gain 0.5) prevents clipping. Output scaled to ±5V.
- **CV modulation**: Per-channel X/Y CV (±10V → ±1.0 normalized) plus global LFO A/B drift (±5V → ±0.25 normalized), all clamped to 0–1.
- **Implementation**: Faust DSP with `FaustModule<VCVRackDSP>` wrapper. Manual parameter mapping in `process()` with atomic variables for thread-safe display updates.

## Inspiration / References

- Spatial mixing consoles with filter-per-channel
- Mutable Instruments Blinds (CV mixing) combined with Ripples (SVF)
- XY pad controllers in DAWs (e.g., Ableton's XY effect racks)

## Special Requirements

- Interactive NanoVG display with 4 draggable colored nodes (cyan, magenta, orange, green)
- Connection lines between nodes showing spatial relationships
- Hover tooltips showing node position as percentages
- Thread-safe `std::atomic` communication between DSP and UI threads
- No gate/trigger logic — purely continuous audio processing
- Input scaling ×0.2, output scaling ×5 for VCV voltage conventions

## Test Scenarios

1. Default settings — 4 channels spread across stereo field with varied filter positions
2. All LPF — all channels at tilt=0 with full stereo spread
3. All HPF — all channels at tilt=1 with centered positioning
4. Morph spread — demonstrates full LP→BP→HP progression with high resonance
5. High resonance — Q=8 with all channels at BPF center point
