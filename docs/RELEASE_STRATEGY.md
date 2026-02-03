# WiggleRoom Release Strategy

## Plugin Pack Structure

WiggleRoom modules are organized into **thematic packs**, each with **Free** and **Pro** tiers.

### Pack Overview

| Pack | Description | Target Users |
|------|-------------|--------------|
| **Voices** | Oscillators, synthesizers, physical models | Sound designers, ambient/drone producers |
| **Effects** | Reverbs, delays, filters, waveshapers | Mixing, sound design |
| **Modulators** | LFOs, clocks, sequencers, quantizers | Patch architects, generative music |

### Module Assignment

#### Voices Pack (14 modules)

| Module | Tier | Rationale |
|--------|------|-----------|
| PluckedString | Free | Classic Karplus-Strong, good entry point |
| ModalBell | Free | Accessible physical model, demonstrates quality |
| ChaosFlute | Pro | Unique, complex instrument |
| SpaceCello | Pro | Advanced physical model |
| TheAbyss | Pro | Niche but unique (waterphone) |
| Matter | Pro | Complex resonant body model |
| VektorX | Pro | Flagship drone synth |
| ACID9Voice | Pro | TB-303 inspired acid synth |
| TR808 | Pro | Classic drum machine emulation |
| TetanusCoil | Pro | Chaotic oscillator |
| NutShaker | Pro | Physical shaker model |
| PhysicalChoir | Pro | Vocal choir synthesis |
| Linkage | Pro | Chaotic percussion generator |
| ChaosPad | Pro | XY pad with chaos effects |

#### Effects Pack (7 modules)

| Module | Tier | Rationale |
|--------|------|-----------|
| LadderLPF | Free | Classic filter, essential utility |
| BigReverb | Free | High-quality reverb attracts users |
| InfiniteFolder | Free | West coast essential |
| SaturationEcho | Pro | Premium tape delay |
| TriPhaseEnsemble | Pro | Vintage string ensemble |
| SpectralResonator | Pro | Advanced spectral processing |
| TheCauldron | Pro | Unique wave math processor |

#### Modulators Pack (8 modules)

| Module | Tier | Rationale |
|--------|------|-----------|
| GravityClock | Free | Unique physics-based, eye-catching |
| OctoLFO | Free | Practical utility, 8 channels |
| Intersect | Free | Useful trigger generator |
| Cycloid | Pro | Visual Euclidean sequencer |
| TheArchitect | Pro | Advanced poly-quantizer |
| Euclogic | Pro | 4-channel Euclidean sequencer |
| Euclogic2 | Free | Compact 2-channel Euclidean |
| ACID9Seq | Pro | Companion sequencer for ACID9Voice |

### Summary

| Pack | Free Modules | Pro Modules | Total |
|------|--------------|-------------|-------|
| Voices | 2 | 12 | 14 |
| Effects | 3 | 4 | 7 |
| Modulators | 4 | 4 | 8 |
| **Total** | **9** | **20** | **29** |

### Visual Overview

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         WIGGLE ROOM PACKS                                 │
├────────────────────────┬─────────────────────┬────────────────────────────┤
│        VOICES          │       EFFECTS       │        MODULATORS          │
├──────────┬─────────────┼──────────┬──────────┼─────────────┬──────────────┤
│   FREE   │     PRO     │   FREE   │   PRO    │    FREE     │     PRO      │
├──────────┼─────────────┼──────────┼──────────┼─────────────┼──────────────┤
│ Plucked  │ ChaosFlute  │ LadderLPF│ Satura-  │ GravityClock│ Cycloid      │
│ String   │ SpaceCello  │ BigReverb│ tionEcho │ OctoLFO     │ TheArchitect │
│ ModalBell│ TheAbyss    │ Infinite │ TriPhase │ Intersect   │ Euclogic     │
│          │ Matter      │ Folder   │ Ensemble │ Euclogic2   │ ACID9Seq     │
│          │ VektorX     │          │ Spectral │             │              │
│          │ ACID9Voice  │          │ Resonator│             │              │
│          │ TR808       │          │ The      │             │              │
│          │ TetanusCoil │          │ Cauldron │             │              │
│          │ NutShaker   │          │          │             │              │
│          │ Physical    │          │          │             │              │
│          │ Choir       │          │          │             │              │
│          │ Linkage     │          │          │             │              │
│          │ ChaosPad    │          │          │             │              │
├──────────┼─────────────┼──────────┼──────────┼─────────────┼──────────────┤
│    2     │     12      │    3     │    4     │      4      │      4       │
└──────────┴─────────────┴──────────┴──────────┴─────────────┴──────────────┘
              9 Free                         20 Pro
