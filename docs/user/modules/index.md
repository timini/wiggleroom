# Module Documentation

Detailed documentation for each WiggleRoom module.

## Physical Modeling Synthesis

- [ChaosFlute](ChaosFlute.md) - Chaotic non-linear flute with feedback and breath noise
- [Linkage](Linkage.md) - Chaotic percussion generator using coupled physical spring model
- [Matter](Matter.md) - Struck solid inside a resonant tube — morphs from strings to bells
- [ModalBell](ModalBell.md) - Physical model of a struck metal bar with tunable modes
- [NutShaker](NutShaker.md) - Physical model of a shaker/maraca instrument
- [PhysicalChoir](PhysicalChoir.md) - Vocal choir synthesis with multiple voice types
- [PluckedString](PluckedString.md) - Karplus-Strong plucked string with damping and position
- [SpaceCello](SpaceCello.md) - Digital Yaybahar — bowed string with spring reverb coupling
- [TheAbyss](TheAbyss.md) - Waterphone-inspired metallic percussion instrument

## Filters & Effects

- [BigReverb](BigReverb.md) - Zita Rev1 algorithmic reverb with pre-delay and EQ
- [InfiniteFolder](InfiniteFolder.md) - West Coast-style wavefolder with infinite folding
- [LadderLPF](LadderLPF.md) - Classic 4-pole ladder lowpass filter with resonance
- [SaturationEcho](SaturationEcho.md) - Vintage tape delay with saturation and modulation
- [SpectraHenge](SpectraHenge.md) - Four-node spectral processor with stereo audio inputs and send/return loop
- [SpectralResonator](SpectralResonator.md) - 6-band resonant filter bank for spectral shaping
- [TheCauldron](TheCauldron.md) - Fluid wave math processor with chaotic modulation
- [TriPhaseEnsemble](TriPhaseEnsemble.md) - 3-voice BBD string ensemble effect

## Synthesizers & Oscillators

- [ACID9Voice](ACID9Voice.md) - TB-303 inspired acid synth voice with morphing oscillator
- [AnalogDrums](AnalogDrums.md) - Virtual analog drum machine with 12 independent voices
- [ChaosPad](ChaosPad.md) - XY pad controller with 8 chaos-based effects
- [TetanusCoil](TetanusCoil.md) - Chaotic oscillator with coupled nonlinear systems
- [VektorX](VektorX.md) - Vector synthesis module with complex modulation

## Sequencers & Clocks

- [ACID9Seq](ACID9Seq.md) - Companion sequencer for ACID9Voice with planetary display
- [Cycloid](Cycloid.md) - Polar Euclidean sequencer with rotating patterns
- [EucBank](EucBank.md) - 16-slot pattern storage/recall for EucSeq + LogicMangler chain
- [EucMix](EucMix.md) - 4x4 CV summing matrix for standalone or expander use
- [EucSeq](EucSeq.md) - 4-channel Euclidean sequencer with per-step CV values
- [GravityClock](GravityClock.md) - Clock-synced bouncing ball trigger generator
- [Intersect](Intersect.md) - Rhythmic trigger generator with set operations
- [Mutagen](Mutagen.md) - CV-addressable step sequencer with random values, a mutation rate and 16 banks
- [Mutagen X](MutagenX.md) - Slave lane expander for Mutagen
- [Stems](Stems.md) - Beat-synced separation, wavetable voice and granular texturiser
- [LogicMangler](LogicMangler.md) - Truth table logic processor with cell locking and density control
- [PreFlightClock](PreFlightClock.md) - Master clock with Ableton-style count-in sequence
- [TheWeaver](TheWeaver.md) - Scale-Bus-aware polyphonic arpeggiator with 11 patterns and addressable Index CV

### 2- and 3-channel EucLogic chain

Narrower variants of the EucSeq chain above, for patches that do not need four channels.

- [EucSeq2](EucSeq2.md) / [EucSeq3](EucSeq3.md) - 2- and 3-channel Euclidean sequencers
- [LogicMangler2](LogicMangler2.md) / [LogicMangler3](LogicMangler3.md) - matching truth table logic processors
- [EucMix2](EucMix2.md) / [EucMix3](EucMix3.md) - 2x2 and 3x3 CV summing matrices
- [EucBank2](EucBank2.md) / [EucBank3](EucBank3.md) - 16-slot pattern storage for each chain

## Utilities

- [OctoLFO](OctoLFO.md) - 8-channel clock-synced LFO with multiple shapes and per-channel FM
- [PixelProbe](PixelProbe.md) - Image-to-CV colour sampler with X/Y probe addressing
- [TheArchitect](TheArchitect.md) - Polyphonic quantizer and chord machine
- [XFade](XFade.md) - CV crossfader/mixer with Ring Mod and Fold modes

---

For source, module list, and installation, see the [main README](../../../README.md#modules-47).
