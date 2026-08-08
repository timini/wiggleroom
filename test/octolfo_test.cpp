/**
 * OctoLFO Test Executable
 *
 * Standalone test tool for OctoLFO module that can:
 * - Instantiate the module and inspect internal state
 * - Simulate VCV Rack processing (clock, parameters)
 * - Output JSON for verification by Python tests
 *
 * This enables TDD testing of a native C++ VCV Rack module.
 *
 * Usage:
 *   ./octolfo_test --list-master-rate-values
 *   ./octolfo_test --list-channel-rate-values
 *   ./octolfo_test --test-scope-buffer
 *   ./octolfo_test --test-combined-rate --master-index=8 --channel-index=10
 */

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <sstream>

// We need to include the OctoLFO implementation
// Define the necessary VCV Rack types as stubs for testing
namespace rack {
    struct Plugin;

    // Minimal Module base class stub
    struct Module {
        struct Param { float value = 0.f; float getValue() const { return value; } void setValue(float v) { value = v; } };
        struct Input {
            float voltage = 0.f;
            bool connected = false;
            float getVoltage() const { return voltage; }
            void setVoltage(float v) { voltage = v; }
            bool isConnected() const { return connected; }
            void setConnected(bool c) { connected = c; }
        };
        struct Output { float voltage = 0.f; float getVoltage() const { return voltage; } void setVoltage(float v) { voltage = v; } };
        struct Light { float brightness = 0.f; void setBrightness(float b) { brightness = b; } };

        std::vector<Param> params;
        std::vector<Input> inputs;
        std::vector<Output> outputs;
        std::vector<Light> lights;

        void config(int numParams, int numInputs, int numOutputs, int numLights) {
            params.resize(numParams);
            inputs.resize(numInputs);
            outputs.resize(numOutputs);
            lights.resize(numLights);
        }

        void configParam(int id, float min, float max, float def, const std::string& name) {
            if (id >= 0 && id < (int)params.size()) params[id].value = def;
        }
        void configSwitch(int id, float min, float max, float def, const std::string& name, const std::vector<std::string>& labels = {}) {
            if (id >= 0 && id < (int)params.size()) params[id].value = def;
        }
        void configInput(int id, const std::string& name) {}
        void configOutput(int id, const std::string& name) {}
        void configLight(int id, const std::string& name) {}

        virtual void process(float sampleTime) {}
    };

    namespace dsp {
        struct SchmittTrigger {
            bool state = false;
            bool process(float in, float lo = 0.1f, float hi = 1.f) {
                if (state) {
                    if (in <= lo) { state = false; }
                } else {
                    if (in >= hi) { state = true; return true; }
                }
                return false;
            }
        };
    }

    template<typename T>
    T clamp(T x, T a, T b) { return std::max(a, std::min(b, x)); }
}

using namespace rack;

// Stub for extern Plugin* pluginInstance
Plugin* pluginInstance = nullptr;

