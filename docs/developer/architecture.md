# Module Architecture

WiggleRoom supports two module types, each with its own development pattern.

## Module Types

| Type | Description | Base Class | When to Use |
|------|-------------|------------|-------------|
| **Faust DSP** | Audio DSP in Faust language | `FaustModule<VCVRackDSP>` | Filters, synths, physical models |
| **Native C++** | Custom DSP in C++ | `rack::Module` | Complex algorithms, sequencers, utilities |

## Directory Structure

Each module lives in its own directory:

```
src/modules/
└── ModuleName/
    ├── ModuleName.cpp      # VCV Rack wrapper (required)
    ├── CMakeLists.txt      # Build configuration (required)
    ├── module_name.dsp     # Faust DSP (Faust modules only)
    └── test_config.json    # Test configuration (optional)
```

## Faust Module Architecture

```
┌──────────────────┐    ┌─────────────────┐    ┌──────────────────┐
│   module.dsp     │───▶│  Faust Compiler │───▶│  module.hpp      │
│   (Faust DSP)    │    │                 │    │  (Generated C++) │
└──────────────────┘    └─────────────────┘    └────────┬─────────┘
                                                        │
                                                        ▼
┌──────────────────┐    ┌─────────────────┐    ┌──────────────────┐
│  VCV Rack        │◀───│  FaustModule    │◀───│  VCVRackDSP      │
│  (Host)          │    │  (Base Class)   │    │  (Wrapper)       │
└──────────────────┘    └─────────────────┘    └──────────────────┘
```

### FaustModule Base Class

The `FaustModule<T>` template class (`src/common/FaustModule.hpp`) handles:

- Parameter mapping between VCV knobs and Faust parameters
- CV input modulation (V/Oct and linear)
- Audio I/O buffering
- Sample rate change handling
- JSON state serialization

#### Key Methods

| Method | Purpose |
|--------|---------|
| `mapParam(vcvId, faustIdx)` | Map VCV knob to Faust parameter by index |
| `mapCVInput(vcvId, faustIdx, exp, scale)` | Map CV input to modulate a Faust parameter |
| `setAudioIO(inputId, outputId)` | Set main audio I/O jacks |
| `updateFaustParams()` | Sync all mapped parameters to Faust DSP |

### Example: Faust Module Wrapper

```cpp
struct MyFilter : FaustModule<VCVRackDSP> {
    enum ParamId { CUTOFF_PARAM, RESONANCE_PARAM, PARAMS_LEN };
    enum InputId { AUDIO_INPUT, CUTOFF_CV_INPUT, INPUTS_LEN };
    enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    MyFilter() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(CUTOFF_PARAM, 20.f, 20000.f, 1000.f, "Cutoff", " Hz");
        configParam(RESONANCE_PARAM, 0.f, 0.95f, 0.f, "Resonance");
        configInput(AUDIO_INPUT, "Audio");
        configInput(CUTOFF_CV_INPUT, "Cutoff CV (V/Oct)");
        configOutput(AUDIO_OUTPUT, "Filtered Audio");

        setAudioIO(AUDIO_INPUT, AUDIO_OUTPUT);

        // Faust params alphabetically: cutoff=0, resonance=1
        mapParam(CUTOFF_PARAM, 0);
        mapParam(RESONANCE_PARAM, 1);

        // V/Oct exponential scaling for cutoff
        mapCVInput(CUTOFF_CV_INPUT, 0, true, 1.0f);
    }
};
```

## Native C++ Module Architecture

For custom DSP not using Faust, inherit directly from `rack::Module`:

```cpp
struct MyOscillator : rack::Module {
    enum ParamId { FREQ_PARAM, PARAMS_LEN };
    enum InputId { VOCT_INPUT, INPUTS_LEN };
    enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    float phase = 0.f;
    DSP::OnePoleLPF freqSmooth{0.01f};  // Parameter smoothing

    MyOscillator() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(FREQ_PARAM, 20.f, 20000.f, 440.f, "Frequency", " Hz");
        configInput(VOCT_INPUT, "V/Oct");
        configOutput(AUDIO_OUTPUT, "Audio");
    }

    void process(const ProcessArgs& args) override {
        float freq = params[FREQ_PARAM].getValue();

        // V/Oct modulation
        if (inputs[VOCT_INPUT].isConnected()) {
            freq *= DSP::voltToFreqMultiplier(inputs[VOCT_INPUT].getVoltage());
        }

        freq = freqSmooth.process(freq);

        // Generate oscillator
        float dt = freq / args.sampleRate;
        phase += dt;
        if (phase >= 1.f) phase -= 1.f;

        // Anti-aliased sawtooth using polyblep
        float saw = 2.f * phase - 1.f;
        saw -= DSP::polyblep(phase, dt);

        outputs[AUDIO_OUTPUT].setVoltage(saw * 5.f);
    }
};
```

## Shared DSP Utilities

`src/common/DSP.hpp` provides common utilities:

| Utility | Purpose |
|---------|---------|
| `voltToFreqMultiplier(v)` | Convert V/Oct to frequency multiplier |
| `midiToFreq(note)` | MIDI note number to frequency |
| `clamp(val, min, max)` | Clamp value to range |
| `lerp(a, b, t)` | Linear interpolation |
| `OnePoleLPF` | One-pole lowpass for parameter smoothing |
| `polyblep(t, dt)` | Polyblep anti-aliasing correction |

## VCV Rack Voltage Standards

| Signal Type | Range | Notes |
|-------------|-------|-------|
| Audio | ±5V | 10Vpp |
| CV (unipolar) | 0-10V | Gates, envelopes |
| CV (bipolar) | ±5V | LFO, modulation |
| V/Oct | 0V = C4 | 1V/octave |
| Gate | 0/10V | 0 = off, 10 = on |
| Trigger | 0→10V | Rising edge |

## File Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Module directory | PascalCase | `src/modules/MyModule/` |
| C++ file | PascalCase | `MyModule.cpp` |
| Faust DSP | snake_case | `my_module.dsp` |
| Generated header | snake_case | `my_module.hpp` |
| Panel SVG | PascalCase | `MyModule.svg` |

## Next Steps

- [Faust Guide](faust-guide.md) - Deep dive into Faust DSP
- [Adding Modules](adding-modules.md) - Step-by-step tutorial