```

---

## Plugin Naming Convention

```
WiggleRoom Voices          # Free voice modules
WiggleRoom Voices Pro      # Paid voice modules (requires Voices Free)

WiggleRoom Effects         # Free effect modules
WiggleRoom Effects Pro     # Paid effect modules

WiggleRoom Modulators      # Free modulator modules
WiggleRoom Modulators Pro  # Paid modulator modules
```

### Plugin Slugs

| Plugin | Slug | plugin.json name |
|--------|------|------------------|
| Voices Free | `WiggleRoom-Voices` | "Wiggle Room Voices" |
| Voices Pro | `WiggleRoom-Voices-Pro` | "Wiggle Room Voices Pro" |
| Effects Free | `WiggleRoom-Effects` | "Wiggle Room Effects" |
| Effects Pro | `WiggleRoom-Effects-Pro` | "Wiggle Room Effects Pro" |
| Modulators Free | `WiggleRoom-Modulators` | "Wiggle Room Modulators" |
| Modulators Pro | `WiggleRoom-Modulators-Pro` | "Wiggle Room Modulators Pro" |

---

## Repository Structure (Future)

```
WiggleRoom/
├── plugins/
│   ├── voices/
│   │   ├── free/
│   │   │   └── plugin.json
│   │   └── pro/
│   │       └── plugin.json
│   ├── effects/
│   │   ├── free/
│   │   │   └── plugin.json
│   │   └── pro/
│   │       └── plugin.json
│   └── modulators/
│       ├── free/
│       │   └── plugin.json
│       └── pro/
│           └── plugin.json
├── src/
│   ├── common/              # Shared code across all packs
│   └── modules/             # All modules (assigned to packs via config)
├── res/                     # All panel graphics
├── CMakeLists.txt          # Root build config
└── Justfile                # Build commands for each pack
```

### Build Commands (Future)

```bash
# Build specific pack
just build-voices-free
just build-voices-pro
just build-effects-free
just build-effects-pro
just build-modulators-free
just build-modulators-pro

# Build all free packs
just build-free

# Build all pro packs (requires license)
just build-pro

# Package for release
just package-voices-free
just package-all
```

---

## Release Phases

### Phase 1: Beta (Current → Q1)
- [ ] Release all modules as single "WiggleRoom" plugin
- [ ] Gather community feedback on VCV Forum, Reddit
- [ ] Iterate on DSP quality and UI
- [ ] Build awareness and user base

### Phase 2: Pack Split (Q2)
- [ ] Restructure repo for multi-pack builds
- [ ] Release Free packs to VCV Library
- [ ] Beta test Pro packs with select users

### Phase 3: Pro Launch (Q3)
- [ ] Submit Pro packs to VCV Plugin Store
- [ ] Create demo videos and documentation
- [ ] Pricing: ~$15-20 per Pro pack, or ~$40 bundle

---

## Pricing Strategy (Draft)

| Option | Price | Notes |
|--------|-------|-------|
| Individual Pro Pack | $15-20 | Voices Pro, Effects Pro, or Modulators Pro |
| Pro Bundle (all 3) | $40 | ~30% discount |
| Free Packs | $0 | Always free, attracts users |

Comparable pricing in VCV ecosystem:
- Vult Premium: $30
- Geodesics: $20
- Valley: Free (donation)

---

## Distribution Channels

| Channel | Free Packs | Pro Packs |
|---------|------------|-----------|
| VCV Library | ✓ | - |
| VCV Plugin Store | - | ✓ |
| GitHub Releases | ✓ (beta) | - |

---

## Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2025-01-21 | Thematic packs (Voices/Effects/Modulators) | Better organization, users buy what they need |
| 2025-01-21 | Free + Pro tiers per pack | Free modules attract users, Pro generates revenue |
| 2025-01-21 | Monorepo with multi-pack builds | Keep code together, easier maintenance |
| 2026-02-03 | Updated to 29 modules | Added ACID9Voice, TR808, TetanusCoil, NutShaker, PhysicalChoir, Linkage, ChaosPad, Euclogic, Euclogic2, ACID9Seq |
