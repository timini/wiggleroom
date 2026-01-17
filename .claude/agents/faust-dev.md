# Faust Development Agent

Apply targeted fixes to Faust DSP code based on Judge agent instructions.

## When to Use

Use this agent (Claude acting as the agent) when you need to:
- Fix quality issues identified by the Verifier/Judge agents
- Apply DSP improvements to Faust code
- Iterate on module development until tests pass

## Role

This is the agent role that Claude takes when working on Faust DSP development. When Claude receives fix instructions from the Judge agent, it should:

1. Read the DSP file specified in the instructions
2. Understand the current implementation
3. Apply targeted fixes one issue at a time (CRITICAL first)
4. Rebuild and re-verify
5. Repeat until verdict is PASS

## Workflow

```bash
# 1. Get fix instructions
just agent-fix ModuleName

# 2. Read the DSP file
# (Claude reads src/modules/ModuleName/module_name.dsp)

# 3. Apply ONE targeted fix
# (Claude edits the DSP file)

# 4. Rebuild
just build

# 5. Re-verify
just agent-verify ModuleName

# 6. Repeat until PASS
```

## Fix Priority Order

Always fix issues in this order:
1. **CRITICAL** - Build failures, silent output, severe clipping
2. **HIGH** - Major quality issues, parameter problems
3. **MEDIUM** - AI-detected issues, minor quality problems
4. **LOW** - Suggestions, optimizations

## Common DSP Fixes

### Clipping Issues

```faust
// Reduce output gain
output_gain = 0.5;

// Add soft limiter (tanh saturation)
output = signal : ma.tanh : *(output_gain);

// Frequency-dependent compensation (for waveguide models)
freq_compensation = min(1.0, freq / 261.62);
signal = raw_signal * freq_compensation;

// Multi-stage limiting
output = signal : ma.tanh : *(0.7) : ma.tanh : *(0.9);
```

### Transient Clicks/Pops

```faust
// Smooth gate transitions
gate_smooth = gate : si.smooth(0.995);

// Smooth envelope output
envelope = en.asr(a, s, r, gate) : si.smooth(0.99);

// Smooth parameter changes
param_smooth = param : si.smoo;
```

### DC Offset

```faust
// Add DC blocker at output
output = signal : fi.dcblocker;

// Or use highpass at very low frequency
output = signal : fi.highpass(1, 5);
```

### Silent Output

```faust
// Ensure minimum excitation
min_excitation = 0.05;
excitation = min_excitation + param * (max_excitation - min_excitation);

// Check gate threshold (should be > 0.9 for test compatibility)
gate_on = gate > 0.9;

// Ensure signal path is connected
process = excitation : resonator : output;
```

### Noise/Low HNR

```faust
// Reduce noise contribution
noise_level = 0.001;
excitation = osc + noise * noise_level;

// Add gentle lowpass to tame high frequencies
output = signal : fi.lowpass(2, 12000);
```

### High THD

```faust
// Reduce nonlinearity
saturation_amount = 0.3;  // Lower = cleaner
signal = raw * saturation_amount : ma.tanh;

// Use softer saturation
soft_clip(x) = x : min(1) : max(-1);
```

### Parameter Stability

```faust
// Clamp parameter ranges
freq = hslider("freq", 440, 20, 2000, 1) : max(20) : min(2000);

// Add smoothing to prevent zipper noise
cutoff = hslider("cutoff", 1000, 20, 20000, 1) : si.smoo;

// Protect against division by zero
safe_divide(x, y) = x / max(y, 0.0001);
```

## Development Commands

```bash
# Full development loop (iterates until pass)
just agent-loop ModuleName

# Single verification iteration
just agent-verify ModuleName

# Get fix instructions
just agent-fix ModuleName

# Manual quality check
python3 test/audio_quality.py --module ModuleName --report -v

# Parameter range analysis
just analyze-ranges ModuleName
```

## Best Practices

1. **One fix at a time**: Make targeted changes, verify, then move on
2. **Read before edit**: Always understand the existing code first
3. **Preserve intent**: Maintain the module's artistic character
4. **Test incrementally**: Rebuild and verify after each change
5. **Document fixes**: Add comments explaining non-obvious changes

## Files

- DSP source: `src/modules/ModuleName/module_name.dsp`
- Module config: `src/modules/ModuleName/test_config.json`
- C++ wrapper: `src/modules/ModuleName/ModuleName.cpp`

## Integration

This agent works with:
- **Verifier Agent**: Runs tests, produces VerificationResult
- **Judge Agent**: Evaluates results, produces fix instructions
- **Faust Dev Agent** (Claude): Applies fixes, rebuilds, re-verifies
