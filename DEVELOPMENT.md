# WiggleRoom Developer Guide

This guide has been reorganized into focused documents for easier navigation.

## Documentation Structure

| Document | Description |
|----------|-------------|
| **[Getting Started](docs/developer/getting-started.md)** | Set up development environment |
| **[Architecture](docs/developer/architecture.md)** | Module types and patterns |
| **[Faust Guide](docs/developer/faust-guide.md)** | Faust DSP conventions |
| **[Adding Modules](docs/developer/adding-modules.md)** | Step-by-step tutorial |
| **[Testing](docs/developer/testing.md)** | Test framework and quality checks |
| **[CI Pipeline](docs/ci/pipeline.md)** | Verification and validation stages |
| **[Agent System](docs/ci/agents.md)** | Automated development loop |

## Quick Start

```bash
# Clone and build
git clone https://github.com/timini/WiggleRoom.git
cd WiggleRoom
just build
just install
```

## Quick Commands

```bash
# Build
just build              # Compile the plugin
just install            # Install to VCV Rack

# Verification (fast, no build)
just verify             # All verification checks
just verify-manifest    # Check plugin.json

# Validation (requires build)
just validate-modules   # Audio quality tests
just validate ModuleName # Full validation for one module

# Full pipeline
just ci                 # verify -> build -> validate
just test               # Same as ci
```

## Testing Terminology

This project uses ISO 9001 terminology:

| Term | Question | Checks |
|------|----------|--------|
| **Verification** | "Building it right?" | Structural correctness |
| **Validation** | "Building the right thing?" | Audio quality |

## Full Documentation

See the **[Documentation Index](docs/README.md)** for complete documentation.
