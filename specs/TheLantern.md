# TheLantern Module Specification

## Overview

A West Coast complex oscillator voice: a sine principal oscillator shaped by a five-cell parallel wavefolder, gated by a vactrol-modelled lowpass gate. Models the Buchla 259 timbre circuit and the Buchla 292 lopass gate from published virtual-analog research.

## Type

instrument

**Implementation**: Faust DSP with `FaustModule<VCVRackDSP>` wrapper. Monophonic (`FaustModule` has no polyphony support).

## HP Width

28 HP

## Sonic Character

- Metallic, harmonically dense, hollow when symmetry is centred
- Percussive and plucked rather than sustained, because the lowpass gate closes slowly and cannot be made to snap shut
- Timbre brightens dramatically with Harmonics; the sweep is the signature gesture of the instrument
- Odd harmonics only at centred symmetry, giving a hollow, clarinet-like core; even harmonics enter as symmetry moves off centre
- Dark and woody in Lopass mode, cleanly attenuated in Gate mode, ringing and bell-like in Both mode
- Not clean: the folder produces high THD by design, and that is the point

## Parameters

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| freq | Principal oscillator coarse frequency | 27.5-7040 Hz | 261.62 |
| fine | Principal oscillator fine tune | -1 to 1 semitone | 0 |
| range | Principal oscillator range switch (low/high) | 0-1 | 1 |
| harmonics | Folder input gain. Drives harmonic count | 0-10 | 0 |
| symmetry | Pre-fold DC offset. Introduces even harmonics | -1 to 1 | 0 |
| timbre_amt | Attenuverter for the timbre CV input | -1 to 1 | 0 |
| mod_freq | Modulation oscillator frequency | 0.05-7040 Hz | 2.0 |
| mod_range | Modulation oscillator range (LFO/audio) | 0-1 | 0 |
| mod_shape | Modulation waveshape: saw, square, triangle | 0-2 | 2 |
| fm_amt | Modulation oscillator into principal frequency | 0-1 | 0 |
| am_amt | Modulation oscillator into principal amplitude | 0-1 | 0 |
| timbre_mod | Modulation oscillator into folder gain | 0-1 | 0 |
| level | Lowpass gate resting level | 0-1 | 0 |
| lpg_mode | Lopass (0), Both (1), Gate (2) | 0-2 | 1 |
| response | Scales vactrol time constants about the 12/250 ms reference | 0.5-2 | 1.0 |

Faust indexes parameters alphabetically, not by declaration order. Confirm indices with
`./build/test/faust_render --module TheLantern --list-params` before writing `mapParam` calls.

## Inputs

| Input | Type | Description |
|-------|------|-------------|
| gate | Control (0-10V) | Opens the lowpass gate. Threshold > 0.9V |
| volts | V/Oct | Principal oscillator pitch (0V = C4) |
| fm_cv | CV | External frequency modulation of the principal oscillator |
| timbre_cv | CV | Folder gain, scaled by the timbre_amt attenuverter |
| lpg_cv | CV | Lowpass gate control, sums with level |
| mod_cv | CV | Modulation oscillator frequency |
| ext_in | Audio | External signal into the folder, replacing the principal oscillator |

## Outputs

| Output | Type | Description |
|--------|------|-------------|
| out | Audio | Main output, post lowpass gate |
| sine | Audio | Principal oscillator sine, pre-fold |
| square | Audio | Principal oscillator square |

The 259 has dedicated sine and square outputs; both are reproduced here.

## Algorithm / DSP Approach

Three stages, each modelled from a published paper rather than approximated by ear.

### Stage 1: Complex oscillator

Principal oscillator generates a sine. Modulation oscillator generates saw, square or triangle and routes
to three destinations with independent depths: principal frequency (FM), principal amplitude (AM), and
folder input gain (timbre modulation). This three-way routing is what makes the 259 a *complex* oscillator
rather than two oscillators in a box.

### Stage 2: Timbre wavefolder (Buchla 259)

Five **non-identical folding cells in parallel** with a direct path, summed with alternating polarity.
This differs fundamentally from Serge and Intellijel folders, which cascade stages in series, and it is
the main reason the 259 has its particular character. Each cell is a memoryless piecewise mapping with a
deadband, with `s = sgn(Vin)`:

```
V1 = 0.8333*Vin - 0.5000*s   if |Vin| > 0.6000, else 0
V2 = 0.3768*Vin - 1.1281*s   if |Vin| > 2.9940, else 0
V3 = 0.2829*Vin - 1.5446*s   if |Vin| > 5.4600, else 0
V4 = 0.5743*Vin - 1.0338*s   if |Vin| > 1.8000, else 0
V5 = 0.2673*Vin - 1.0907*s   if |Vin| > 4.0800, else 0

Vout = -12.000*V1 - 27.777*V2 - 21.428*V3 + 17.647*V4 + 36.363*V5 + 5.000*Vin
```

Followed by a fixed one-pole lowpass at 1.33 kHz, which is a tone control in the original circuit and
should not be exposed as a parameter.

Folding is odd-symmetric, so it introduces **odd harmonics only**, and harmonic count rises directly with
input gain. `symmetry` adds a DC offset before folding, breaking that symmetry and introducing even
harmonics.

### Stage 3: Lowpass gate (Buchla 292)

A two-pole network whose cutoff is set by a modelled vactrol resistance, in three switchable modes:

