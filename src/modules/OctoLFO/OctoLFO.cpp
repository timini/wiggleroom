/******************************************************************************
 * OCTO LFO
 * Clock-synced 8-channel LFO with advanced wave shaping
 *
 * Features:
 *   - 8 independent LFOs locked to external clock
 *   - Per-LFO rate multiplier/divider
 *   - Master rate multiplier/divider
 *   - Wave types: Sine, Triangle, Saw Up, Saw Down, Square
 *   - Per-LFO modifiers:
 *     - Skew: Time warping (shift peak position)
 *     - Curve: Slope warping (log/exp response)
 *     - Fold: Harmonic warping (wave folding)
 *   - Mini scope display per LFO
 ******************************************************************************/

#include "rack.hpp"
#include "ImagePanel.hpp"
#include <cmath>
#include <vector>
#include <string>

using namespace rack;

extern Plugin* pluginInstance;

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

// Rate multiplier values - musical divisions and multiplications
static const std::vector<float> RATE_VALUES = {
    1.f/16.f,      // /16
    1.f/12.f,      // /12
    1.f/8.f,       // /8
    1.f/6.f,       // /6
    1.f/4.f,       // /4
    1.f/3.f,       // /3
    1.f/2.f,       // /2
    2.f/3.f,       // /1.5 (2/3)
    1.f,           // x1
    3.f/2.f,       // x1.5
    2.f,           // x2
    3.f,           // x3
    4.f,           // x4
    6.f,           // x6
    8.f,           // x8
    12.f,          // x12
    16.f           // x16
};