// Now include the actual OctoLFO code (excluding the Widget parts)
namespace WiggleRoom {

// Wave types
enum WaveType {
    WAVE_SINE = 0,
    WAVE_TRIANGLE,
    WAVE_SAW_UP,
    WAVE_SAW_DOWN,
    WAVE_SQUARE,
    NUM_WAVE_TYPES
};

// Master rate multiplier values - extended to /128
static const std::vector<float> MASTER_RATE_VALUES = {
    1.f/128.f,     // /128 (NEW)
    1.f/64.f,      // /64 (NEW)
    1.f/32.f,      // /32 (NEW)
    1.f/16.f,      // /16
    1.f/12.f,      // /12
    1.f/8.f,       // /8
    1.f/6.f,       // /6
    1.f/4.f,       // /4
    1.f/3.f,       // /3
    1.f/2.f,       // /2
    2.f/3.f,       // /1.5 (2/3)
    1.f,           // x1 (index 11)
    3.f/2.f,       // x1.5
    2.f,           // x2
    3.f,           // x3
    4.f,           // x4
    6.f,           // x6
    8.f,           // x8
    12.f,          // x12
    16.f           // x16
};

static const std::vector<std::string> MASTER_RATE_LABELS = {
    "/128", "/64", "/32", "/16", "/12", "/8", "/6", "/4", "/3", "/2", "/1.5",
    "x1", "x1.5", "x2", "x3", "x4", "x6", "x8", "x12", "x16"
};

// Channel rate multiplier values - with primes and odds
static const std::vector<float> CHANNEL_RATE_VALUES = {
    1.f/16.f,      // /16
    1.f/12.f,      // /12
    1.f/9.f,       // /9 (NEW - odd)
    1.f/8.f,       // /8
    1.f/7.f,       // /7 (NEW - prime)
    1.f/6.f,       // /6
    1.f/5.f,       // /5 (NEW - prime)
    1.f/4.f,       // /4
    1.f/3.f,       // /3
    1.f/2.f,       // /2
    2.f/3.f,       // /1.5 (2/3)
    1.f,           // x1 (index 11)
    3.f/2.f,       // x1.5
    2.f,           // x2
    3.f,           // x3
    4.f,           // x4
    5.f,           // x5 (NEW - prime)
    6.f,           // x6
    7.f,           // x7 (NEW - prime)
    8.f,           // x8
    9.f,           // x9 (NEW - odd)
    12.f,          // x12
    16.f           // x16
};

static const std::vector<std::string> CHANNEL_RATE_LABELS = {
    "/16", "/12", "/9", "/8", "/7", "/6", "/5", "/4", "/3", "/2", "/1.5",
    "x1", "x1.5", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x12", "x16"
};

// Fold values - musically interesting fractions
static const std::vector<float> FOLD_VALUES = {
    0.f,           // 0 - no fold
    1.f/16.f,      // 1/16
    1.f/8.f,       // 1/8
    1.f/6.f,       // 1/6
    1.f/4.f,       // 1/4
    1.f/3.f,       // 1/3
    1.f/2.f,       // 1/2
    2.f/3.f,       // 2/3
    3.f/4.f,       // 3/4
    1.f,           // 1
    5.f/4.f,       // 5/4
    4.f/3.f,       // 4/3
    3.f/2.f,       // 3/2
    2.f,           // 2
    3.f,           // 3
    4.f            // 4
};

struct OctoLFOTest : Module {
    static constexpr int NUM_LFOS = 8;
    static constexpr int SCOPE_BUFFER_SIZE = 256;  // 4x zoom out from original 64

    enum ParamId {
        MASTER_RATE_PARAM,
        BIPOLAR_PARAM,
        RATE_PARAM,
        RATE_PARAM_END = RATE_PARAM + NUM_LFOS,
        WAVE_PARAM,
        WAVE_PARAM_END = WAVE_PARAM + NUM_LFOS,
        SKEW_PARAM,
        SKEW_PARAM_END = SKEW_PARAM + NUM_LFOS,
        CURVE_PARAM,
        CURVE_PARAM_END = CURVE_PARAM + NUM_LFOS,
        FOLD_PARAM,
        FOLD_PARAM_END = FOLD_PARAM + NUM_LFOS,
        FM_PARAM,
        FM_PARAM_END = FM_PARAM + NUM_LFOS,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        FM_INPUT,
        FM_INPUT_END = FM_INPUT + NUM_LFOS,
        INPUTS_LEN
    };
    enum OutputId {
        LFO_OUTPUT,
        LFO_OUTPUT_END = LFO_OUTPUT + NUM_LFOS,
        OUTPUTS_LEN
    };
    enum LightId {
        CLOCK_LIGHT,
        LIGHTS_LEN
    };

    // Clock detection
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    float timeSinceClock = 0.f;
    float clockPeriod = 0.5f;
    bool clockDetected = false;

    // LFO state
    float phases[NUM_LFOS] = {};

