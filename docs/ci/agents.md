# Agent-Based Development

WiggleRoom includes a multi-agent system for structured Faust module development.

## Overview

The agents automate verification, quality evaluation, and provide actionable fix instructions.

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│  Verifier Agent │────▶│   Judge Agent   │────▶│ Faust Dev Agent │
│                 │     │                 │     │    (Claude)     │
│ - Build         │     │ - Evaluate      │     │                 │
│ - Render tests  │     │ - Prioritize    │     │ - Read DSP      │
│ - Quality tests │     │ - Score         │     │ - Apply fixes   │
│ - Param sweep   │     │ - Generate fix  │     │ - Rebuild       │
│ - AI analysis   │     │   instructions  │     │                 │
└─────────────────┘     └─────────────────┘     └─────────────────┘
         │                                               │
         └───────────────────────────────────────────────┘
                        (iterate until pass)
```

## Agent Definitions

Agent prompts are in `.claude/agents/`:

| Agent | File | Purpose |
|-------|------|---------|
| **Verifier** | `verifier.md` | Build and run tests |
| **Judge** | `judge.md` | Evaluate and prioritize |
| **Dev** | `dev-agent.md` | Apply DSP fixes |

## Commands

```bash
# Single verification + judgment
just agent-verify ModuleName

# Get fix instructions
just agent-fix ModuleName

# Full loop (iterates until pass)
just agent-loop ModuleName [iterations]

# JSON output
just agent-json ModuleName
```

## Development Workflow

1. **Run verification**: `just agent-fix ModuleName`
2. **Receive fix instructions** with priority issues
3. **Read the DSP file**
4. **Apply targeted fixes** (CRITICAL first, then HIGH)
5. **Rebuild**: `just build`
6. **Re-verify**: `just agent-verify ModuleName`
7. **Repeat** until PASS

## Severity Levels

| Severity | Action | Examples |
|----------|--------|----------|
| **CRITICAL** | Fix immediately | Build failure, silent output, clipping >15% |
| **HIGH** | Fix before release | Clipping 5-15%, quality score <60 |
| **MEDIUM** | Should fix | Parameter issues, AI-detected problems |
| **LOW** | Optional | Minor suggestions |

## Pass Thresholds

| Metric | Default | Notes |
|--------|---------|-------|
| Build | Must succeed | No errors |
| Clipping | <5% | Or per-module config |
| Quality Score | ≥70 | Combined metric |
| THD | <15% | Higher OK for distortion |
| HNR | >0 dB | Harmonic-to-noise |

## Slash Command

Use `/dsp-fix ModuleName` to automate the full loop.

## Python Implementation

```python
from test.agents import Orchestrator, run_development_loop

# Quick development loop
session = run_development_loop("ChaosFlute")

# With more control
orchestrator = Orchestrator(max_iterations=10)
judgment, verification = orchestrator.run_iteration("ChaosFlute", verbose=True)

if judgment.verdict != "pass":
    fix_instructions = orchestrator.generate_fix_instructions("ChaosFlute", judgment)
    print(fix_instructions.to_prompt())
```

## Files

| File | Description |
|------|-------------|
| `test/agents/verifier_agent.py` | Verification logic |
| `test/agents/judge_agent.py` | Evaluation logic |
| `test/agents/orchestrator.py` | Loop coordination |
