/******************************************************************************
 * EUCLOGIC 2
 * 2-channel Euclidean drum sequencer with truth table logic (compact version)
 *
 * Signal flow:
 *   Clock → Master Speed → 2x Euclidean Engines → Prob A → Truth Table → Prob B → Outputs
 *
 * Features:
 *   - 2 independent Euclidean rhythm generators
 *   - Per-channel: Steps (1-64), Hits (0-Steps), Quant ratio
 *   - Probability A: Pre-logic probability gate (×2)
 *   - Truth Table: 4-state logic engine with LED matrix display
 *   - Probability B: Post-logic probability gate (×2)
 *   - Random/Mutate/Undo buttons for truth table
 *   - 2x Gate + 2x Trigger + 2x LFO outputs
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include "EuclideanEngine.hpp"
#include "ProbabilityGate.hpp"
#include <atomic>
#include <array>
#include <random>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

// Speed ratio values (/16 to x16)
static const std::vector<float> SPEED_RATIOS_2 = {
    1.f/16, 1.f/12, 1.f/8, 1.f/6, 1.f/4, 1.f/3, 1.f/2, 2.f/3,
    1.f,  // x1 (index 8)
    3.f/2, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f
};

static const std::vector<std::string> SPEED_LABELS_2 = {
    "/16", "/12", "/8", "/6", "/4", "/3", "/2", "/1.5",
    "x1",
    "x1.5", "x2", "x3", "x4", "x6", "x8", "x12", "x16"
};

// Quant ratio values (per-channel clock division)
static const std::vector<float> QUANT_RATIOS_2 = {
    1.f, 1.f/2, 1.f/4, 1.f/8, 1.f/16
};

static const std::vector<std::string> QUANT_LABELS_2 = {
    "x1", "/2", "/4", "/8", "/16"
};

// Forward declaration
struct Euclogic2;

// Generate LFO waveform from phase [0, 1) - returns [-1, 1]
// Fixed waveform per channel: Ch1=Sine, Ch2=Triangle
static float generateLFOWave2(int channel, float phase) {
    switch (channel) {
        case 0:  // Ch1: Sine
            return std::sin(phase * 2.f * M_PI);
        case 1:  // Ch2: Triangle
            return (phase < 0.5f) ? (phase * 4.f - 1.f) : (3.f - phase * 4.f);
        default:
            return 0.f;
    }
}

// Simple 2-input truth table (4 states)
struct TruthTable2 {
    std::array<uint8_t, 4> mapping = {0, 1, 2, 3};  // Default: pass-through
    std::array<uint8_t, 4> undoMapping = {0, 1, 2, 3};
    std::mt19937 rng{std::random_device{}()};

    void randomize() {
        pushUndo();
        std::uniform_int_distribution<int> dist(0, 3);
        for (int i = 0; i < 4; i++) {
            mapping[i] = dist(rng);
        }
    }

    void mutate() {
        pushUndo();
        std::uniform_int_distribution<int> stateDist(0, 3);
        std::uniform_int_distribution<int> bitDist(0, 1);
        int state = stateDist(rng);
        int bit = bitDist(rng);
        mapping[state] ^= (1 << bit);
    }

    void pushUndo() {
        undoMapping = mapping;
    }

    void undo() {
        std::swap(mapping, undoMapping);
    }

    void toggleBit(int state, int bit) {
        if (state >= 0 && state < 4 && bit >= 0 && bit < 2) {
            mapping[state] ^= (1 << bit);
        }
    }

    uint8_t getMapping(int state) const {
        return (state >= 0 && state < 4) ? mapping[state] : 0;
    }

    void evaluate(const bool* inputs, bool* outputs) const {
        uint8_t inputState = (inputs[0] ? 1 : 0) | (inputs[1] ? 2 : 0);
        uint8_t outputMask = mapping[inputState];
        outputs[0] = (outputMask >> 0) & 1;
        outputs[1] = (outputMask >> 1) & 1;
    }

    std::array<uint8_t, 4> serialize() const { return mapping; }
    void deserialize(const std::array<uint8_t, 4>& m) { mapping = m; }
};

// Custom ParamQuantity that limits hits to current steps value
struct Hits2ParamQuantity : ParamQuantity {
    int channel = 0;
    float getMaxValue() override;
    void setValue(float value) override;
};

struct Euclogic2 : Module {
    static constexpr int NUM_CHANNELS = 2;
    static constexpr float TRIGGER_PULSE_DURATION = 1e-3f;  // 1ms
    static constexpr float RETRIG_GAP_DURATION = 0.5e-3f;   // 0.5ms gap for retrigger
    static constexpr float SCHMITT_LOW = 0.1f;
    static constexpr float SCHMITT_HIGH = 1.0f;
    static constexpr float DEFAULT_CLOCK_PERIOD = 0.5f;  // 120 BPM

    enum ParamId {
        MASTER_SPEED_PARAM,
        SWING_PARAM,
        RANDOM_PARAM,
        MUTATE_PARAM,
        UNDO_PARAM,
        ENUMS(STEPS_PARAM, NUM_CHANNELS),
        ENUMS(HITS_PARAM, NUM_CHANNELS),
        ENUMS(QUANT_PARAM, NUM_CHANNELS),
        ENUMS(PROB_A_PARAM, NUM_CHANNELS),
        ENUMS(PROB_B_PARAM, NUM_CHANNELS),
        ENUMS(RETRIG_PARAM, NUM_CHANNELS),
        PARAMS_LEN
    };

    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        RUN_INPUT,
        ENUMS(HITS_CV_INPUT, NUM_CHANNELS),
        ENUMS(PROB_A_CV_INPUT, NUM_CHANNELS),
        ENUMS(PROB_B_CV_INPUT, NUM_CHANNELS),
        INPUTS_LEN
    };

    enum OutputId {
        ENUMS(GATE_OUTPUT, NUM_CHANNELS),
        ENUMS(TRIG_OUTPUT, NUM_CHANNELS),
        ENUMS(LFO_OUTPUT, NUM_CHANNELS),
        OUTPUTS_LEN
    };

    enum LightId {
        CLOCK_LIGHT,
        RUN_LIGHT,
        ENUMS(GATE_LIGHT, NUM_CHANNELS),
        ENUMS(LED_MATRIX_LIGHT, 8),  // 4 input states × 2 output bits
        LIGHTS_LEN
    };

    // Core components
    EuclideanEngine engines[NUM_CHANNELS];
    ProbabilityGate probA[NUM_CHANNELS];
    ProbabilityGate probB[NUM_CHANNELS];
    TruthTable2 truthTable;

    // Clock state
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger runTrigger;
    dsp::SchmittTrigger randomTrigger;
    dsp::SchmittTrigger mutateTrigger;
    dsp::SchmittTrigger undoTrigger;

    float clockPeriod = DEFAULT_CLOCK_PERIOD;
    float timeSinceClock = 0.f;
    float internalTickPhase = 0.f;
    bool clockLocked = false;
    bool running = true;
    bool hadFirstTick = false;
    int prevSpeedIdx = 8;
    bool swingLong = true;

    int quantCounter[NUM_CHANNELS] = {0};
    dsp::PulseGenerator trigPulse[NUM_CHANNELS];
    bool prevGateHigh[NUM_CHANNELS] = {false};
    float retrigGapTimer[NUM_CHANNELS] = {0.f};

    std::atomic<uint8_t> currentInputState{0};
    std::atomic<bool> gateStates[NUM_CHANNELS];

    Euclogic2() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configSwitch(MASTER_SPEED_PARAM, 0.f, SPEED_RATIOS_2.size() - 1, 8.f,
            "Master Speed", SPEED_LABELS_2);
        configParam(SWING_PARAM, 50.f, 75.f, 50.f, "Swing", "%");

        configButton(RANDOM_PARAM, "Random");
        configButton(MUTATE_PARAM, "Mutate");
        configButton(UNDO_PARAM, "Undo");

        for (int i = 0; i < NUM_CHANNELS; i++) {
            std::string ch = "Ch " + std::to_string(i + 1);

            configParam(STEPS_PARAM + i, 1.f, 64.f, 16.f, ch + " Steps");
            paramQuantities[STEPS_PARAM + i]->snapEnabled = true;

            configParam(HITS_PARAM + i, 0.f, 64.f, 8.f, ch + " Hits");
            paramQuantities[HITS_PARAM + i]->snapEnabled = true;
            Hits2ParamQuantity* hitsQ = new Hits2ParamQuantity();
            hitsQ->channel = i;
            hitsQ->module = this;
            hitsQ->paramId = HITS_PARAM + i;
            hitsQ->minValue = 0.f;
            hitsQ->maxValue = 64.f;
            hitsQ->defaultValue = 8.f;
            hitsQ->name = ch + " Hits";
            hitsQ->snapEnabled = true;
            delete paramQuantities[HITS_PARAM + i];
            paramQuantities[HITS_PARAM + i] = hitsQ;

            configSwitch(QUANT_PARAM + i, 0.f, QUANT_RATIOS_2.size() - 1, 0.f,
                ch + " Quant", QUANT_LABELS_2);

            configParam(PROB_A_PARAM + i, 0.f, 1.f, 1.f, ch + " Prob A", "%", 0.f, 100.f);
            configParam(PROB_B_PARAM + i, 0.f, 1.f, 1.f, ch + " Prob B", "%", 0.f, 100.f);
            configSwitch(RETRIG_PARAM + i, 0.f, 1.f, 1.f, ch + " Retrigger", {"Off", "On"});

            configInput(HITS_CV_INPUT + i, ch + " Hits CV");
            configInput(PROB_A_CV_INPUT + i, ch + " Prob A CV");
            configInput(PROB_B_CV_INPUT + i, ch + " Prob B CV");

            configOutput(GATE_OUTPUT + i, ch + " Gate");
            configOutput(TRIG_OUTPUT + i, ch + " Trigger");
            configOutput(LFO_OUTPUT + i, ch + " LFO");
        }

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(RUN_INPUT, "Run");

        configLight(CLOCK_LIGHT, "Clock Lock");
        configLight(RUN_LIGHT, "Running");

        // Initialize engines with default values
        for (int i = 0; i < NUM_CHANNELS; i++) {
            engines[i].configure(16, 8, 0);  // 16 steps, 8 hits
            gateStates[i].store(false);
        }
    }

    void onReset() override {
        for (int i = 0; i < NUM_CHANNELS; i++) {
            engines[i].reset();
            quantCounter[i] = 0;
            gateStates[i].store(false);
            prevGateHigh[i] = false;
            retrigGapTimer[i] = 0.f;
        }
        timeSinceClock = 0.f;
        internalTickPhase = 0.f;
        clockPeriod = DEFAULT_CLOCK_PERIOD;
        running = true;
        hadFirstTick = false;
        prevSpeedIdx = 8;
        swingLong = true;
        currentInputState.store(0);
    }

    void process(const ProcessArgs& args) override {
        float dt = args.sampleTime;

        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), SCHMITT_LOW, SCHMITT_HIGH)) {
            onReset();
        }

        if (inputs[RUN_INPUT].isConnected()) {
            running = inputs[RUN_INPUT].getVoltage() > SCHMITT_HIGH;
        } else {
            running = true;
        }
        lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.2f);

        if (randomTrigger.process(params[RANDOM_PARAM].getValue())) {
            truthTable.randomize();
        }
        if (mutateTrigger.process(params[MUTATE_PARAM].getValue())) {
            truthTable.mutate();
        }
        if (undoTrigger.process(params[UNDO_PARAM].getValue())) {
            truthTable.undo();
        }

        timeSinceClock += dt;

        bool clockEdge = false;
        if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), SCHMITT_LOW, SCHMITT_HIGH)) {
            if (timeSinceClock > 0.001f) {
                clockPeriod = timeSinceClock;
                clockLocked = true;
            }
            timeSinceClock = 0.f;
            clockEdge = true;
        }

        if (timeSinceClock > 2.f) {
            clockLocked = false;
            hadFirstTick = false;
        }
        lights[CLOCK_LIGHT].setBrightness(clockLocked ? 1.f : 0.2f);

        int speedIdx = static_cast<int>(params[MASTER_SPEED_PARAM].getValue());
        speedIdx = DSP::clamp(speedIdx, 0, (int)SPEED_RATIOS_2.size() - 1);
        float masterSpeed = SPEED_RATIOS_2[speedIdx];

        if (speedIdx != prevSpeedIdx) {
            hadFirstTick = false;
            internalTickPhase = 0.f;
            prevSpeedIdx = speedIdx;
        }

        float swingPercent = params[SWING_PARAM].getValue();
        float swingRatio = swingPercent / 100.f;

        bool shouldTick = false;
        if (running && clockLocked) {
            float baseInterval = clockPeriod / masterSpeed;
            float longInterval = 2.f * baseInterval * swingRatio;
            float shortInterval = 2.f * baseInterval * (1.f - swingRatio);
            float currentInterval = swingLong ? longInterval : shortInterval;

            if (clockEdge) {
                shouldTick = true;
                hadFirstTick = true;
                swingLong = true;
                internalTickPhase = 0.f;
            } else if (hadFirstTick) {
                internalTickPhase += dt;
                if (internalTickPhase >= currentInterval) {
                    internalTickPhase -= currentInterval;
                    shouldTick = true;
                    swingLong = !swingLong;
                }
            }
        }

        if (shouldTick) {
            bool preLogicStates[NUM_CHANNELS] = {false};

            for (int i = 0; i < NUM_CHANNELS; i++) {
                int quantIdx = static_cast<int>(params[QUANT_PARAM + i].getValue());
                quantIdx = DSP::clamp(quantIdx, 0, (int)QUANT_RATIOS_2.size() - 1);
                float quantRatio = QUANT_RATIOS_2[quantIdx];

                int divAmount = static_cast<int>(1.f / quantRatio);
                if (divAmount < 1) divAmount = 1;

                quantCounter[i]++;
                if (quantCounter[i] >= divAmount) {
                    quantCounter[i] = 0;

                    int steps = static_cast<int>(params[STEPS_PARAM + i].getValue());
                    float hitsBase = params[HITS_PARAM + i].getValue();
                    float hitsCV = inputs[HITS_CV_INPUT + i].getVoltage() / 5.f * 12.f;
                    int hits = static_cast<int>(hitsBase + hitsCV);
                    hits = DSP::clamp(hits, 0, steps);

                    engines[i].configure(steps, hits, 0);
                    bool euclideanHit = engines[i].tick();

                    float probABase = params[PROB_A_PARAM + i].getValue();
                    float probACV = inputs[PROB_A_CV_INPUT + i].getVoltage() / 10.f;
                    float probAVal = DSP::clamp(probABase + probACV, 0.f, 1.f);

                    preLogicStates[i] = euclideanHit && probA[i].process(true, probAVal);
                } else {
                    preLogicStates[i] = false;
                }
            }

            uint8_t inputState = (preLogicStates[0] ? 1 : 0) | (preLogicStates[1] ? 2 : 0);
            currentInputState.store(inputState);

            bool postLogicStates[NUM_CHANNELS];
            truthTable.evaluate(preLogicStates, postLogicStates);

            for (int i = 0; i < NUM_CHANNELS; i++) {
                float probBBase = params[PROB_B_PARAM + i].getValue();
                float probBCV = inputs[PROB_B_CV_INPUT + i].getVoltage() / 10.f;
                float probBVal = DSP::clamp(probBBase + probBCV, 0.f, 1.f);

                bool finalOutput = postLogicStates[i] && probB[i].process(true, probBVal);
                bool retrigger = params[RETRIG_PARAM + i].getValue() > 0.5f;

                bool shouldTrigger = finalOutput && (!prevGateHigh[i] || retrigger);
                if (shouldTrigger) {
                    trigPulse[i].trigger(TRIGGER_PULSE_DURATION);
                }

                // Start retrigger gap if we have consecutive hits with retrigger on
                if (finalOutput && prevGateHigh[i] && retrigger) {
                    retrigGapTimer[i] = RETRIG_GAP_DURATION;
                }

                gateStates[i].store(finalOutput);
                prevGateHigh[i] = finalOutput;
            }
        }

        // Output voltages
        for (int i = 0; i < NUM_CHANNELS; i++) {
            bool gate = gateStates[i].load();

            // Handle retrigger gap - force gate low briefly for envelope retrigger
            if (retrigGapTimer[i] > 0.f) {
                retrigGapTimer[i] -= dt;
                outputs[GATE_OUTPUT + i].setVoltage(0.f);
            } else {
                outputs[GATE_OUTPUT + i].setVoltage(gate ? 10.f : 0.f);
            }

            outputs[TRIG_OUTPUT + i].setVoltage(trigPulse[i].process(dt) ? 10.f : 0.f);
            lights[GATE_LIGHT + i].setBrightness(gate ? 1.f : 0.f);

            // LFO output
            int steps = engines[i].steps;
            int currentStep = engines[i].currentStep;
            float phase = (steps > 0) ? (float)currentStep / (float)steps : 0.f;
            float lfoValue = generateLFOWave2(i, phase);
            outputs[LFO_OUTPUT + i].setVoltage(lfoValue * 5.f);
        }

        // Update LED matrix lights (4 states × 2 outputs)
        uint8_t currentState = currentInputState.load();
        for (int state = 0; state < 4; state++) {
            uint8_t outputMask = truthTable.getMapping(state);
            bool isCurrentState = (state == currentState);
            for (int bit = 0; bit < 2; bit++) {
                bool isSet = (outputMask >> bit) & 1;
                float brightness = isSet ? (isCurrentState ? 1.f : 0.5f) : (isCurrentState ? 0.2f : 0.05f);
                lights[LED_MATRIX_LIGHT + state * 2 + bit].setBrightness(brightness);
            }
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "clockPeriod", json_real(clockPeriod));

        json_t* mappingJ = json_array();
        auto mapping = truthTable.serialize();
        for (int i = 0; i < 4; i++) {
            json_array_append_new(mappingJ, json_integer(mapping[i]));
        }
        json_object_set_new(rootJ, "truthTable", mappingJ);

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* periodJ = json_object_get(rootJ, "clockPeriod");
        if (periodJ) {
            clockPeriod = json_real_value(periodJ);
        }

        json_t* mappingJ = json_object_get(rootJ, "truthTable");
        if (mappingJ && json_is_array(mappingJ)) {
            std::array<uint8_t, 4> mapping;
            for (int i = 0; i < 4 && i < (int)json_array_size(mappingJ); i++) {
                mapping[i] = json_integer_value(json_array_get(mappingJ, i));
            }
            truthTable.deserialize(mapping);
        }
    }
};

// HitsParamQuantity implementation
float Hits2ParamQuantity::getMaxValue() {
    Euclogic2* euclogic = dynamic_cast<Euclogic2*>(module);
    if (euclogic) {
        int steps = static_cast<int>(euclogic->params[Euclogic2::STEPS_PARAM + channel].getValue());
        return static_cast<float>(steps);
    }
    return 64.f;
}

void Hits2ParamQuantity::setValue(float value) {
    value = math::clamp(value, getMinValue(), getMaxValue());
    ParamQuantity::setValue(value);
}

// Euclidean Pattern Display - shows 2 channels
struct Euclidean2Display : LightWidget {
    Euclogic2* module = nullptr;

    Euclidean2Display() {
        box.size = Vec(100.f, 24.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 160));
        nvgFill(args.vg);

        float rowHeight = box.size.y / 2.f;

        if (!module) {
            nvgFillColor(args.vg, nvgRGBA(80, 100, 140, 200));
            nvgFontSize(args.vg, 9);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2, "EUCLIDEAN", nullptr);
            return;
        }

        for (int ch = 0; ch < 2; ch++) {
            const auto& engine = module->engines[ch];
            float y = ch * rowHeight;
            int steps = engine.steps;
            int currentStep = engine.currentStep;

            if (steps <= 0) continue;

            int visibleSteps = std::min(steps, 32);
            float stepW = (box.size.x - 4.f) / visibleSteps;
            float stepH = rowHeight - 2.f;

            for (int s = 0; s < visibleSteps; s++) {
                float x = 2.f + s * stepW;
                bool isHit = engine.getHit(s);
                bool isCurrent = (s == currentStep);

                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, x + 0.5f, y + 1.f, stepW - 1.f, stepH, 2.f);

                if (isCurrent) {
                    if (isHit) {
                        nvgFillColor(args.vg, nvgRGBA(100, 200, 255, 255));
                    } else {
                        nvgFillColor(args.vg, nvgRGBA(180, 140, 255, 220));
                    }
                } else {
                    if (isHit) {
                        nvgFillColor(args.vg, nvgRGBA(60, 120, 180, 200));
                    } else {
                        nvgFillColor(args.vg, nvgRGBA(30, 35, 50, 120));
                    }
                }
                nvgFill(args.vg);

                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, x + 0.5f, y + 1.f, stepW - 1.f, stepH, 2.f);
                if (isCurrent) {
                    nvgStrokeColor(args.vg, nvgRGBA(200, 220, 255, 255));
                    nvgStrokeWidth(args.vg, 1.5f);
                } else {
                    nvgStrokeColor(args.vg, nvgRGBA(60, 70, 100, 100));
                    nvgStrokeWidth(args.vg, 0.5f);
                }
                nvgStroke(args.vg);
            }
        }

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgStrokeColor(args.vg, nvgRGBA(80, 100, 140, 150));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);
    }
};

// Truth Table Display - 4 rows (2 inputs = 4 states)
struct TruthTable2Display : LightWidget {
    Euclogic2* module = nullptr;

    TruthTable2Display() {
        box.size = Vec(60.f, 40.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        float rowH = box.size.y / 4.f;
        float inputColW = box.size.x * 0.5f;
        float outputColW = box.size.x * 0.5f;
        float singleInputW = inputColW / 2.f;
        float singleOutputW = outputColW / 2.f;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 160));
        nvgFill(args.vg);

        uint8_t currentState = module ? module->currentInputState.load() : 0;

        for (int state = 0; state < 4; state++) {
            float y = state * rowH;
            bool isActive = module && (state == currentState);

            if (isActive) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 0, y, box.size.x, rowH);
                nvgFillColor(args.vg, nvgRGBA(60, 100, 160, 150));
                nvgFill(args.vg);
            }

            // Input columns (A B)
            for (int bit = 0; bit < 2; bit++) {
                bool bitValue = (state >> (1 - bit)) & 1;
                float x = bit * singleInputW;

                nvgFillColor(args.vg, bitValue ? nvgRGBA(100, 180, 255, 220) : nvgRGBA(80, 80, 120, 150));
                nvgFontSize(args.vg, rowH * 0.7f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgText(args.vg, x + singleInputW / 2.f, y + rowH / 2.f, bitValue ? "T" : "F", nullptr);
            }

            // Separator
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, inputColW, y);
            nvgLineTo(args.vg, inputColW, y + rowH);
            nvgStrokeColor(args.vg, nvgRGBA(60, 80, 120, 150));
            nvgStrokeWidth(args.vg, 1.f);
            nvgStroke(args.vg);

            // Output columns (1 2)
            uint8_t outputMask = module ? module->truthTable.getMapping(state) : 0;
            for (int outBit = 0; outBit < 2; outBit++) {
                bool isSet = (outputMask >> outBit) & 1;
                float x = inputColW + outBit * singleOutputW;

                float btnSize = std::min(singleOutputW, rowH) * 0.7f;
                float btnX = x + (singleOutputW - btnSize) / 2.f;
                float btnY = y + (rowH - btnSize) / 2.f;

                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, btnX, btnY, btnSize, btnSize, 2.f);

                if (isSet) {
                    if (isActive) {
                        nvgFillColor(args.vg, nvgRGBA(100, 200, 255, 255));
                    } else {
                        nvgFillColor(args.vg, nvgRGBA(60, 140, 200, 200));
                    }
                } else {
                    nvgFillColor(args.vg, nvgRGBA(30, 35, 50, 120));
                }
                nvgFill(args.vg);

                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, btnX, btnY, btnSize, btnSize, 2.f);
                nvgStrokeColor(args.vg, isSet ? nvgRGBA(120, 180, 255, 180) : nvgRGBA(50, 60, 80, 100));
                nvgStrokeWidth(args.vg, 0.5f);
                nvgStroke(args.vg);
            }

            if (state < 3) {
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, 0, y + rowH);
                nvgLineTo(args.vg, box.size.x, y + rowH);
                nvgStrokeColor(args.vg, nvgRGBA(40, 50, 70, 100));
                nvgStrokeWidth(args.vg, 0.5f);
                nvgStroke(args.vg);
            }
        }

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgStrokeColor(args.vg, nvgRGBA(80, 100, 140, 150));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);
    }

    void onButton(const event::Button& e) override {
        if (!module) return;
        if (e.action != GLFW_PRESS) return;
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;

        float rowH = box.size.y / 4.f;
        float inputColW = box.size.x * 0.5f;
        float singleOutputW = (box.size.x - inputColW) / 2.f;

        if (e.pos.x < inputColW) return;

        int row = static_cast<int>(e.pos.y / rowH);
        int outBit = static_cast<int>((e.pos.x - inputColW) / singleOutputW);

        if (row >= 0 && row < 4 && outBit >= 0 && outBit < 2) {
            module->truthTable.pushUndo();
            module->truthTable.toggleBit(row, outBit);
            e.consume(this);
        }
    }
};

// Snap knob for discrete switch parameters
struct RoundBlackSnapKnob2 : RoundBlackKnob {
    RoundBlackSnapKnob2() {
        snap = true;
    }
};

struct Euclogic2Widget : ModuleWidget {
    Euclogic2Widget(Euclogic2* module) {
        setModule(module);
        box.size = Vec(20 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);  // 20HP
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/Euclogic2.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Layout: 20HP = 101.6mm wide
        float panelWidth = 101.6f;

        // Top: Euclidean display (y=16-32mm)
        float yEucDisplay = 16.f;
        Euclidean2Display* eucDisplay = createWidget<Euclidean2Display>(mm2px(Vec(4.f, yEucDisplay)));
        eucDisplay->module = module;
        eucDisplay->box.size = mm2px(Vec(panelWidth - 8.f, 16.f));
        addChild(eucDisplay);

        // Channel controls (2 rows)
        float yChannel1 = 36.f;
        float yChannel2 = 48.f;
        float col1 = 8.f;   // Steps
        float col2 = 20.f;  // Hits
        float col3 = 32.f;  // Hits CV
        float col4 = 44.f;  // Quant
        float col5 = 56.f;  // Prob A
        float col6 = 68.f;  // Prob A CV
        float col7 = 80.f;  // Prob B
        float col8 = 92.f;  // Prob B CV

        for (int i = 0; i < Euclogic2::NUM_CHANNELS; i++) {
            float y = (i == 0) ? yChannel1 : yChannel2;

            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col1, y)), module, Euclogic2::STEPS_PARAM + i));
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col2, y)), module, Euclogic2::HITS_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col3, y)), module, Euclogic2::HITS_CV_INPUT + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col4, y)), module, Euclogic2::QUANT_PARAM + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col5, y)), module, Euclogic2::PROB_A_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col6, y)), module, Euclogic2::PROB_A_CV_INPUT + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col7, y)), module, Euclogic2::PROB_B_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col8, y)), module, Euclogic2::PROB_B_CV_INPUT + i));
        }

        // Retrigger switches after channel rows
        float yRetrig = 58.f;
        addParam(createParamCentered<CKSS>(mm2px(Vec(col1, yRetrig)), module, Euclogic2::RETRIG_PARAM + 0));
        addParam(createParamCentered<CKSS>(mm2px(Vec(col2, yRetrig)), module, Euclogic2::RETRIG_PARAM + 1));

        // Master clock inputs (same row as retrig)
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col5, yRetrig)), module, Euclogic2::CLOCK_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(col5 + 5.f, yRetrig - 4.f)), module, Euclogic2::CLOCK_LIGHT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col6, yRetrig)), module, Euclogic2::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col7, yRetrig)), module, Euclogic2::RUN_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(col7 + 5.f, yRetrig - 4.f)), module, Euclogic2::RUN_LIGHT));

        // Truth table display (y=68-94mm, positioned on right side)
        float yTable = 68.f;
        TruthTable2Display* truthTable = createWidget<TruthTable2Display>(mm2px(Vec(panelWidth - 54.f, yTable)));
        truthTable->module = module;
        truthTable->box.size = mm2px(Vec(50.f, 26.f));
        addChild(truthTable);

        // Speed and Swing (left side, same row as truth table)
        float ySpeedRow = 78.f;
        addParam(createParamCentered<RoundBlackSnapKnob2>(mm2px(Vec(col1 + 6.f, ySpeedRow)), module, Euclogic2::MASTER_SPEED_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col3, ySpeedRow)), module, Euclogic2::SWING_PARAM));

        // Buttons row (below speed knobs)
        float yButtons = 94.f;
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col1, yButtons)), module, Euclogic2::RANDOM_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col2, yButtons)), module, Euclogic2::MUTATE_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col3, yButtons)), module, Euclogic2::UNDO_PARAM));

        // Outputs row (bottom)
        float yOutputs = 110.f;
        for (int i = 0; i < Euclogic2::NUM_CHANNELS; i++) {
            float xBase = 12.f + i * 40.f;
            // Gate
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase, yOutputs)), module, Euclogic2::GATE_OUTPUT + i));
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(xBase, yOutputs - 5)), module, Euclogic2::GATE_LIGHT + i));
            // Trigger
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase + 12.f, yOutputs)), module, Euclogic2::TRIG_OUTPUT + i));
            // LFO
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase + 24.f, yOutputs)), module, Euclogic2::LFO_OUTPUT + i));
        }
    }
};

} // namespace WiggleRoom

Model* modelEuclogic2 = createModel<WiggleRoom::Euclogic2, WiggleRoom::Euclogic2Widget>("Euclogic2");
