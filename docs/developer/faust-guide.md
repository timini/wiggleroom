# Faust DSP Guide

This guide covers Faust DSP conventions for WiggleRoom modules.

## Parameter Declaration

Use `hslider()` with `si.smoo` for smoothed parameters:

```faust
import("stdfaust.lib");

// Parameters are indexed ALPHABETICALLY by Faust
cutoff = hslider("cutoff", 1000, 20, 20000, 1) : si.smoo;
resonance = hslider("resonance", 0, 0, 0.95, 0.01) : si.smoo;

process = ve.moog_vcf_2bn(resonance, cutoff);
```

**Important**: Faust indexes parameters alphabetically by name, not by declaration order.

## Parameter Index Reference

To find Faust parameter indices, list them alphabetically:

```faust
// Example Faust DSP with these parameters:
decay = hslider("decay", 0.5, 0.1, 2.0, 0.01);
gate = hslider("gate", 0, 0, 10, 0.01);
hardness = hslider("hardness", 0.5, 0.01, 1.0, 0.01);
volts = hslider("volts", 0, -5, 10, 0.001);

// Alphabetical order: decay=0, gate=1, hardness=2, volts=3
```

Or use the test harness to list them:
```bash
./build/test/faust_render --module ModuleName --list-params
```

## Gate/Trigger Handling

For instruments with gate inputs:

```faust
// Gate input (0-10V from VCV Rack)
gate = hslider("gate", 0, 0, 10, 0.01);

// Trigger on rising edge crossing threshold
// Use 0.9 threshold (slightly below 1V) for test framework compatibility
trig = (gate > 0.9) & (gate' <= 0.9);

// Envelope
env = en.ar(0.01, decay, trig);
```

## V/Oct Pitch Tracking

Standard V/Oct implementation (0V = C4 = 261.62 Hz):

```faust
volts = hslider("volts", 0, -5, 10, 0.001);
freq = max(20, 261.62 * (2.0 ^ volts));
```

## Stability Measures

Always include protection for numerical stability:

```faust
// Clamp Q factor to prevent instability
safe_resonbp(freq, q, g) = fi.resonbp(freq, max(1, q), g);

// DC blocker for stability
dc_block = fi.dcblocker;

// Soft limiter to prevent clipping
soft_limit = ma.tanh;

// Chain protections in output
process = my_synth : *(gain) : dc_block : soft_limit;
```

## Common Issues and Fixes

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| Very low output | Gain staging issue | Increase `output_gain` or excitation |
| Clicks at onset | Instant envelope attack | Add smoothing: `si.smooth(0.99)` |
| Physical model silent | Insufficient excitation | Boost input signal, adjust feedback |
| Harsh high frequencies | Aliasing or no filtering | Add lowpass: `fi.lowpass(2, 8000)` |
| DC offset | No DC blocking | Add `fi.dcblocker` in signal chain |
| Clipping | Too much gain | Add soft limiter: `ma.tanh` or reduce gain |

## Custom Process Override (C++)

For modules needing custom processing (e.g., instruments with gate/V/Oct):

```cpp
void process(const ProcessArgs& args) override {
    if (!initialized) {
        faustDsp.init(static_cast<int>(args.sampleRate));
        initialized = true;
    }

    // Update mapped parameters (knobs + CV)
    updateFaustParams();

    // Set unmapped parameters directly
    // Faust params alphabetically: gate=1, volts=7
    faustDsp.setParamValue(1, inputs[GATE_INPUT].getVoltage());
    faustDsp.setParamValue(7, inputs[VOCT_INPUT].getVoltage());

    // Process audio
    float* nullInputs[1] = { nullptr };
    float outputL = 0.f, outputR = 0.f;
    float* outputPtrs[2] = { &outputL, &outputR };

    faustDsp.compute(1, nullInputs, outputPtrs);

    outputs[LEFT_OUTPUT].setVoltage(outputL * 5.0f);
    outputs[RIGHT_OUTPUT].setVoltage(outputR * 5.0f);
}
```

## Best Practices

1. **Always use `si.smoo`** for parameters that affect audio to prevent zipper noise
2. **Use `fi.dcblocker`** in feedback loops and at output
3. **Add `ma.tanh` limiting** for safety, especially in feedback systems
4. **Test with extreme parameters** to catch clipping and instability
5. **Use 0.9 gate threshold** for test framework compatibility

## Next Steps

- [Adding Modules](adding-modules.md) - Step-by-step tutorial
- [Testing](testing.md) - Validate your DSP
