# Wiggle Room

A collection of creative modules for [VCV Rack 2](https://vcvrack.com/), built with [Faust](https://faust.grame.fr/) physical modeling DSP.

**[Documentation](docs/README.md)** | **[Developer Guide](docs/developer/getting-started.md)** | **[Release Strategy](docs/RELEASE_STRATEGY.md)**

## Modules (47)

### Physical Modeling Synthesis
| Module | Description |
|--------|-------------|
| **ChaosFlute** | Chaotic non-linear flute with feedback and breath noise |
| **Linkage** | Chaotic percussion generator using coupled physical spring model |
| **Matter** | Struck solid inside a resonant tube — morphs from strings to bells |
| **ModalBell** | Physical model of a struck metal bar with tunable modes |
| **NutShaker** | Physical model of a shaker/maraca instrument |
| **PhysicalChoir** | Vocal choir synthesis with multiple voice types |
| **PluckedString** | Karplus-Strong plucked string with damping and position |
| **SpaceCello** | Digital Yaybahar — bowed string with spring reverb coupling |
| **TheAbyss** | Waterphone-inspired metallic percussion instrument |

### Filters & Effects
| Module | Description |
|--------|-------------|
| **BigReverb** | Zita Rev1 algorithmic reverb with pre-delay and EQ |
| **InfiniteFolder** | West Coast-style wavefolder with infinite folding |
| **LadderLPF** | Classic 4-pole ladder lowpass filter with resonance |
| **SaturationEcho** | Vintage tape delay with saturation and modulation |
| **SpectraHenge** | Four-node spectral processor with stereo inputs and send/return loop |
| **SpectralResonator** | 6-band resonant filter bank for spectral shaping |
| **TheCauldron** | Fluid wave math processor with chaotic modulation |
| **TriPhaseEnsemble** | 3-voice BBD string ensemble effect |

### Synthesizers & Oscillators
| Module | Description |
|--------|-------------|
| **ACID9Voice** | TB-303 inspired acid synth voice with morphing oscillator |
| **AnalogDrums** | Virtual analog drum machine with 12 independent voices |
| **TetanusCoil** | Chaotic oscillator with coupled nonlinear systems |
| **VektorX** | Vector synthesis module with complex modulation |
| **ChaosPad** | XY pad controller with 8 chaos-based effects |

### Sequencers & Clocks
| Module | Description |
|--------|-------------|
| **ACID9Seq** | Companion sequencer for ACID9Voice with planetary display |
| **Cycloid** | Polar Euclidean sequencer with rotating patterns |
| **EucSeq** | 4-channel Euclidean sequencer with per-step CV values |
| **Logic Mangler** | Truth table logic processor with cell locking and density control |
| **EucBank** | 16-slot pattern storage/recall for EucSeq + LogicMangler chain |
| **EucMix** | 4x4 CV summing matrix for standalone or expander use |
| **GravityClock** | Clock-synced bouncing ball trigger generator |
| **Intersect** | Rhythmic trigger generator with set operations |
| **Mutagen** | CV-addressable step sequencer with random values, a mutation rate and 16 banks |
| **Mutagen X** | Slave lane expander for Mutagen, adds a CV lane on the same clock and address |
| **Stems** | Beat-synced separation, wavetable voice and granular texturiser |
| **PreFlightClock** | Master clock with Ableton-style count-in sequence |
| **TheWeaver** | Scale-Bus-aware polyphonic arpeggiator with 11 patterns and addressable Index CV |

#### 2- and 3-channel EucLogic chain

Narrower variants of the EucSeq chain above, for patches that do not need four channels.

| Module | Description |
|--------|-------------|
| **EucSeq2** / **EucSeq3** | 2- and 3-channel Euclidean sequencers |
| **LogicMangler2** / **LogicMangler3** | Matching truth table logic processors |
| **EucMix2** / **EucMix3** | 2x2 and 3x3 CV summing matrices |
| **EucBank2** / **EucBank3** | 16-slot pattern storage for each chain |

### Utilities
| Module | Description |
|--------|-------------|
| **OctoLFO** | 8-channel clock-synced LFO with multiple shapes and per-channel FM |
| **TheArchitect** | Polyphonic quantizer and chord machine |
| **XFade** | CV crossfader/mixer with Ring Mod and Fold modes |
| **PixelProbe** | Image-to-CV color sampler — probe an image with X/Y CV, output Hue/Saturation/Intensity |

## Installation

### Manual Installation

1. Download the latest release for your platform from the [Releases](https://github.com/timini/WiggleRoom/releases) page
2. Extract to your VCV Rack plugins folder:
   - **macOS**: `~/Library/Application Support/Rack2/plugins-mac-arm64/` (or `plugins-mac-x64/`)
   - **Windows**: `%LOCALAPPDATA%/Rack2/plugins-win-x64/`
   - **Linux**: `~/.Rack2/plugins-lin-x64/`
3. Restart VCV Rack

## Building from Source

Requires CMake 3.15+ and a C++17 compiler.

```bash
# Clone the repository
git clone https://github.com/timini/WiggleRoom.git
cd WiggleRoom

# Build
just build

# Install to VCV Rack
just install
```

For Faust-based modules, install [Faust](https://faust.grame.fr/) for DSP code generation. Pre-generated headers are included for building without Faust.

## License

GPL-3.0-or-later

## Author

Tim Richardson

## Acknowledgments

- [VCV Rack](https://vcvrack.com/) by Andrew Belt
- [Faust](https://faust.grame.fr/) by GRAME