static const std::vector<std::string> RATE_LABELS = {
    "/16", "/12", "/8", "/6", "/4", "/3", "/2", "/1.5",
    "x1", "x1.5", "x2", "x3", "x4", "x6", "x8", "x12", "x16"
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

static const std::vector<std::string> FOLD_LABELS = {
    "0", "1/16", "1/8", "1/6", "1/4", "1/3", "1/2", "2/3",
    "3/4", "1", "5/4", "4/3", "3/2", "2", "3", "4"
};

struct OctoLFO : Module {
    static constexpr int NUM_LFOS = 8;
    static constexpr int SCOPE_BUFFER_SIZE = 64;

    enum ParamId {
        MASTER_RATE_PARAM,  // Master mult/div
        BIPOLAR_PARAM,      // Bipolar (0) / Unipolar (1) switch
        ENUMS(RATE_PARAM, NUM_LFOS),      // Per-LFO mult/div
        ENUMS(WAVE_PARAM, NUM_LFOS),      // Wave type selector
        ENUMS(SKEW_PARAM, NUM_LFOS),      // Time warping
        ENUMS(CURVE_PARAM, NUM_LFOS),     // Slope warping
        ENUMS(FOLD_PARAM, NUM_LFOS),      // Harmonic warping
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        ENUMS(LFO_OUTPUT, NUM_LFOS),
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
    float clockPeriod = 0.5f;  // Default 120 BPM
    bool clockDetected = false;

    // LFO state
    float phases[NUM_LFOS] = {};

    // Scope buffers (circular buffer per LFO)
    float scopeBuffer[NUM_LFOS][SCOPE_BUFFER_SIZE] = {};
    int scopeWriteIndex[NUM_LFOS] = {};
    int scopeDownsample = 0;
    static constexpr int SCOPE_DOWNSAMPLE_RATE = 64;

    OctoLFO() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Master rate: snapped to musical divisions/multiplications
        // Default to index 8 which is "x1"
        configSwitch(MASTER_RATE_PARAM, 0.f, RATE_VALUES.size() - 1, 8.f,
            "Master Rate", RATE_LABELS);

        // Bipolar/Unipolar switch (0 = bipolar ±5V, 1 = unipolar 0-10V)
        configSwitch(BIPOLAR_PARAM, 0.f, 1.f, 0.f, "Output Mode", {"Bipolar (±5V)", "Unipolar (0-10V)"});

        for (int i = 0; i < NUM_LFOS; i++) {
            std::string label = "LFO " + std::to_string(i + 1);

            // Per-LFO rate: snapped to musical divisions/multiplications
            // Default to index 8 which is "x1"
            configSwitch(RATE_PARAM + i, 0.f, RATE_VALUES.size() - 1, 8.f,
                label + " Rate", RATE_LABELS);

            // Wave type (0-4)
            configSwitch(WAVE_PARAM + i, 0.f, NUM_WAVE_TYPES - 1, WAVE_SINE,
                label + " Wave", {"Sine", "Triangle", "Saw Up", "Saw Down", "Square"});

            // Skew: -1 (early peak) to +1 (late peak), 0 = centered
            configParam(SKEW_PARAM + i, -1.f, 1.f, 0.f, label + " Skew");

            // Curve: -1 (log) to +1 (exp), 0 = linear
            configParam(CURVE_PARAM + i, -1.f, 1.f, 0.f, label + " Curve");

            // Fold: snapped to musical fractions
            configSwitch(FOLD_PARAM + i, 0.f, FOLD_VALUES.size() - 1, 0.f,
                label + " Fold", FOLD_LABELS);

            configOutput(LFO_OUTPUT + i, label);
        }

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configLight(CLOCK_LIGHT, "Clock Lock");
    }

    // Apply skew (time warping) to phase
    float applySkew(float phase, float skew) {
        if (std::abs(skew) < 0.001f) return phase;
        float power = std::pow(2.f, -skew * 2.f);
        return std::pow(phase, power);
    }

    // Apply curve (slope warping) to amplitude
    float applyCurve(float value, float curve) {
        if (std::abs(curve) < 0.001f) return value;
        float power = std::pow(3.f, curve);
        return std::pow(value, power);
    }

    // Apply fold (harmonic warping) - foldIndex is index into FOLD_VALUES
    float applyFold(float value, int foldIndex) {
        if (foldIndex <= 0 || foldIndex >= (int)FOLD_VALUES.size()) return value;
        float foldAmount = FOLD_VALUES[foldIndex];
        if (foldAmount < 0.001f) return value;
        // Fold amount scales the gain for wave folding
        float gain = 1.f + foldAmount * 4.f;
        float folded = value * gain;
        folded = std::sin(folded * M_PI);
        return folded;
    }

    // Generate base waveform from phase [0, 1)
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

    void process(const ProcessArgs& args) override {
        float dt = args.sampleTime;

        // Clock detection
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

        // Reset handling
        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
            for (int i = 0; i < NUM_LFOS; i++) {
                phases[i] = 0.f;
            }
        }

        // Master rate multiplier
        int masterRateIndex = static_cast<int>(params[MASTER_RATE_PARAM].getValue());
        masterRateIndex = clamp(masterRateIndex, 0, (int)RATE_VALUES.size() - 1);
        float masterRate = RATE_VALUES[masterRateIndex];

        // Output mode: 0 = bipolar (±5V), 1 = unipolar (0-10V)
        bool unipolar = params[BIPOLAR_PARAM].getValue() > 0.5f;

        // Update scope downsample counter
        scopeDownsample++;
        bool updateScope = (scopeDownsample >= SCOPE_DOWNSAMPLE_RATE);
        if (updateScope) scopeDownsample = 0;

        // Process each LFO
        for (int i = 0; i < NUM_LFOS; i++) {
            int lfoRateIndex = static_cast<int>(params[RATE_PARAM + i].getValue());
            lfoRateIndex = clamp(lfoRateIndex, 0, (int)RATE_VALUES.size() - 1);
            float lfoRate = RATE_VALUES[lfoRateIndex];
            float combinedRate = masterRate * lfoRate;
            float freq = combinedRate / clockPeriod;

            phases[i] += freq * dt;
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

            // Apply output mode (after folding to preserve wave shape)
            float voltage;
            if (unipolar) {
                // Unipolar: 0 to 10V
                voltage = (output + 1.f) * 5.f;
            } else {
                // Bipolar: -5V to +5V
                voltage = output * 5.f;
            }
            outputs[LFO_OUTPUT + i].setVoltage(voltage);

            // Update scope buffer (downsampled)
            if (updateScope) {
                scopeBuffer[i][scopeWriteIndex[i]] = output;
                scopeWriteIndex[i] = (scopeWriteIndex[i] + 1) % SCOPE_BUFFER_SIZE;
            }
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "clockPeriod", json_real(clockPeriod));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* periodJ = json_object_get(rootJ, "clockPeriod");
        if (periodJ) {
            clockPeriod = json_real_value(periodJ);
        }
    }
};

