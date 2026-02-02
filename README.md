# Wiggle Room

A collection of creative modules for [VCV Rack 2](https://vcvrack.com/).

Wiggle Room focuses on rhythmic generation, spectral processing, and lush audio effects. The modules range from simple utilities to complex algorithmic sequencers and high-quality DSP effects built with [Faust](https://faust.grame.fr/).

## Modules

### Oscillators & Sound Sources

| Module | Description |
|--------|-------------|
| [Super Oscillator](docs/modules/SuperOsc.md) | Simple sine/saw oscillator with V/Oct tracking |
| [Modal Bell](docs/modules/ModalBell.md) | Physical model of a struck bar - vibraphone, marimba, and bell sounds |
| [Plucked String](docs/modules/PluckedString.md) | Karplus-Strong plucked string - guitar, harp, and plucked sounds |

### Rhythm & Sequencing

| Module | Description |
|--------|-------------|
| [Intersect](docs/modules/Intersect.md) | Rhythmic trigger generator using discrete gradient analysis |
| [Cycloid](docs/modules/Cycloid.md) | Polar Euclidean sequencer with spirograph LFO visualization |

### Filters

| Module | Description |
|--------|-------------|
| [Moog LPF](docs/modules/MoogLPF.md) | Classic Moog ladder lowpass filter with resonance |
| [Spectral Resonator](docs/modules/SpectralResonator.md) | 6-band resonant filter bank for spectral chord processing |

### Effects

| Module | Description |
|--------|-------------|
| [Big Reverb](docs/modules/BigReverb.md) | Lush stereo reverb using the Zita Rev1 algorithm |
| [Saturation Echo](docs/modules/SaturationEcho.md) | Vintage tape delay with saturation and wobble |

## Installation

### From VCV Library (Recommended)

Search for "Wiggle Room" in the VCV Rack plugin manager.

### Manual Installation

1. Download the latest release for your platform from the [Releases](https://github.com/timini/WiggleRoom/releases) page
2. Extract to your VCV Rack plugins folder:
   - **macOS**: `~/Library/Application Support/Rack2/plugins-mac-arm64/` (or `plugins-mac-x64/`)
   - **Windows**: `%LOCALAPPDATA%/Rack2/plugins-win-x64/`
   - **Linux**: `~/.Rack2/plugins-lin-x64/`
3. Restart VCV Rack

## Documentation

Full documentation for each module is available in the [docs/modules](docs/modules/) folder:

- [Super Oscillator](docs/modules/SuperOsc.md) - Basic oscillator
- [Intersect](docs/modules/Intersect.md) - CV-driven rhythm generator
- [Cycloid](docs/modules/Cycloid.md) - Euclidean sequencer with spirograph
- [Moog LPF](docs/modules/MoogLPF.md) - Classic ladder filter
- [Modal Bell](docs/modules/ModalBell.md) - Physical modeling bell
- [Plucked String](docs/modules/PluckedString.md) - Karplus-Strong string
- [Big Reverb](docs/modules/BigReverb.md) - High-quality stereo reverb
- [Saturation Echo](docs/modules/SaturationEcho.md) - Tape delay emulation
- [Spectral Resonator](docs/modules/SpectralResonator.md) - Chord resonator

## Building from Source

Requires CMake 3.16+ and a C++17 compiler.

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
- Zita Rev1 reverb algorithm by Fons Adriaensen
