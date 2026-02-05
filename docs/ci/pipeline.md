# CI Pipeline

The project uses GitHub Actions for continuous integration with a multi-stage pipeline.

## Git Workflow

**The `main` branch is protected.** All development happens on feature branches.

```bash
# Start a new feature
git checkout main && git pull
git checkout -b feature/my-feature

# Develop and test locally
just build
just test

# Push and create PR
git push -u origin feature/my-feature
gh pr create
```

**CI runs on all PRs to main.** PRs must pass all checks before merging.

## Terminology (ISO 9001)

| Term | Question | What It Checks |
|------|----------|----------------|
| **Verification** | "Are we building the product right?" | Structural correctness, conformance to specs |
| **Validation** | "Are we building the right product?" | Audio quality, fitness for purpose |

## Pipeline Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        CI PIPELINE                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ╔═══════════════════════════════════════════════════════════╗  │
│  ║  VERIFICATION - "Are we building the product right?"      ║  │
│  ╠═══════════════════════════════════════════════════════════╣  │
│  ║  Stage 1: Verify Manifest (JSON conformance)              ║  │
│  ║  Stage 2: Verify Test Infra (unit tests for tools)        ║  │
│  ║  Stage 3: Verify Build (Faust + C++ compilation)          ║  │
│  ╚═══════════════════════════════════════════════════════════╝  │
│                            │                                     │
│                            ▼                                     │
│  ╔═══════════════════════════════════════════════════════════╗  │
│  ║  VALIDATION - "Are we building the right product?"        ║  │
│  ╠═══════════════════════════════════════════════════════════╣  │
│  ║  Stage 4: Validate Audio (THD, clipping, stability)       ║  │
│  ║  Stage 4b: AI Quality Gate (CLAP analysis, fast)          ║  │
│  ║  Stage 5: Validate Report (AI analysis, spectrograms)     ║  │
│  ╚═══════════════════════════════════════════════════════════╝  │
│                            │                                     │
│                            ▼                                     │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  Stage 6: Deploy (GitHub Pages) - main branch only        │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Stage 1: Verify Manifest

Fast pre-build checks for `plugin.json`.

| Check | Description | Severity |
|-------|-------------|----------|
| **Valid Tags** | All tags from VCV's 57 official tags | ERROR |
| **Trademark Detection** | No brand names (Moog, Roland, etc.) | ERROR |
| **Module Sync** | All modules in code match manifest | ERROR |
| **Slug Format** | Valid identifiers | ERROR |

```bash
just verify-manifest
just verify-manifest-tests
```

## Stage 2: Verify Test Infrastructure

Tests the test tools themselves.

| Tests | What It Verifies |
|-------|------------------|
| 32 utility tests | Path resolution, config loading, audio stats |
| 31 audio quality tests | THD, aliasing, envelope detection |

```bash
just verify-test-infra
```

## Stage 3: Verify Build

Compiles all code.

```bash
just test-faust    # Faust DSP compilation
just build         # Full plugin build
```

## Stage 4: Validate Audio Quality

Runs module tests.

| Test | What It Checks |
|------|----------------|
| `compilation` | Module loads |
| `basic_render` | No NaN/Inf |
| `audio_stability` | DC, clipping, silence |
| `gate_response` | Instruments respond to gate |
| `pitch_tracking` | V/Oct accuracy |
| `parameter_sensitivity` | All params affect output |

```bash
just validate-modules
```

## Stage 4b: AI Quality Gate

Fast CLAP-based quality analysis (no API key needed).

| Check | Threshold | Description |
|-------|-----------|-------------|
| CLAP quality | ≥50 | AI embedding quality score |
| THD | ≤20% | Total harmonic distortion |
| Clipping | ≤5% | Sample clipping percentage |
| HNR | ≥-5 dB | Harmonic-to-noise ratio |

```bash
just validate-ci            # All modules
just validate-ci-module X   # Single module
```

Configuration: `test/ci_config.json`

## Stage 5: Validate Full Report

Generates detailed analysis with AI assessment.

```bash
just test-report-full  # Requires GEMINI_API_KEY
```

## Stage 6: Deploy

Deploys quality report to GitHub Pages (main branch only).

## Running Locally

```bash
# Individual stages
just verify-manifest         # Stage 1.1
just verify-manifest-tests   # Stage 1.2
just verify-test-infra       # Stage 2
just test-faust              # Stage 3.1
just build                   # Stage 3.2
just validate-modules        # Stage 4
just validate-ci             # Stage 4b (AI quality gate)
just test-report-full        # Stage 5

# Combined
just verify                  # All verification (no build)
just ci                      # Full pipeline
just test                    # Same as ci
```

## Failure Modes

| Stage | Failure | Effect |
|-------|---------|--------|
| 1. Verify Manifest | Invalid tags/trademarks | Blocks all |
| 2. Verify Test Infra | Broken tools | Blocks build |
| 3. Verify Build | Compilation error | Blocks validation |
| 4. Validate Audio | Quality issues | Blocks merge |
| 4b. AI Quality Gate | Quality below threshold | Non-blocking (warning) |
| 5. Validate Report | Generation issues | Non-blocking |
| 6. Deploy | Pages issues | Non-blocking |

## Trigger Conditions

```yaml
on:
  pull_request:
    branches: [main]   # CI runs on all PRs
  push:
    branches: [main]   # Deploy to Pages after merge
```

**All development uses feature branches.** Direct pushes to main are blocked.

## Branch Protection

The `main` branch should be protected with these rules:

**To configure (GitHub → Settings → Branches → Add rule):**

| Setting | Value |
|---------|-------|
| Branch name pattern | `main` |
| Require PR before merging | ✓ |
| Require status checks | ✓ |
| Required checks | `Stage 1: Verify Manifest`, `Stage 3: Verify Build`, `Stage 4: Validate Audio Quality` |
| Require branches to be up to date | ✓ |
| Do not allow bypassing | ✓ (recommended) |

## Required Secrets

| Secret | Required For |
|--------|--------------|
| `GEMINI_API_KEY` | Stage 5 AI analysis |

## VCV Library Submission Checklist

Before submitting:

- [ ] `just verify-manifest` passes
- [ ] `just verify-manifest-tests` passes (33 tests)
- [ ] `just verify-test-infra` passes (63 tests)
- [ ] `just build` succeeds
- [ ] `just validate-modules` passes
- [ ] No trademark names
- [ ] All tags official
- [ ] All modules synced