    // Scope buffers
    float scopeBuffer[NUM_LFOS][SCOPE_BUFFER_SIZE] = {};
    int scopeWriteIndex[NUM_LFOS] = {};
    int scopeDownsample = 0;
    static constexpr int SCOPE_DOWNSAMPLE_RATE = 256;  // 4x zoom out from original 64

    // Must match OctoLFO.cpp
    static constexpr float FM_RANGE_OCTAVES = 4.f;
    static constexpr float MAX_PHASE_INC = 0.25f;

    OctoLFOTest() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configSwitch(MASTER_RATE_PARAM, 0.f, MASTER_RATE_VALUES.size() - 1, 8.f, "Master Rate", MASTER_RATE_LABELS);
        configSwitch(BIPOLAR_PARAM, 0.f, 1.f, 0.f, "Output Mode");

        for (int i = 0; i < NUM_LFOS; i++) {
            configSwitch(RATE_PARAM + i, 0.f, CHANNEL_RATE_VALUES.size() - 1, 8.f, "LFO Rate", CHANNEL_RATE_LABELS);
            configSwitch(WAVE_PARAM + i, 0.f, NUM_WAVE_TYPES - 1, WAVE_SINE, "Wave");
            configParam(SKEW_PARAM + i, -1.f, 1.f, 0.f, "Skew");
            configParam(CURVE_PARAM + i, -1.f, 1.f, 0.f, "Curve");
            configSwitch(FOLD_PARAM + i, 0.f, FOLD_VALUES.size() - 1, 0.f, "Fold");
            configParam(FM_PARAM + i, -1.f, 1.f, 0.f, "FM Amount");
            configInput(FM_INPUT + i, "FM");
            configOutput(LFO_OUTPUT + i, "LFO");
        }

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configLight(CLOCK_LIGHT, "Clock Lock");
    }

    float applySkew(float phase, float skew) {
        if (std::abs(skew) < 0.001f) return phase;
        float power = std::pow(2.f, -skew * 2.f);
        return std::pow(phase, power);
    }

    float applyCurve(float value, float curve) {
        if (std::abs(curve) < 0.001f) return value;
        float power = std::pow(3.f, curve);
        return std::pow(value, power);
    }

    float applyFold(float value, int foldIndex) {
        if (foldIndex <= 0 || foldIndex >= (int)FOLD_VALUES.size()) return value;
        float foldAmount = FOLD_VALUES[foldIndex];
        if (foldAmount < 0.001f) return value;
        float gain = 1.f + foldAmount * 4.f;
        float folded = value * gain;
        folded = std::sin(folded * M_PI);
        return folded;
    }

    float generateWave(float phase, WaveType wave) {
        switch (wave) {
            case WAVE_SINE:
                return std::sin(phase * 2.f * M_PI);
            case WAVE_TRIANGLE:
                return (phase < 0.5f) ? (phase * 4.f - 1.f) : (3.f - phase * 4.f);
            case WAVE_SAW_UP:
                return phase * 2.f - 1.f;
            case WAVE_SAW_DOWN:
                return 1.f - phase * 2.f;
            case WAVE_SQUARE:
                return (phase < 0.5f) ? -1.f : 1.f;
            default:
                return 0.f;
        }
    }

    void process(float sampleTime) {
        float dt = sampleTime;

        timeSinceClock += dt;

        if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
            if (timeSinceClock > 0.001f) {
                clockPeriod = timeSinceClock;
                clockDetected = true;
            }
            timeSinceClock = 0.f;
        }

        if (timeSinceClock > 3.f) {
            clockDetected = false;
        }

