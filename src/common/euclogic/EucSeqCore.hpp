#pragma once
/******************************************************************************
 * EUCSEQ CORE
 * N-channel Euclidean sequencer template — shared logic for 2/3/4ch variants
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include "euclogic/EuclideanEngine.hpp"
#include "euclogic/ProbabilityGate.hpp"
#include "euclogic/ExpanderMessage.hpp"
#include "euclogic/EuclogicModels.hpp"
#include <atomic>
#include <vector>
#include <string>
#include <cmath>

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
// Forward declare for HitsParamQuantity
// ============================================================================

template<int N> struct EucSeqModuleT;

// ============================================================================
// HitsParamQuantity - limits hits to current steps value
// ============================================================================

template<int N>
struct EucSeqHitsParamQuantityT : ParamQuantity {
    int channel = 0;

    float getMaxValue() override {
        auto* m = dynamic_cast<EucSeqModuleT<N>*>(module);
        if (m) {
            int steps = static_cast<int>(m->params[EucSeqModuleT<N>::STEPS_PARAM + channel].getValue());
            return static_cast<float>(steps);
        }
        return 64.f;
    }

    void setValue(float value) override {
        value = math::clamp(value, getMinValue(), getMaxValue());
        ParamQuantity::setValue(value);
    }
};

// ============================================================================
// EucSeq Module Template
// ============================================================================

template<int N>
struct EucSeqModuleT : Module {
    static constexpr int NUM_CHANNELS = N;
    static constexpr int MAX_STEPS = 64;

    enum ParamId {
        MASTER_SPEED_PARAM,
        SWING_PARAM,
        ENUMS(STEPS_PARAM, N),
        ENUMS(HITS_PARAM, N),
        ENUMS(QUANT_PARAM, N),
        ENUMS(PROB_A_PARAM, N),
        ENUMS(RETRIG_PARAM, N),
        ENUMS(BIPOLAR_PARAM, N),
        ENUMS(RAND_CV_PARAM, N),
        ENUMS(SCALE_PARAM, N),
        PARAMS_LEN
    };

    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        RUN_INPUT,
        ENUMS(HITS_CV_INPUT, N),
        ENUMS(PROB_A_CV_INPUT, N),
        ENUMS(STEPS_CV_INPUT, N),
        SCALE_BUS_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        ENUMS(GATE_OUTPUT, N),
        ENUMS(TRIG_OUTPUT, N),
        ENUMS(CV_OUTPUT, N),
        ENUMS(LFO_OUTPUT, N),
        OUTPUTS_LEN
    };

    enum LightId {
        CLOCK_LIGHT,
        RUN_LIGHT,
        ENUMS(GATE_LIGHT, N),
        LIGHTS_LEN
    };

    EuclideanEngine engines[N];
    ProbabilityGate probA[N];
    float cvValues[N][MAX_STEPS] = {};

    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger randCvTrigger[N];

    // The step each channel last played, or -1 before the first tick.
    // Deriving this from currentStep read as steps-1 at reset, which
    // parked the LFO at 10V instead of 0V. Tracking it explicitly also
    // survives a Steps change resetting the engine.
    int playedStep[N];

    float clockPeriod = EucSeqConstants::DEFAULT_CLOCK_PERIOD;
    float timeSinceClock = 0.f;
    float internalTickPhase = 0.f;
    bool clockLocked = false;
    bool running = true;
    bool hadFirstTick = false;
    int prevSpeedIdx = 8;
    bool swingLong = true;

    int quantCounter[N] = {};
    dsp::PulseGenerator trigPulse[N];
    bool prevGateHigh[N] = {};
    float retrigGapTimer[N] = {};
    std::atomic<bool> gateStates[N];

    int scaleMask = 0xFFF;
    int scaleRoot = 0;
    bool scaleConnected = false;

    EuclogicExpanderMessageT<N> rightMessages[2];

    float quantizeToScale(float voltage) {
        if (!scaleConnected) return voltage;
        float midiNote = voltage * 12.f + 60.f;
        int octave = static_cast<int>(std::floor(midiNote / 12.f));
        int chromaRaw = static_cast<int>(std::round(midiNote)) % 12;
        int chroma = (chromaRaw + 12) % 12;
        int relNote = (chroma - scaleRoot + 12) % 12;
        if ((scaleMask >> relNote) & 1) {
            float qMidi = octave * 12.f + chroma;
            return (qMidi - 60.f) / 12.f;
        }
        for (int offset = 1; offset <= 6; offset++) {
            int lower = (relNote - offset + 12) % 12;
            int upper = (relNote + offset) % 12;
            if ((scaleMask >> lower) & 1) {
                int qChroma = (chroma - offset + 12) % 12;
                float qMidi = octave * 12.f + qChroma;
                return (qMidi - 60.f) / 12.f;
            }
            if ((scaleMask >> upper) & 1) {
                int qChroma = (chroma + offset) % 12;
                float qMidi = octave * 12.f + qChroma;
                return (qMidi - 60.f) / 12.f;
            }
        }
        return voltage;
    }

    EucSeqModuleT() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        const auto& sr = EucSeqConstants::speedRatios();
        const auto& sl = EucSeqConstants::speedLabels();
        const auto& ql = EucSeqConstants::quantLabels();

        configSwitch(MASTER_SPEED_PARAM, 0.f, sr.size() - 1, 8.f, "Master Speed", sl);
        configParam(SWING_PARAM, 50.f, 75.f, 50.f, "Swing", "%");

        for (int i = 0; i < N; i++) {
            std::string ch = "Ch " + std::to_string(i + 1);
            configParam(STEPS_PARAM + i, 1.f, 64.f, 16.f, ch + " Steps");
            paramQuantities[STEPS_PARAM + i]->snapEnabled = true;

            configParam(HITS_PARAM + i, 0.f, 64.f, 8.f, ch + " Hits");
            paramQuantities[HITS_PARAM + i]->snapEnabled = true;

            auto* hitsQ = new EucSeqHitsParamQuantityT<N>();
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
            configButton(RAND_CV_PARAM + i, ch + " Randomize CV");
            configParam(SCALE_PARAM + i, 1.f, 10.f, 5.f, ch + " Scale", "V");
            paramQuantities[SCALE_PARAM + i]->snapEnabled = true;

            configInput(HITS_CV_INPUT + i, ch + " Hits CV");
            configInput(PROB_A_CV_INPUT + i, ch + " Prob CV");
            configInput(STEPS_CV_INPUT + i, ch + " Steps CV");
            configOutput(GATE_OUTPUT + i, ch + " Gate");
            configOutput(TRIG_OUTPUT + i, ch + " Trigger");
            configOutput(CV_OUTPUT + i, ch + " CV");
            configOutput(LFO_OUTPUT + i, ch + " LFO");

            gateStates[i].store(false);
        }

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(RUN_INPUT, "Run");
        configInput(SCALE_BUS_INPUT, "Scale Bus");
        configLight(CLOCK_LIGHT, "Clock Lock");
        configLight(RUN_LIGHT, "Running");

        for (int ch = 0; ch < N; ch++)
            for (int s = 0; s < MAX_STEPS; s++)
                cvValues[ch][s] = 0.f;

        for (int ch = 0; ch < N; ch++) playedStep[ch] = -1;

        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];
    }

    void onReset() override {
        for (int i = 0; i < N; i++) {
            engines[i].reset();
            playedStep[i] = -1;
            quantCounter[i] = 0;
            gateStates[i].store(false);
            prevGateHigh[i] = false;
            retrigGapTimer[i] = 0.f;
            for (int s = 0; s < MAX_STEPS; s++)
                cvValues[i][s] = 0.f;
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

        if (inputs[SCALE_BUS_INPUT].isConnected()) {
            int channels = inputs[SCALE_BUS_INPUT].getChannels();
            if (channels >= 12) {
                scaleConnected = true;
                scaleMask = 0;
                for (int i = 0; i < 12; i++) {
                    if (inputs[SCALE_BUS_INPUT].getVoltage(i) > 0.5f)
                        scaleMask |= (1 << i);
                }
                if (channels >= 16) {
                    scaleRoot = static_cast<int>(std::round(inputs[SCALE_BUS_INPUT].getVoltage(15) * 12.f)) % 12;
                    if (scaleRoot < 0) scaleRoot += 12;
                }
            }
        } else {
            scaleConnected = false;
        }

        for (int i = 0; i < N; i++) {
            if (randCvTrigger[i].process(params[RAND_CV_PARAM + i].getValue())) {
                int steps = static_cast<int>(params[STEPS_PARAM + i].getValue());
                for (int s = 0; s < steps; s++)
                    cvValues[i][s] = random::uniform();
            }
        }

        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(),
                EucSeqConstants::SCHMITT_LOW, EucSeqConstants::SCHMITT_HIGH)) {
            for (int i = 0; i < N; i++) {
                engines[i].reset();
                playedStep[i] = -1;
                quantCounter[i] = 0;
                gateStates[i].store(false);
                prevGateHigh[i] = false;
                retrigGapTimer[i] = 0.f;
            }
            timeSinceClock = 0.f;
            internalTickPhase = 0.f;
            hadFirstTick = false;
            swingLong = true;
        }

        if (inputs[RUN_INPUT].isConnected()) {
            running = inputs[RUN_INPUT].getVoltage() > EucSeqConstants::SCHMITT_HIGH;
        } else {
            running = true;
        }
        lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.2f);

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

        const auto& speedRatios = EucSeqConstants::speedRatios();
        int speedIdx = static_cast<int>(params[MASTER_SPEED_PARAM].getValue());
        speedIdx = DSP::clamp(speedIdx, 0, (int)speedRatios.size() - 1);
        float masterSpeed = speedRatios[speedIdx];

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

        for (int i = 0; i < N; i++) {
            int steps = static_cast<int>(params[STEPS_PARAM + i].getValue());
            int hitCount = static_cast<int>(params[HITS_PARAM + i].getValue());
            hitCount = DSP::clamp(hitCount, 0, steps);

            // configure() zeroes currentStep when the length changes. The cached
            // playhead would otherwise keep reporting the old pattern's index
            // until the next tick, which on a divided channel is several master
            // ticks away, so clear it and sit on step 0 until then.
            int prevSteps = engines[i].steps;
            engines[i].configure(steps, hitCount, 0);
            if (engines[i].steps != prevSteps) playedStep[i] = -1;
        }

        if (shouldTick) processTick();
        updateOutputs(dt);
        updateExpanderMessage();
    }

    void processTick() {
        const auto& quantRatios = EucSeqConstants::quantRatios();
        for (int i = 0; i < N; i++) {
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
                playedStep[i] = engines[i].currentStep;
                bool euclideanHit = engines[i].tick();

                float probABase = params[PROB_A_PARAM + i].getValue();
                float probACV = inputs[PROB_A_CV_INPUT + i].getVoltage() / 10.f;
                float probAVal = DSP::clamp(probABase + probACV, 0.f, 1.f);

                bool hit = euclideanHit && probA[i].process(true, probAVal);
                bool retrigger = params[RETRIG_PARAM + i].getValue() > 0.5f;

                if (hit && (!prevGateHigh[i] || retrigger))
                    trigPulse[i].trigger(EucSeqConstants::TRIGGER_PULSE_DURATION);
                if (hit && prevGateHigh[i] && retrigger)
                    retrigGapTimer[i] = EucSeqConstants::RETRIG_GAP_DURATION;

                gateStates[i].store(hit);
                prevGateHigh[i] = hit;
            }
        }
    }

    void updateOutputs(float dt) {
        for (int i = 0; i < N; i++) {
            bool gate = gateStates[i].load();
            if (retrigGapTimer[i] > 0.f) {
                retrigGapTimer[i] -= dt;
                outputs[GATE_OUTPUT + i].setVoltage(0.f);
            } else {
                outputs[GATE_OUTPUT + i].setVoltage(gate ? 10.f : 0.f);
            }
            outputs[TRIG_OUTPUT + i].setVoltage(trigPulse[i].process(dt) ? 10.f : 0.f);
            lights[GATE_LIGHT + i].setBrightness(gate ? 1.f : 0.f);

            int steps = engines[i].steps;
            // Before the first tick nothing has played, so sit on step 0
            int stepIdx = (playedStep[i] >= 0) ? DSP::clamp(playedStep[i], 0, steps - 1) : 0;
            float cvVal = cvValues[i][stepIdx % MAX_STEPS];
            bool bipolar = params[BIPOLAR_PARAM + i].getValue() > 0.5f;
            float scale = params[SCALE_PARAM + i].getValue();
            float cvOut = bipolar ? (cvVal * scale - scale * 0.5f) : (cvVal * scale);
            outputs[CV_OUTPUT + i].setVoltage(quantizeToScale(cvOut));

            // LFO: unipolar staircase tracking the step just played. Dividing by
            // (steps - 1) makes the ramp reach a full 10V on the last step, which
            // was the fix for issue #21 before the split removed this output.
            float lfoPhase = (steps > 1) ? (float)stepIdx / (float)(steps - 1) : 0.f;
            outputs[LFO_OUTPUT + i].setVoltage(lfoPhase * 10.f);
        }
    }

    void updateExpanderMessage() {
        using Models = EuclogicModels<N>;
        if (rightExpander.module &&
            (rightExpander.module->model == Models::logicMangler() ||
             rightExpander.module->model == Models::eucBank() ||
             rightExpander.module->model == Models::eucMix())) {
            auto* msg = static_cast<EuclogicExpanderMessageT<N>*>(rightExpander.producerMessage);
            msg->clear();

            for (int i = 0; i < N; i++) {
                msg->gates[i] = gateStates[i].load();
                msg->triggers[i] = trigPulse[i].remaining > 0.f;
                int steps = engines[i].steps;
                // Before the first tick nothing has played, so sit on step 0
                int stepIdx = (playedStep[i] >= 0) ? DSP::clamp(playedStep[i], 0, steps - 1) : 0;

                // Same phase the LFO port emits, so the bus and the jack agree
                float phase = (steps > 1) ? (float)stepIdx / (float)(steps - 1) : 0.f;
                msg->lfo[i] = phase * 10.f;
                bool bipolar = params[BIPOLAR_PARAM + i].getValue() > 0.5f;
                float cvVal = cvValues[i][stepIdx % MAX_STEPS];
                float scale = params[SCALE_PARAM + i].getValue();
                float cvOut = bipolar ? (cvVal * scale - scale * 0.5f) : (cvVal * scale);
                msg->cv[i] = quantizeToScale(cvOut);

                // The step actually played, matching the Gate, CV and LFO outputs
                msg->currentStep[i] = stepIdx;
                msg->totalSteps[i] = steps;
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
        json_t* cvArrayJ = json_array();
        for (int ch = 0; ch < N; ch++) {
            json_t* chJ = json_array();
            int steps = static_cast<int>(params[STEPS_PARAM + ch].getValue());
            for (int s = 0; s < steps; s++)
                json_array_append_new(chJ, json_real(cvValues[ch][s]));
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
            for (int ch = 0; ch < N && ch < (int)json_array_size(cvArrayJ); ch++) {
                json_t* chJ = json_array_get(cvArrayJ, ch);
                if (chJ && json_is_array(chJ)) {
                    for (int s = 0; s < MAX_STEPS && s < (int)json_array_size(chJ); s++)
                        cvValues[ch][s] = json_real_value(json_array_get(chJ, s));
                }
            }
        }
    }
};

// ============================================================================
// CV Step Bar Display
// ============================================================================

template<int N>
struct CVStepDisplayT : OpaqueWidget {
    EucSeqModuleT<N>* module = nullptr;
    int channel = 0;

    CVStepDisplayT() { box.size = Vec(200.f, 40.f); }

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

        int steps = static_cast<int>(module->params[EucSeqModuleT<N>::STEPS_PARAM + channel].getValue());
        // The step last played, or -1 before the first tick, so no bar is
        // highlighted rather than falsely lighting the last one.
        int displayStep = module->playedStep[channel];
        float barW = box.size.x / std::max(1, steps);

        for (int s = 0; s < steps; s++) {
            float val = module->cvValues[channel][s];
            float barH = val * (box.size.y - 2.f);
            float x = s * barW;
            float y = box.size.y - barH - 1.f;
            bool isCurrent = (s == displayStep);
            bool isHit = module->engines[channel].getHit(s);

            if (isHit) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, x + 0.5f, 1.f, barW - 1.f, box.size.y - 2.f);
                nvgFillColor(args.vg, nvgRGBA(220, 180, 60, 25));
                nvgFill(args.vg);
            }

            nvgBeginPath(args.vg);
            nvgRect(args.vg, x + 0.5f, y, barW - 1.f, barH);
            nvgFillColor(args.vg, nvgRGBA(80, 160, 220, 230));
            nvgFill(args.vg);

            if (isCurrent) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, x + 0.5f, 1.f, barW - 1.f, box.size.y - 2.f);
                nvgStrokeColor(args.vg, nvgRGBA(255, 220, 80, 255));
                nvgStrokeWidth(args.vg, 2.f);
                nvgStroke(args.vg);
            } else if (isHit) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, x + 0.5f, 1.f, barW - 1.f, box.size.y - 2.f);
                nvgStrokeColor(args.vg, nvgRGBA(180, 120, 255, 255));
                nvgStrokeWidth(args.vg, 2.f);
                nvgStroke(args.vg);
            }
        }

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(args.vg, nvgRGBA(80, 100, 140, 150));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);
        OpaqueWidget::draw(args);
    }

    void onDragHover(const event::DragHover& e) override {
        if (module && e.origin == this) setCVFromPos(e.pos);
        OpaqueWidget::onDragHover(e);
    }

    void onButton(const event::Button& e) override {
        if (!module) return;
        if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        setCVFromPos(e.pos);
        e.consume(this);
    }

    void onDragStart(const event::DragStart& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) e.consume(this);
    }

    void setCVFromPos(Vec pos) {
        int steps = static_cast<int>(module->params[EucSeqModuleT<N>::STEPS_PARAM + channel].getValue());
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
// Widget Template
// ============================================================================

template<int N>
struct EucSeqWidgetT : ModuleWidget {
    // All EucSeq variants are 20HP — same horizontal controls
    static constexpr int HP = 20;
    static constexpr float PANEL_WIDTH_MM = HP * 5.08f;

    EucSeqWidgetT(EucSeqModuleT<N>* module, const std::string& panelFile) {
        this->setModule(module);
        this->box.size = Vec(HP * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        this->addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, panelFile), this->box.size));

        this->addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        this->addChild(createWidget<ScrewSilver>(Vec(this->box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        this->addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        this->addChild(createWidget<ScrewSilver>(Vec(this->box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Master controls at top
        float yMaster = 18.f;
        this->addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.f, yMaster)), module, EucSeqModuleT<N>::CLOCK_INPUT));
        this->addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(10.f, yMaster - 5)), module, EucSeqModuleT<N>::CLOCK_LIGHT));
        this->addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.f, yMaster)), module, EucSeqModuleT<N>::RESET_INPUT));
        this->addInput(createInputCentered<PJ301MPort>(mm2px(Vec(34.f, yMaster)), module, EucSeqModuleT<N>::RUN_INPUT));
        this->addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(34.f, yMaster - 5)), module, EucSeqModuleT<N>::RUN_LIGHT));
        this->addParam(createParamCentered<EucSeqSnapKnob>(mm2px(Vec(50.f, yMaster)), module, EucSeqModuleT<N>::MASTER_SPEED_PARAM));

        // Swing and scale bus - position adapts to panel width
        float rightArea = PANEL_WIDTH_MM - 10.f;
        this->addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(rightArea - 14.f, yMaster)), module, EucSeqModuleT<N>::SWING_PARAM));
        this->addInput(createInputCentered<PJ301MPort>(mm2px(Vec(rightArea, yMaster)), module, EucSeqModuleT<N>::SCALE_BUS_INPUT));

        // CV displays
        float yCvStart = 32.f;
        float yCvHeight = 10.f;
        float yCvSpacing = 11.f;
        for (int i = 0; i < N; i++) {
            auto* cvDisp = createWidget<CVStepDisplayT<N>>(mm2px(Vec(4.f, yCvStart + i * yCvSpacing)));
            cvDisp->module = module;
            cvDisp->channel = i;
            cvDisp->box.size = mm2px(Vec(PANEL_WIDTH_MM - 8.f, yCvHeight));
            this->addChild(cvDisp);
        }

        // Per-channel controls
        float yChannelStart = 32.f + N * yCvSpacing + 4.f;
        // Adjust spacing to fit within panel height (128.5mm)
        float availableHeight = 124.f - yChannelStart;  // leave margin
        float yChannelSpacing = std::min(10.f, availableHeight / N);

        for (int i = 0; i < N; i++) {
            float y = yChannelStart + i * yChannelSpacing;
            float x = 6.f;
            float spacing = (PANEL_WIDTH_MM - 12.f) / 12.f;  // distribute across panel

            this->addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::STEPS_PARAM + i));
            x += spacing;
            this->addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::HITS_PARAM + i));
            x += spacing;
            this->addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::HITS_CV_INPUT + i));
            x += spacing;
            this->addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::QUANT_PARAM + i));
            x += spacing;
            this->addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::PROB_A_PARAM + i));
            x += spacing;
            this->addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::PROB_A_CV_INPUT + i));
            x += spacing;
            this->addParam(createParamCentered<CKSS>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::RETRIG_PARAM + i));
            x += spacing;
            this->addParam(createParamCentered<CKSS>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::BIPOLAR_PARAM + i));
            x += spacing;
            this->addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::SCALE_PARAM + i));
            x += spacing;
            this->addParam(createParamCentered<VCVButton>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::RAND_CV_PARAM + i));
            x += spacing;
            this->addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::GATE_OUTPUT + i));
            this->addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(x, y - 5.f)), module, EucSeqModuleT<N>::GATE_LIGHT + i));
            x += spacing;
            this->addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, EucSeqModuleT<N>::TRIG_OUTPUT + i));
            // CV output at end
            float cvOutX = PANEL_WIDTH_MM - 6.f;
            this->addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(cvOutX, y)), module, EucSeqModuleT<N>::CV_OUTPUT + i));
        }

        // LFO outputs get their own row: the per-channel rows are already full,
        // and these were absent from the panel entirely before now.
        float yLfo = std::min(yChannelStart + N * yChannelSpacing + 8.f, 120.f);
        for (int i = 0; i < N; i++) {
            float lfoX = (N > 1) ? (14.f + i * ((PANEL_WIDTH_MM - 28.f) / (N - 1)))
                                 : (PANEL_WIDTH_MM / 2.f);
            this->addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(lfoX, yLfo)), module, EucSeqModuleT<N>::LFO_OUTPUT + i));
        }
    }
};

} // namespace WiggleRoom
