# Verifier Agent

Build modules and run comprehensive tests, returning structured results.

## When to Use

Use this agent when you need to:
- Verify a module builds successfully
- Run audio quality tests on a module
- Analyze parameter ranges for issues
- Get structured test results for further evaluation

## Capabilities

- **Build verification**: Compiles the plugin and reports any build errors
- **Render testing**: Tests basic audio rendering with gate input
- **Quality analysis**: Runs THD, clipping, HNR, and spectral analysis
- **Parameter sweep**: Tests all parameter ranges for issues (clipping, silence, instability)
- **AI analysis**: Optional CLAP-based audio quality assessment

## Usage

```bash
# Full verification (all tests)
python3 test/agents/verifier_agent.py ModuleName -v

# JSON output for programmatic use
python3 test/agents/verifier_agent.py ModuleName --json

# Skip specific test types
python3 test/agents/verifier_agent.py ModuleName --no-quality --no-params --no-ai
```

## Output Structure

The verifier returns a `VerificationResult` with:

```json
{
  "module_name": "ChaosFlute",
  "success": true,
  "build_success": true,
  "build_error": "",
  "config": {
    "module_type": "instrument",
    "skip_audio_tests": false,
    "thd_max_percent": 25.0,
    "clipping_max_percent": 5.0,
    "allow_hot_signal": true
  },
  "render": {
    "peak_amplitude": 0.85,
    "rms_level": 0.3,
    "clipping_percent": 0.5,
    "is_silent": false
  },
  "quality": {
    "overall_score": 75,
    "thd_percent": 12.5,
    "hnr_db": 15.2,
    "issues": []
  },
  "parameter_issues": [],
  "safe_params": ["freq", "pressure", "attack"],
  "ai": {
    "clap_score": 72,
    "top_positive": [["musical instrument", 0.85]]
  }
}
```

## Success Criteria

A module passes verification when:
1. Build succeeds
2. Audio is not silent (RMS > 0.001)
3. Clipping is below module threshold (from test_config.json)
4. Quality score >= 70

## Integration

This agent is typically called by:
1. **Direct invocation**: `just agent-verify ModuleName`
2. **Orchestrator**: As first step in development loop
3. **CI/CD**: For automated testing

Results are passed to the Judge agent for evaluation and fix prioritization.

## Module Configuration

The verifier reads `src/modules/ModuleName/test_config.json` for:
- Module type (instrument, filter, effect, utility)
- Quality thresholds (THD, clipping limits)
- Whether to skip audio tests (for utility modules)

See [DEVELOPMENT.md](../../DEVELOPMENT.md#module-test-configuration) for config format.