| Component | Lopass | Both | Gate (VCA) |
|-----------|--------|------|------------|
| C1 | 1 nF | 1 nF | 1 nF |
| C2 | 220 pF | 220 pF | 220 pF |
| C3 | 4.7 nF | 0 | 0 |
| Ralpha | 5 MOhm | 5 MOhm | 5 kOhm |

Transfer function `H(s) = 1 / (a1 + a2*s + a3*s^2)`, coefficients in the research dossier.

**Use the topology-preserving discretisation, not the direct form.** The source paper is explicit that
the direct form produces transients under modulation and can diverge; a lowpass gate is modulated
constantly, so this is a correctness requirement rather than a preference.

The vactrol is the character of the module. It is an integrator whose time constant switches on the sign
of the input derivative: approximately **12 ms rising and 250 ms falling**, further modulated by the
current output so it responds faster when already open. That roughly 20:1 asymmetry produces the plucked,
struck-object decay that a normal VCA cannot. Current-to-resistance mapping `Rf = A/If^1.4 + B` with
`A = 3.464`, `B = 1136.212`, `If` clamped to 10 uA - 40 mA.

### Antialiasing

Wavefolding is severely aliasing-prone; at 44.1 kHz a naive implementation is unusable. The reference
paper uses two-point polyBLAMP with 8x oversampling.

polyBLAMP requires the exact instants at which the signal crosses each cell's threshold, which the paper
derives analytically **for sinusoidal input**. Because the folder is fed by the principal oscillator's
sine, that assumption holds by construction. Keeping the internal signal path sinusoidal into the folder
is what makes correct antialiasing tractable, and is a design constraint, not an implementation detail.

The `ext_in` input breaks this assumption. When it is patched, fall back to plain oversampling and accept
the additional aliasing, as an existing Faust port of this circuit was forced to do for arbitrary input.

Implementation order: 8x oversampling first, verified against the transfer-curve test, then polyBLAMP as a
measurable SNR improvement.

## Inspiration / References

- Buchla 259 Complex Waveform Generator (200 series, 1970) - the timbre circuit
- Buchla 292 Quad Lopass Gate - the output stage
- Esqueda, Pontynen, Valimaki and Parker, "Virtual Analog Buchla 259 Wavefolder", DAFx-17
- Parker and D'Angelo, "A Digital Model of the Buchla Lowpass-Gate", DAFx-13
- Full derivations, component tables and coefficient values: `docs/specs/buchla-259-292.md`

Not modelled on the 259e Twisted Waveform Generator, which is a different, digital wavetable module.

## Special Requirements

- **Naming**: the shipping `plugin.json` entry must not contain "Buchla". `scripts/verify_manifest.py`
  flags it as a trademark warning. Use "West Coast", "complex oscillator", "lowpass gate", "timbre".
  The hardware name belongs in `specs/` and `docs/specs/` only.
- **Pitch tracking**: the hardware tracks 1.2 V/octave. This module tracks the VCV standard 1 V/octave by
  default so it patches normally and passes `test_pitch_tracking`, with 1.2 V/oct offered as a
  context-menu option for authenticity.
- **Gate threshold**: `> 0.9` as required by `test_gate_response`.
- **Quality thresholds**: THD is high by design when folding. `test_config.json` must set an elevated
  `thd_max_percent` and `allow_hot_signal`, as `InfiniteFolder` does.
- **Stability**: the lowpass gate must remain stable under audio-rate modulation of its control input.
  The feedback gain has a documented stability limit that must be respected.
- **Coefficient fidelity**: folder coefficients are quoted to four decimal places from the source paper
  and must not be rounded or retuned by ear. They are validated against SPICE and are the basis of the
  transfer-curve test.

## Test Scenarios

1. **Default settings**: `harmonics = 0`, gate high. Should produce a clean sine at the pitch given by
   `volts`, with no folding.
2. **Harmonics sweep**: sweep `harmonics` 0 to 10 at `symmetry = 0`. Odd harmonics only, with harmonic
   count rising monotonically. Even-harmonic energy should stay near zero throughout.
3. **Symmetry offset**: `harmonics = 5`, sweep `symmetry` -1 to 1. Even harmonics should appear and be
   maximal at the extremes, minimal at centre.
4. **Static transfer curve**: DC sweep -10V to +10V through the folder with the gate forced open. Must
   match the published piecewise equations to within 1e-3 V. This is the primary correctness test.
5. **Lowpass gate modes**: at fixed gate level, compare magnitude response across Lopass, Both and Gate.
   Gate should attenuate roughly flat, Lopass should roll off, Both should sit between the two.
6. **Vactrol timing**: step the gate input. Rise time approximately 12 ms, fall approximately 250 ms,
   asymmetry ratio roughly 20:1, scaled by `response`.
7. **Plucked character**: short gate pulse, `lpg_mode = Both`. Output should decay with a simultaneous
   loss of brightness and level, not a level-only fade.
8. **Aliasing**: 890 Hz sine at `harmonics = 5`. Measure alias ratio; compare oversampling-only against
   the polyBLAMP implementation to confirm the improvement.
9. **Extreme settings**: all modulation depths at maximum, audio-rate modulation oscillator. Must not
   produce NaN, Inf or runaway output.
10. **External input**: patch `ext_in`, confirm it replaces the principal oscillator into the folder.
