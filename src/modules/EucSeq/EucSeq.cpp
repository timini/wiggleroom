/******************************************************************************
 * EUCSEQ
 * 4-channel Euclidean sequencer with per-step CV values
 *
 * Signal flow:
 *   Clock -> Master Speed -> 4x Euclidean Engines -> Prob A -> Outputs
 *
 * Features:
 *   - 4 independent Euclidean rhythm generators
 *   - Per-channel: Steps (1-64), Hits (0-Steps), Quant ratio, Probability
 *   - Per-step CV values (0-10V or -5V to 5V bipolar) with interactive bar editor
 *   - 4x Gate + 4x Trigger + 4x LFO + 4x CV outputs
 *   - Right expander sends state to LogicMangler
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include "euclogic/EuclideanEngine.hpp"
#include "euclogic/ProbabilityGate.hpp"
#include "euclogic/ExpanderMessage.hpp"
#include <atomic>
#include <vector>
#include <string>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

// ============================================================================
// Shared constants
// ============================================================================

namespace EucSeqConstants {
    static constexpr float TRIGGER_PULSE_DURATION = 1e-3f;
    static constexpr float RETRIG_GAP_DURATION = 0.5e-3f;
    static constexpr float SCHMITT_LOW = 0.1f;
    static constexpr float SCHMITT_HIGH = 1.0f;
    static constexpr float DEFAULT_CLOCK_PERIOD = 0.5f;

    inline const std::vector<float>& speedRatios() {
        static const std::vector<float> v = {
            1.f/16, 1.f/12, 1.f/8, 1.f/6, 1.f/4, 1.f/3, 1.f/2, 2.f/3,
            1.f, 3.f/2, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f
        };
        return v;
    }

    inline const std::vector<std::string>& speedLabels() {
        static const std::vector<std::string> v = {
            "/16", "/12", "/8", "/6", "/4", "/3", "/2", "/1.5",
            "x1", "x1.5", "x2", "x3", "x4", "x6", "x8", "x12", "x16"
        };
        return v;
    }

    inline const std::vector<float>& quantRatios() {
        static const std::vector<float> v = { 1.f, 1.f/2, 1.f/4, 1.f/8, 1.f/16 };
        return v;
    }

    inline const std::vector<std::string>& quantLabels() {
        static const std::vector<std::string> v = { "x1", "/2", "/4", "/8", "/16" };
        return v;
    }
}

// ============================================================================
// Snap knob
// ============================================================================

struct EucSeqSnapKnob : RoundBlackKnob {
    EucSeqSnapKnob() { snap = true; }
};

// ============================================================================
// HitsParamQuantity - limits hits to current steps value
// ============================================================================

struct EucSeqModule;

struct EucSeqHitsParamQuantity : ParamQuantity {
    int channel = 0;
    float getMaxValue() override;
    void setValue(float value) override;
};

// ============================================================================
// EucSeq Module
// ============================================================================

struct EucSeqModule : Module {
    static constexpr int NUM_CHANNELS = 4;
    static constexpr int MAX_STEPS = 64;

    enum ParamId {
        MASTER_SPEED_PARAM,
        SWING_PARAM,
        ENUMS(STEPS_PARAM, NUM_CHANNELS),
        ENUMS(HITS_PARAM, NUM_CHANNELS),
        ENUMS(QUANT_PARAM, NUM_CHANNELS),
        ENUMS(PROB_A_PARAM, NUM_CHANNELS),
        ENUMS(RETRIG_PARAM, NUM_CHANNELS),
        ENUMS(BIPOLAR_PARAM, NUM_CHANNELS),
        PARAMS_LEN
    };

    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        RUN_INPUT,
        ENUMS(HITS_CV_INPUT, NUM_CHANNELS),
        ENUMS(PROB_A_CV_INPUT, NUM_CHANNELS),
        ENUMS(STEPS_CV_INPUT, NUM_CHANNELS),
        INPUTS_LEN
    };

    enum OutputId {
        ENUMS(GATE_OUTPUT, NUM_CHANNELS),
        ENUMS(TRIG_OUTPUT, NUM_CHANNELS),
        ENUMS(LFO_OUTPUT, NUM_CHANNELS),
        ENUMS(CV_OUTPUT, NUM_CHANNELS),
        OUTPUTS_LEN
    };

    enum LightId {
        CLOCK_LIGHT,
        RUN_LIGHT,
        ENUMS(GATE_LIGHT, NUM_CHANNELS),
        LIGHTS_LEN
    };

    // Core components
    EuclideanEngine engines[NUM_CHANNELS];
    ProbabilityGate probA[NUM_CHANNELS];

    // Per-step CV values
    float cvValues[NUM_CHANNELS][MAX_STEPS] = {};

    // Clock state
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;

    float clockPeriod = EucSeqConstants::DEFAULT_CLOCK_PERIOD;
    float timeSinceClock = 0.f;
    float internalTickPhase = 0.f;
    bool clockLocked = false;
    bool running = true;
    bool hadFirstTick = false;
    int prevSpeedIdx = 8;
    bool swingLong = true;

    // Per-channel state
    int quantCounter[NUM_CHANNELS] = {};
    dsp::PulseGenerator trigPulse[NUM_CHANNELS];
    bool prevGateHigh[NUM_CHANNELS] = {};
    float retrigGapTimer[NUM_CHANNELS] = {};

    // Display state
    std::atomic<bool> gateStates[NUM_CHANNELS];

    // Expander message (double-buffered)
    EuclogicExpanderMessage rightMessages[2];

    EucSeqModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        const auto& sr = EucSeqConstants::speedRatios();
        const auto& sl = EucSeqConstants::speedLabels();
        const auto& ql = EucSeqConstants::quantLabels();

        configSwitch(MASTER_SPEED_PARAM, 0.f, sr.size() - 1, 8.f, "Master Speed", sl);
        configParam(SWING_PARAM, 50.f, 75.f, 50.f, "Swing", "%");

        for (int i = 0; i < NUM_CHANNELS; i++) {
            std::string ch = "Ch " + std::to_string(i + 1);

            configParam(STEPS_PARAM + i, 1.f, 64.f, 16.f, ch + " Steps");
            paramQuantities[STEPS_PARAM + i]->snapEnabled = true;

            configParam(HITS_PARAM + i, 0.f, 64.f, 8.f, ch + " Hits");
            paramQuantities[HITS_PARAM + i]->snapEnabled = true;

            auto* hitsQ = new EucSeqHitsParamQuantity();
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

            configSwitch(QUANT_PARAM + i, 0.f, 4.f, 0.f, ch + " Quant", ql);
            configParam(PROB_A_PARAM + i, 0.f, 1.f, 1.f, ch + " Probability", "%", 0.f, 100.f);
            configSwitch(RETRIG_PARAM + i, 0.f, 1.f, 1.f, ch + " Retrigger", {"Off", "On"});
            configSwitch(BIPOLAR_PARAM + i, 0.f, 1.f, 0.f, ch + " CV Bipolar", {"Uni", "Bi"});

            configInput(HITS_CV_INPUT + i, ch + " Hits CV");
            configInput(PROB_A_CV_INPUT + i, ch + " Prob CV");
            configInput(STEPS_CV_INPUT + i, ch + " Steps CV");

            configOutput(GATE_OUTPUT + i, ch + " Gate");
            configOutput(TRIG_OUTPUT + i, ch + " Trigger");
            configOutput(LFO_OUTPUT + i, ch + " LFO");
            configOutput(CV_OUTPUT + i, ch + " CV");

            gateStates[i].store(false);
        }

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(RUN_INPUT, "Run");
        configLight(CLOCK_LIGHT, "Clock Lock");
        configLight(RUN_LIGHT, "Running");

        // Initialize CV values to 0
        for (int ch = 0; ch < NUM_CHANNELS; ch++) {
            for (int s = 0; s < MAX_STEPS; s++) {
                cvValues[ch][s] = 0.f;
            }
        }

        // Set up right expander
        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];
    }

    void onReset() override {
        for (int i = 0; i < NUM_CHANNELS; i++) {
            engines[i].reset();
            quantCounter[i] = 0;
            gateStates[i].store(false);
            prevGateHigh[i] = false;
            retrigGapTimer[i] = 0.f;
            for (int s = 0; s < MAX_STEPS; s++) {
                cvValues[i][s] = 0.f;
            }
        }
        timeSinceClock = 0.f;
        internalTickPhase = 0.f;
        clockPeriod = EucSeqConstants::DEFAULT_CLOCK_PERIOD;
        running = true;
        hadFirstTick = false;
        prevSpeedIdx = 8;
        swingLong = true;
    }

    void process(const ProcessArgs& args) override {
        float dt = args.sampleTime;

        // Handle reset
        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(),
                EucSeqConstants::SCHMITT_LOW, EucSeqConstants::SCHMITT_HIGH)) {
            onReset();
        }

        // Handle run gate
        if (inputs[RUN_INPUT].isConnected()) {
            running = inputs[RUN_INPUT].getVoltage() > EucSeqConstants::SCHMITT_HIGH;
        } else {
            running = true;
        }
        lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.2f);

        // Clock handling
        timeSinceClock += dt;
        bool clockEdge = false;
        if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(),
                EucSeqConstants::SCHMITT_LOW, EucSeqConstants::SCHMITT_HIGH)) {
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

        // Master speed
        const auto& speedRatios = EucSeqConstants::speedRatios();
        int speedIdx = static_cast<int>(params[MASTER_SPEED_PARAM].getValue());
        speedIdx = DSP::clamp(speedIdx, 0, (int)speedRatios.size() - 1);
        float masterSpeed = speedRatios[speedIdx];

        if (speedIdx != prevSpeedIdx) {
            hadFirstTick = false;
            internalTickPhase = 0.f;
            prevSpeedIdx = speedIdx;
        }

        // Swing
        float swingPercent = params[SWING_PARAM].getValue();
        float swingRatio = swingPercent / 100.f;

        // Determine tick
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
            processTick();
        }

        // Output voltages
        updateOutputs(dt);

        // Send expander message to the right
        updateExpanderMessage();
    }

    void processTick() {
        const auto& quantRatios = EucSeqConstants::quantRatios();

        for (int i = 0; i < NUM_CHANNELS; i++) {
            int quantIdx = static_cast<int>(params[QUANT_PARAM + i].getValue());
            quantIdx = DSP::clamp(quantIdx, 0, (int)quantRatios.size() - 1);
            float quantRatio = quantRatios[quantIdx];

            int divAmount = static_cast<int>(1.f / quantRatio);
            if (divAmount < 1) divAmount = 1;

            quantCounter[i]++;
            if (quantCounter[i] >= divAmount) {
                quantCounter[i] = 0;

                int steps = static_cast<int>(params[STEPS_PARAM + i].getValue());
                float hitsBase = params[HITS_PARAM + i].getValue();
                float hitsCV = inputs[HITS_CV_INPUT + i].getVoltage() / 5.f * 12.f;
                int hitCount = static_cast<int>(hitsBase + hitsCV);
                hitCount = DSP::clamp(hitCount, 0, steps);

                engines[i].configure(steps, hitCount, 0);
                bool euclideanHit = engines[i].tick();

                float probABase = params[PROB_A_PARAM + i].getValue();
                float probACV = inputs[PROB_A_CV_INPUT + i].getVoltage() / 10.f;
                float probAVal = DSP::clamp(probABase + probACV, 0.f, 1.f);

                bool hit = euclideanHit && probA[i].process(true, probAVal);
                bool retrigger = params[RETRIG_PARAM + i].getValue() > 0.5f;

                bool shouldTrigger = hit && (!prevGateHigh[i] || retrigger);
                if (shouldTrigger) {
                    trigPulse[i].trigger(EucSeqConstants::TRIGGER_PULSE_DURATION);
                }

                if (hit && prevGateHigh[i] && retrigger) {
                    retrigGapTimer[i] = EucSeqConstants::RETRIG_GAP_DURATION;
                }

                gateStates[i].store(hit);
                prevGateHigh[i] = hit;
            } else {
                gateStates[i].store(false);
                prevGateHigh[i] = false;
            }
        }
    }

    void updateOutputs(float dt) {
        for (int i = 0; i < NUM_CHANNELS; i++) {
            bool gate = gateStates[i].load();

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
            float phase = (steps > 1) ? (float)currentStep / (float)(steps - 1) : 0.f;
            outputs[LFO_OUTPUT + i].setVoltage(phase * 10.f);

            // CV output from step values
            int stepIdx = (currentStep > 0) ? currentStep - 1 : steps - 1; // tick advances, so current is next
            float cvVal = cvValues[i][stepIdx % MAX_STEPS];
            bool bipolar = params[BIPOLAR_PARAM + i].getValue() > 0.5f;
            if (bipolar) {
                outputs[CV_OUTPUT + i].setVoltage(cvVal * 10.f - 5.f);
            } else {
                outputs[CV_OUTPUT + i].setVoltage(cvVal * 10.f);
            }
        }
    }

    void updateExpanderMessage() {
        // Check if right neighbor is a compatible module
        if (rightExpander.module) {
            EuclogicExpanderMessage* msg = static_cast<EuclogicExpanderMessage*>(rightExpander.producerMessage);
            msg->clear();

            for (int i = 0; i < NUM_CHANNELS; i++) {
                msg->gates[i] = gateStates[i].load();
                msg->triggers[i] = trigPulse[i].remaining > 0.f;
                int steps = engines[i].steps;
                int currentStep = engines[i].currentStep;
                float phase = (steps > 1) ? (float)currentStep / (float)(steps - 1) : 0.f;
                msg->lfo[i] = phase * 10.f;

                int stepIdx = (currentStep > 0) ? currentStep - 1 : steps - 1;
                bool bipolar = params[BIPOLAR_PARAM + i].getValue() > 0.5f;
                float cvVal = cvValues[i][stepIdx % MAX_STEPS];
                msg->cv[i] = bipolar ? (cvVal * 10.f - 5.f) : (cvVal * 10.f);

                msg->currentStep[i] = currentStep;
                msg->totalSteps[i] = steps;

                // Store params for bank recall
                msg->steps[i] = static_cast<int>(params[STEPS_PARAM + i].getValue());
                msg->hits[i] = static_cast<int>(params[HITS_PARAM + i].getValue());
                msg->quant[i] = static_cast<int>(params[QUANT_PARAM + i].getValue());
                msg->probA[i] = params[PROB_A_PARAM + i].getValue();
                msg->retrigger[i] = params[RETRIG_PARAM + i].getValue() > 0.5f;
                msg->bipolar[i] = bipolar;
            }

            msg->speed = params[MASTER_SPEED_PARAM].getValue();
            msg->swing = params[SWING_PARAM].getValue();
            msg->valid = true;

            rightExpander.requestMessageFlip();
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "clockPeriod", json_real(clockPeriod));

        // Save CV values per channel
        json_t* cvArrayJ = json_array();
        for (int ch = 0; ch < NUM_CHANNELS; ch++) {
            json_t* chJ = json_array();
            int steps = static_cast<int>(params[STEPS_PARAM + ch].getValue());
            for (int s = 0; s < steps; s++) {
                json_array_append_new(chJ, json_real(cvValues[ch][s]));
            }
            json_array_append_new(cvArrayJ, chJ);
        }
        json_object_set_new(rootJ, "cvValues", cvArrayJ);

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* periodJ = json_object_get(rootJ, "clockPeriod");
        if (periodJ) clockPeriod = json_real_value(periodJ);

        json_t* cvArrayJ = json_object_get(rootJ, "cvValues");
        if (cvArrayJ && json_is_array(cvArrayJ)) {
            for (int ch = 0; ch < NUM_CHANNELS && ch < (int)json_array_size(cvArrayJ); ch++) {
                json_t* chJ = json_array_get(cvArrayJ, ch);
                if (chJ && json_is_array(chJ)) {
                    for (int s = 0; s < MAX_STEPS && s < (int)json_array_size(chJ); s++) {
                        cvValues[ch][s] = json_real_value(json_array_get(chJ, s));
                    }
                }
            }
        }
    }
};

// HitsParamQuantity implementation
float EucSeqHitsParamQuantity::getMaxValue() {
    EucSeqModule* m = dynamic_cast<EucSeqModule*>(module);
    if (m) {
        int steps = static_cast<int>(m->params[EucSeqModule::STEPS_PARAM + channel].getValue());
        return static_cast<float>(steps);
    }
    return 64.f;
}

void EucSeqHitsParamQuantity::setValue(float value) {
    value = math::clamp(value, getMinValue(), getMaxValue());
    ParamQuantity::setValue(value);
}

// ============================================================================
// CV Step Bar Display
// ============================================================================

struct CVStepDisplay : OpaqueWidget {
    EucSeqModule* module = nullptr;
    int channel = 0;

    CVStepDisplay() {
        box.size = Vec(200.f, 40.f);
    }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 180));
        nvgFill(args.vg);

        if (!module) {
            nvgFillColor(args.vg, nvgRGBA(80, 100, 140, 200));
            nvgFontSize(args.vg, 9);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2, "CV STEPS", nullptr);
            OpaqueWidget::draw(args);
            return;
        }

        int steps = static_cast<int>(module->params[EucSeqModule::STEPS_PARAM + channel].getValue());
        int currentStep = module->engines[channel].currentStep;
        float barW = box.size.x / std::max(1, steps);

        for (int s = 0; s < steps; s++) {
            float val = module->cvValues[channel][s];
            float barH = val * (box.size.y - 2.f);
            float x = s * barW;
            float y = box.size.y - barH - 1.f;
            bool isCurrent = (s == currentStep);

            nvgBeginPath(args.vg);
            nvgRect(args.vg, x + 0.5f, y, barW - 1.f, barH);

            if (isCurrent) {
                nvgFillColor(args.vg, nvgRGBA(100, 200, 255, 255));
            } else {
                nvgFillColor(args.vg, nvgRGBA(60, 120, 180, 200));
            }
            nvgFill(args.vg);
        }

        // Border
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(args.vg, nvgRGBA(80, 100, 140, 150));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);

        OpaqueWidget::draw(args);
    }

    void onDragHover(const event::DragHover& e) override {
        if (module && e.origin == this) {
            setCVFromPos(e.pos);
        }
        OpaqueWidget::onDragHover(e);
    }

    void onButton(const event::Button& e) override {
        if (!module) return;
        if (e.action != GLFW_PRESS) return;
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;

        setCVFromPos(e.pos);
        e.consume(this);
    }

    void onDragStart(const event::DragStart& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
            e.consume(this);
        }
    }

    void setCVFromPos(Vec pos) {
        int steps = static_cast<int>(module->params[EucSeqModule::STEPS_PARAM + channel].getValue());
        if (steps <= 0) return;

        float barW = box.size.x / steps;
        int stepIdx = static_cast<int>(pos.x / barW);
        stepIdx = DSP::clamp(stepIdx, 0, steps - 1);

        float val = 1.f - (pos.y / box.size.y);
        val = DSP::clamp(val, 0.f, 1.f);
        module->cvValues[channel][stepIdx] = val;
    }
};

// ============================================================================
// Euclidean Pattern Display
// ============================================================================

struct EucSeqPatternDisplay : LightWidget {
    EucSeqModule* module = nullptr;

    EucSeqPatternDisplay() {
        box.size = Vec(200.f, 32.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 160));
        nvgFill(args.vg);

        float rowHeight = box.size.y / 4.f;

        if (!module) {
            nvgFillColor(args.vg, nvgRGBA(80, 100, 140, 200));
            nvgFontSize(args.vg, 10);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2, "EUCLIDEAN PATTERNS", nullptr);
            return;
        }

        for (int ch = 0; ch < 4; ch++) {
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
                    nvgFillColor(args.vg, isHit ? nvgRGBA(100, 200, 255, 255) : nvgRGBA(180, 140, 255, 220));
                } else {
                    nvgFillColor(args.vg, isHit ? nvgRGBA(60, 120, 180, 200) : nvgRGBA(30, 35, 50, 120));
                }
                nvgFill(args.vg);

                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, x + 0.5f, y + 1.f, stepW - 1.f, stepH, 2.f);
                nvgStrokeColor(args.vg, isCurrent ? nvgRGBA(200, 220, 255, 255) : nvgRGBA(60, 70, 100, 100));
                nvgStrokeWidth(args.vg, isCurrent ? 1.5f : 0.5f);
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

// ============================================================================
// Widget
// ============================================================================

struct EucSeqWidget : ModuleWidget {
    EucSeqWidget(EucSeqModule* module) {
        setModule(module);
        box.size = Vec(20 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/EucSeq.png"), box.size));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float panelWidth = 101.6f;

        // Euclidean pattern display
        float yEucDisplay = 16.f;
        EucSeqPatternDisplay* eucDisplay = createWidget<EucSeqPatternDisplay>(mm2px(Vec(4.f, yEucDisplay)));
        eucDisplay->module = module;
        eucDisplay->box.size = mm2px(Vec(panelWidth - 8.f, 16.f));
        addChild(eucDisplay);

        // Channel knobs
        float yChannelStart = 36.f;
        float yChannelSpacing = 10.f;
        float col1 = 6.f, col2 = 18.f, col3 = 30.f, col4 = 42.f, col5 = 54.f;
        float col6 = 66.f, col7 = 78.f, col8 = 90.f;

        for (int i = 0; i < 4; i++) {
            float y = yChannelStart + i * yChannelSpacing;

            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col1, y)), module, EucSeqModule::STEPS_PARAM + i));
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col2, y)), module, EucSeqModule::HITS_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col3, y)), module, EucSeqModule::HITS_CV_INPUT + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col4, y)), module, EucSeqModule::QUANT_PARAM + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col5, y)), module, EucSeqModule::PROB_A_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col6, y)), module, EucSeqModule::PROB_A_CV_INPUT + i));
            addParam(createParamCentered<CKSS>(mm2px(Vec(col7, y)), module, EucSeqModule::RETRIG_PARAM + i));
            addParam(createParamCentered<CKSS>(mm2px(Vec(col8, y)), module, EucSeqModule::BIPOLAR_PARAM + i));
        }

        // CV step bar displays (4 rows)
        float yCvStart = 80.f;
        float yCvHeight = 8.f;
        float yCvSpacing = 9.f;
        for (int i = 0; i < 4; i++) {
            CVStepDisplay* cvDisp = createWidget<CVStepDisplay>(mm2px(Vec(4.f, yCvStart + i * yCvSpacing)));
            cvDisp->module = module;
            cvDisp->channel = i;
            cvDisp->box.size = mm2px(Vec(panelWidth - 8.f, yCvHeight));
            addChild(cvDisp);
        }

        // Master controls
        float yMaster = 120.f;
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.f, yMaster)), module, EucSeqModule::CLOCK_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(10.f, yMaster - 5)), module, EucSeqModule::CLOCK_LIGHT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.f, yMaster)), module, EucSeqModule::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(34.f, yMaster)), module, EucSeqModule::RUN_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(34.f, yMaster - 5)), module, EucSeqModule::RUN_LIGHT));

        addParam(createParamCentered<EucSeqSnapKnob>(mm2px(Vec(50.f, yMaster)), module, EucSeqModule::MASTER_SPEED_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(64.f, yMaster)), module, EucSeqModule::SWING_PARAM));

        // Outputs row
        float yOut = 132.f;
        float outSpacing = 11.f;
        for (int i = 0; i < 4; i++) {
            float xBase = 6.f + i * 24.f;
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase, yOut)), module, EucSeqModule::GATE_OUTPUT + i));
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(xBase, yOut - 5)), module, EucSeqModule::GATE_LIGHT + i));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase + outSpacing, yOut)), module, EucSeqModule::TRIG_OUTPUT + i));
        }

        float yOut2 = 144.f;
        for (int i = 0; i < 4; i++) {
            float xBase = 6.f + i * 24.f;
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase, yOut2)), module, EucSeqModule::LFO_OUTPUT + i));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase + outSpacing, yOut2)), module, EucSeqModule::CV_OUTPUT + i));
        }
    }
};

} // namespace WiggleRoom

Model* modelEucSeq = createModel<WiggleRoom::EucSeqModule, WiggleRoom::EucSeqWidget>("EucSeq");