        lights[CLOCK_LIGHT].setBrightness(clockDetected ? 1.f : 0.2f);

        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
            for (int i = 0; i < NUM_LFOS; i++) {
                phases[i] = 0.f;
            }
        }

        int masterRateIndex = static_cast<int>(params[MASTER_RATE_PARAM].getValue());
        masterRateIndex = clamp(masterRateIndex, 0, (int)MASTER_RATE_VALUES.size() - 1);
        float masterRate = MASTER_RATE_VALUES[masterRateIndex];

        bool unipolar = params[BIPOLAR_PARAM].getValue() > 0.5f;

        scopeDownsample++;
        bool updateScope = (scopeDownsample >= SCOPE_DOWNSAMPLE_RATE);
        if (updateScope) scopeDownsample = 0;

        for (int i = 0; i < NUM_LFOS; i++) {
            int lfoRateIndex = static_cast<int>(params[RATE_PARAM + i].getValue());
            lfoRateIndex = clamp(lfoRateIndex, 0, (int)CHANNEL_RATE_VALUES.size() - 1);
            float lfoRate = CHANNEL_RATE_VALUES[lfoRateIndex];
            float combinedRate = masterRate * lfoRate;
            float freq = combinedRate / clockPeriod;

            float fmAmount = params[FM_PARAM + i].getValue();
            if (inputs[FM_INPUT + i].isConnected() && std::abs(fmAmount) > 0.001f) {
                float fmCV = inputs[FM_INPUT + i].getVoltage();
                freq *= std::pow(2.f, (fmCV / 5.f) * fmAmount * FM_RANGE_OCTAVES);
            }

            phases[i] += clamp(freq * dt, -MAX_PHASE_INC, MAX_PHASE_INC);
            while (phases[i] >= 1.f) phases[i] -= 1.f;
            while (phases[i] < 0.f) phases[i] += 1.f;

            WaveType wave = static_cast<WaveType>(
                static_cast<int>(params[WAVE_PARAM + i].getValue()));
            float skew = params[SKEW_PARAM + i].getValue();
            float curve = params[CURVE_PARAM + i].getValue();
            int foldIndex = static_cast<int>(params[FOLD_PARAM + i].getValue());

            float skewedPhase = applySkew(phases[i], skew);
            float output = generateWave(skewedPhase, wave);

            float normalized = (output + 1.f) * 0.5f;
            normalized = applyCurve(normalized, curve);
            output = normalized * 2.f - 1.f;

            output = applyFold(output, foldIndex);
            output = clamp(output, -1.f, 1.f);

            float voltage;
            if (unipolar) {
                voltage = (output + 1.f) * 5.f;
            } else {
                voltage = output * 5.f;
            }
            outputs[LFO_OUTPUT + i].setVoltage(voltage);

            if (updateScope) {
                scopeBuffer[i][scopeWriteIndex[i]] = output;
                scopeWriteIndex[i] = (scopeWriteIndex[i] + 1) % SCOPE_BUFFER_SIZE;
            }
        }
    }
};

} // namespace WiggleRoom

// Helper functions for JSON output
void printJsonArray(const std::string& name, const std::vector<float>& values) {
    std::cout << "{\"" << name << "\": [";
    for (size_t i = 0; i < values.size(); i++) {
        std::cout << values[i];
        if (i < values.size() - 1) std::cout << ", ";
    }
    std::cout << "]}" << std::endl;
}

void printJsonValue(const std::string& name, float value) {
    std::cout << "{\"" << name << "\": " << value << "}" << std::endl;
}

void printJsonInt(const std::string& name, int value) {
    std::cout << "{\"" << name << "\": " << value << "}" << std::endl;
}

void printJsonObject(const std::vector<std::pair<std::string, float>>& kvs) {
    std::cout << "{";
    for (size_t i = 0; i < kvs.size(); i++) {
        std::cout << "\"" << kvs[i].first << "\": " << kvs[i].second;
        if (i < kvs.size() - 1) std::cout << ", ";
    }
    std::cout << "}" << std::endl;
}

int parseIntArg(int argc, char** argv, const char* prefix, int defaultVal) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], prefix, strlen(prefix)) == 0) {
            return std::stoi(argv[i] + strlen(prefix));
        }
    }
    return defaultVal;
}

