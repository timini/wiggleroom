# Wiggle Room

A collection of creative modules for [VCV Rack 2](https://vcvrack.com/), built with [Faust](https://faust.grame.fr/) physical modeling DSP.

## Modules

| Module | Description |
|--------|-------------|
| **Matter** | Physical model of a struck solid inside a resonant tube — morphs from strings to bells with acoustic chamber |
| **ACID9 Voice** | TB-303 inspired acid synthesizer voice with tri-core morphing oscillator, dual filters, stereo delay, and accent/slide control |

More modules are in development.

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