// Mini scope widget for displaying LFO waveform
struct MiniScopeWidget : Widget {
    OctoLFO* module;
    int lfoIndex;
    NVGcolor waveColor;

    MiniScopeWidget() {
        box.size = Vec(30, 20);
        waveColor = nvgRGB(0x00, 0xff, 0x80);
    }

    void draw(const DrawArgs& args) override {
        // Background
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(0x10, 0x10, 0x10));
        nvgFill(args.vg);

        // Border
        nvgStrokeColor(args.vg, nvgRGB(0x40, 0x40, 0x40));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        // Center line
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, 0, box.size.y / 2);
        nvgLineTo(args.vg, box.size.x, box.size.y / 2);
        nvgStrokeColor(args.vg, nvgRGB(0x30, 0x30, 0x30));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        if (!module) return;

        // Draw waveform
        nvgBeginPath(args.vg);

        int bufferSize = OctoLFO::SCOPE_BUFFER_SIZE;
        int startIndex = module->scopeWriteIndex[lfoIndex];

        for (int i = 0; i < bufferSize; i++) {
            int idx = (startIndex + i) % bufferSize;
            float sample = module->scopeBuffer[lfoIndex][idx];

            float x = (float)i / (bufferSize - 1) * box.size.x;
            float y = (1.f - sample) * 0.5f * box.size.y;
            y = clamp(y, 1.f, box.size.y - 1.f);

            if (i == 0) {
                nvgMoveTo(args.vg, x, y);
            } else {
                nvgLineTo(args.vg, x, y);
            }
        }

        nvgStrokeColor(args.vg, waveColor);
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        Widget::drawLayer(args, layer);
    }
};

struct OctoLFOWidget : ModuleWidget {
    OctoLFOWidget(OctoLFO* module) {
        setModule(module);
        box.size = Vec(7 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/OctoLFO.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Use mm2px for consistent positioning (VCV uses mm internally)
        float panelWidth = box.size.x;  // 101.6mm for 20HP

        // Top section: Clock input, Reset, Master rate, Bipolar switch
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8, 12)), module, OctoLFO::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20, 12)), module, OctoLFO::RESET_INPUT));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(35, 12)), module, OctoLFO::MASTER_RATE_PARAM));
        addParam(createParamCentered<CKSS>(mm2px(Vec(50, 12)), module, OctoLFO::BIPOLAR_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(58, 8)), module, OctoLFO::CLOCK_LIGHT));

        // LFO rows - 8 rows from y=22mm to y=118mm (96mm / 8 = 12mm per row)
        float startY = 24.f;  // mm
        float rowHeight = 12.f;  // mm

        // Column positions in mm
        float colRate = 8.f;
        float colWave = 18.f;
        float colSkew = 28.f;
        float colCurve = 38.f;
        float colFold = 48.f;
        float colScope = 62.f;  // Center of scope
        float colOut = 92.f;

        for (int i = 0; i < OctoLFO::NUM_LFOS; i++) {
            float y = startY + i * rowHeight;

            // Rate knob
            addParam(createParamCentered<Trimpot>(mm2px(Vec(colRate, y)), module, OctoLFO::RATE_PARAM + i));

            // Wave type
            addParam(createParamCentered<Trimpot>(mm2px(Vec(colWave, y)), module, OctoLFO::WAVE_PARAM + i));

            // Skew
            addParam(createParamCentered<Trimpot>(mm2px(Vec(colSkew, y)), module, OctoLFO::SKEW_PARAM + i));

            // Curve
            addParam(createParamCentered<Trimpot>(mm2px(Vec(colCurve, y)), module, OctoLFO::CURVE_PARAM + i));

            // Fold
            addParam(createParamCentered<Trimpot>(mm2px(Vec(colFold, y)), module, OctoLFO::FOLD_PARAM + i));

            // Mini scope (size in pixels)
            MiniScopeWidget* scope = new MiniScopeWidget();
            scope->module = module;
            scope->lfoIndex = i;
            scope->box.size = Vec(mm2px(18), mm2px(8));
            scope->box.pos = mm2px(Vec(colScope - 9, y - 4));
            addChild(scope);

            // Output jack
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colOut, y)), module, OctoLFO::LFO_OUTPUT + i));
        }
    }
};

} // namespace WiggleRoom

Model* modelOctoLFO = createModel<WiggleRoom::OctoLFO, WiggleRoom::OctoLFOWidget>("OctoLFO");