float parseFloatArg(int argc, char** argv, const char* prefix, float defaultVal) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], prefix, strlen(prefix)) == 0) {
            return std::stof(argv[i] + strlen(prefix));
        }
    }
    return defaultVal;
}

// Run one LFO and measure its actual output frequency by counting rising
// zero crossings. Returns Hz.
float measureFrequency(float fmCV, float fmAtten, bool fmConnected,
                       float clockPeriod, float seconds, float sampleRate) {
    using M = WiggleRoom::OctoLFOTest;
    M module;
    module.clockPeriod = clockPeriod;
    module.params[M::MASTER_RATE_PARAM].setValue(11.f);  // x1
    module.params[M::RATE_PARAM + 0].setValue(11.f);     // x1
    module.params[M::WAVE_PARAM + 0].setValue(0.f);      // sine
    module.params[M::FM_PARAM + 0].setValue(fmAtten);
    module.inputs[M::FM_INPUT + 0].setConnected(fmConnected);
    module.inputs[M::FM_INPUT + 0].setVoltage(fmCV);

    const float dt = 1.f / sampleRate;
    const int numSamples = static_cast<int>(seconds * sampleRate);
    int crossings = 0;
    float prev = 0.f;
    for (int s = 0; s < numSamples; s++) {
        module.process(dt);
        float v = module.outputs[M::LFO_OUTPUT + 0].getVoltage();
        if (s > 0 && prev < 0.f && v >= 0.f) crossings++;
        prev = v;
    }
    return crossings / seconds;
}

