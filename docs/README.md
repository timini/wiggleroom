# WiggleRoom Documentation

Welcome to the WiggleRoom documentation. This guide covers everything from using the modules to contributing new ones.

## Documentation Structure

```
docs/
├── user/               # For module users
│   └── modules/        # Per-module documentation
├── developer/          # For contributors
│   ├── getting-started.md
│   ├── architecture.md
│   ├── faust-guide.md
│   ├── adding-modules.md
│   └── testing.md
├── ci/                 # CI/CD documentation
│   ├── pipeline.md
│   └── agents.md
├── specs/              # Design specifications
│   ├── acid9voice.md
│   ├── analog-drums.md
│   ├── euclogic.md
│   └── vektorx.md
└── RELEASE_STRATEGY.md # Plugin packs & releases
```

## Quick Links

### For Users

- [Module Documentation](user/modules/) - How to use each module
- [Installation](../README.md#installation) - Getting started

### For Developers

- **[Getting Started](developer/getting-started.md)** - Set up your development environment
- [Architecture](developer/architecture.md) - Module types and patterns
- [Faust DSP Guide](developer/faust-guide.md) - Writing Faust modules
- [Adding Modules](developer/adding-modules.md) - Step-by-step tutorial
- [Testing](developer/testing.md) - Test framework and quality checks

### For Maintainers

- [CI Pipeline](ci/pipeline.md) - Verification and validation stages
- [Agent System](ci/agents.md) - Automated development loop
- [Release Strategy](RELEASE_STRATEGY.md) - Plugin packs and pricing

## Testing Terminology

This project uses ISO 9001 / software engineering standard terminology:

| Term | Question | What It Checks |
|------|----------|----------------|
| **Verification** | "Are we building the product right?" | Structural correctness, conformance to specs |
| **Validation** | "Are we building the right product?" | Audio quality, fitness for purpose |

## Quick Commands

```bash
# Build
just build              # Compile the plugin
just install            # Install to VCV Rack

# Verification (fast, no build)
just verify             # All verification checks

# Validation (requires build)
just validate-modules   # Audio quality tests

# Full pipeline
just ci                 # verify -> build -> validate
```
