# Getting Started

This guide will help you set up your development environment and build WiggleRoom from source.

## Prerequisites

- **CMake** 3.15+
- **C++17** compiler (GCC 9+, Clang 10+, MSVC 2019+)
- **just** command runner ([install](https://github.com/casey/just))
- **Faust** (optional, for DSP development) ([install](https://faust.grame.fr/))
- **Python 3.8+** (for tests)

## Quick Start

```bash
# Clone the repository
git clone https://github.com/timini/WiggleRoom.git
cd WiggleRoom

# Build
just build

# Install to VCV Rack
just install

# Restart VCV Rack to load the plugin
```

## Project Structure

```
WiggleRoom/
├── src/
│   ├── common/           # Shared utilities (FaustModule, DSP.hpp)
│   ├── modules/          # Module source code
│   │   └── ModuleName/
│   │       ├── ModuleName.cpp
│   │       ├── module_name.dsp
│   │       ├── CMakeLists.txt
│   │       └── test_config.json
│   └── plugin.cpp        # Plugin entry point
├── res/                  # Panel graphics (SVG/PNG)
├── test/                 # Test framework
├── docs/                 # Documentation
├── Justfile              # Build automation
└── plugin.json           # Plugin manifest
```

## Development Workflow

### 1. Make Changes

Edit the module source code in `src/modules/ModuleName/`.

### 2. Build

```bash
just build
```

### 3. Test

```bash
# Quick verification (no build)
just verify

# Full validation (requires build)
just validate-modules ModuleName
```

### 4. Install & Test in VCV Rack

```bash
just install
# Restart VCV Rack
```

### 5. Run Full CI Pipeline Locally

```bash
just ci
```

## Key Commands

| Command | Description |
|---------|-------------|
| `just build` | Build the plugin |
| `just install` | Install to VCV Rack |
| `just clean` | Remove build artifacts |
| `just verify` | Run verification checks |
| `just validate-modules` | Run audio quality tests |
| `just test` | Full test suite |
| `just ci` | Full CI pipeline |

## Next Steps

- [Architecture](architecture.md) - Understand module types
- [Faust Guide](faust-guide.md) - Write DSP in Faust
- [Adding Modules](adding-modules.md) - Create your first module
- [Testing](testing.md) - Test your modules