void printUsage() {
    std::cerr << "OctoLFO Test Executable\n\n"
              << "Commands:\n"
              << "  --list-master-rate-values    Print master rate values array\n"
              << "  --list-channel-rate-values   Print channel rate values array\n"
              << "  --test-scope-buffer          Test scope buffer properties\n"
              << "  --test-combined-rate         Test combined rate calculation\n"
              << "      --master-index=N         Master rate index (default: 8)\n"
              << "      --channel-index=N        Channel rate index (default: 8)\n"
              << "  --test-waveform              Test waveform generation\n"
              << "      --wave=N                 Wave type (0-4, default: 0)\n"
              << "      --phase=F                Phase (0-1, default: 0.25)\n"
              << "  --test-fm                    Test exponential FM tracking\n"
              << "      --cv=F                   FM CV volts (default: 5.0)\n"
              << "      --atten=F                FM attenuverter -1..1 (default: 1.0)\n"
              << "      --seconds=F              Measurement window (default: 8.0)\n"
              << "  --test-fm-bypass             Verify zero atten / unpatched leaves rate untouched\n"
              << "      --seconds=F              Measurement window (default: 8.0)\n"
              << "  --help                       Print this help\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }

    if (cmd == "--list-master-rate-values") {
        printJsonArray("master_rate_values", WiggleRoom::MASTER_RATE_VALUES);
        return 0;
    }

    if (cmd == "--list-channel-rate-values") {
        printJsonArray("channel_rate_values", WiggleRoom::CHANNEL_RATE_VALUES);
        return 0;
    }

    if (cmd == "--test-scope-buffer") {
        std::vector<std::pair<std::string, float>> result;
        result.push_back({"buffer_size", WiggleRoom::OctoLFOTest::SCOPE_BUFFER_SIZE});
        result.push_back({"downsample_rate", WiggleRoom::OctoLFOTest::SCOPE_DOWNSAMPLE_RATE});
        printJsonObject(result);
        return 0;
    }

    if (cmd == "--test-combined-rate") {
        int masterIndex = parseIntArg(argc, argv, "--master-index=", 8);
        int channelIndex = parseIntArg(argc, argv, "--channel-index=", 8);

        // Clamp indices
        masterIndex = std::max(0, std::min(masterIndex, (int)WiggleRoom::MASTER_RATE_VALUES.size() - 1));
        channelIndex = std::max(0, std::min(channelIndex, (int)WiggleRoom::CHANNEL_RATE_VALUES.size() - 1));

        float masterRate = WiggleRoom::MASTER_RATE_VALUES[masterIndex];
        float channelRate = WiggleRoom::CHANNEL_RATE_VALUES[channelIndex];
        float combinedRate = masterRate * channelRate;

        std::vector<std::pair<std::string, float>> result;
        result.push_back({"master_index", (float)masterIndex});
        result.push_back({"channel_index", (float)channelIndex});
        result.push_back({"master_rate", masterRate});
        result.push_back({"channel_rate", channelRate});
        result.push_back({"combined_rate", combinedRate});
        printJsonObject(result);
        return 0;
    }

    if (cmd == "--test-waveform") {
        int wave = parseIntArg(argc, argv, "--wave=", 0);
        float phase = 0.25f;

        // Parse phase arg
        for (int i = 1; i < argc; i++) {
            if (strncmp(argv[i], "--phase=", 8) == 0) {
                phase = std::stof(argv[i] + 8);
            }
        }

        WiggleRoom::OctoLFOTest module;
        float output = module.generateWave(phase, static_cast<WiggleRoom::WaveType>(wave));

        std::vector<std::pair<std::string, float>> result;
        result.push_back({"wave", (float)wave});
        result.push_back({"phase", phase});
        result.push_back({"output", output});
        printJsonObject(result);
        return 0;
    }

    if (cmd == "--test-fm") {
        float cv = parseFloatArg(argc, argv, "--cv=", 5.f);
        float atten = parseFloatArg(argc, argv, "--atten=", 1.f);
        float seconds = parseFloatArg(argc, argv, "--seconds=", 8.f);
        const float sampleRate = 44100.f;
        const float clockPeriod = 0.5f;   // 2 Hz base at x1 / x1

        float base = 1.f / clockPeriod;
        float expected = base * std::pow(2.f, (cv / 5.f) * atten * 4.f);
        float measured = measureFrequency(cv, atten, true, clockPeriod, seconds, sampleRate);

        // Zero-crossing counting quantises to 1/seconds Hz, so allow that plus 2%
        float tolerance = std::max(expected * 0.02f, 1.f / seconds);
        bool pass = std::abs(measured - expected) <= tolerance;

        std::vector<std::pair<std::string, float>> result;
        result.push_back({"cv", cv});
        result.push_back({"atten", atten});
        result.push_back({"base_hz", base});
        result.push_back({"expected_hz", expected});
        result.push_back({"measured_hz", measured});
        result.push_back({"tolerance_hz", tolerance});
        result.push_back({"PASS", pass ? 1.f : 0.f});
        printJsonObject(result);
        return pass ? 0 : 1;
    }

    if (cmd == "--test-fm-bypass") {
        // Regression guard: with the attenuverter at zero, or nothing patched,
        // the rate must be identical to the unmodulated clock-synced rate.
        float seconds = parseFloatArg(argc, argv, "--seconds=", 8.f);
        const float sampleRate = 44100.f;
        const float clockPeriod = 0.5f;
        float base = 1.f / clockPeriod;

        float attenZero = measureFrequency(5.f, 0.f, true, clockPeriod, seconds, sampleRate);
        float unpatched = measureFrequency(5.f, 1.f, false, clockPeriod, seconds, sampleRate);
        float tolerance = 1.f / seconds;
        bool pass = std::abs(attenZero - base) <= tolerance &&
                    std::abs(unpatched - base) <= tolerance;

        std::vector<std::pair<std::string, float>> result;
        result.push_back({"base_hz", base});
        result.push_back({"atten_zero_hz", attenZero});
        result.push_back({"unpatched_hz", unpatched});
        result.push_back({"PASS", pass ? 1.f : 0.f});
        printJsonObject(result);
        return pass ? 0 : 1;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    printUsage();
    return 1;
}
