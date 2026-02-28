#pragma once
/******************************************************************************
 * EUCLOGIC CORE
 * Shared template for 2/3/4-channel Euclidean sequencer + truth table modules.
 *
 * Template parameters:
 *   N            - Number of channels (2, 3, or 4)
 *   HAS_PRE_GATE - Whether the module has pre-logic gate outputs
 *
 * The module struct must provide standard VCV Rack enums (ParamId, InputId,
 * OutputId, LightId) matching the expected layout.
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "EuclideanEngine.hpp"
#include "TruthTable.hpp"
#include "ProbabilityGate.hpp"
#include <atomic>
#include <vector>
#include <string>

using namespace rack;

namespace WiggleRoom {

// ============================================================================
// Shared constants and tables
// ============================================================================

namespace EuclogicConstants {
    static constexpr float TRIGGER_PULSE_DURATION = 1e-3f;   // 1ms
    static constexpr float RETRIG_GAP_DURATION = 0.5e-3f;    // 0.5ms gap for retrigger
    static constexpr float SCHMITT_LOW = 0.1f;
    static constexpr float SCHMITT_HIGH = 1.0f;
    static constexpr float DEFAULT_CLOCK_PERIOD = 0.5f;      // 120 BPM

    // Speed ratio values (/16 to x16)
    inline const std::vector<float>& speedRatios() {
        static const std::vector<float> v = {
            1.f/16, 1.f/12, 1.f/8, 1.f/6, 1.f/4, 1.f/3, 1.f/2, 2.f/3,
            1.f,  // x1 (index 8)
            3.f/2, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f
        };
        return v;
    }

    inline const std::vector<std::string>& speedLabels() {
        static const std::vector<std::string> v = {
            "/16", "/12", "/8", "/6", "/4", "/3", "/2", "/1.5",
            "x1",
            "x1.5", "x2", "x3", "x4", "x6", "x8", "x12", "x16"
        };
        return v;
    }

    // Quant ratio values (per-channel clock division)
    inline const std::vector<float>& quantRatios() {
        static const std::vector<float> v = {
            1.f, 1.f/2, 1.f/4, 1.f/8, 1.f/16
        };
        return v;
    }

    inline const std::vector<std::string>& quantLabels() {
        static const std::vector<std::string> v = {
            "x1", "/2", "/4", "/8", "/16"
        };
        return v;
    }
}

// ============================================================================
// Snap knob (shared widget)
// ============================================================================

struct EuclogicSnapKnob : RoundBlackKnob {
    EuclogicSnapKnob() {
        snap = true;
    }
};

// ============================================================================
// HitsParamQuantity - limits hits to current steps value
// ============================================================================

template<typename ModuleT>
struct EuclogicHitsParamQuantity : ParamQuantity {
    int channel = 0;

    float getMaxValue() override {
        ModuleT* m = dynamic_cast<ModuleT*>(module);
        if (m) {
            int steps = static_cast<int>(m->params[ModuleT::STEPS_PARAM + channel].getValue());
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
// EuclogicCore<N, HAS_PRE_GATE> - all shared processing logic
// ============================================================================

template<int N, bool HAS_PRE_GATE>
struct EuclogicCore {
    static constexpr int NUM_CHANNELS = N;

    // Core components
    EuclideanEngine engines[N];
    ProbabilityGate probA[N];
    ProbabilityGate probB[N];
    TruthTableT<N> truthTable;

    // Clock state
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger runTrigger;
    dsp::SchmittTrigger randomTrigger;
    dsp::SchmittTrigger mutateTrigger;
    dsp::SchmittTrigger undoTrigger;
    dsp::SchmittTrigger redoTrigger;

    float clockPeriod = EuclogicConstants::DEFAULT_CLOCK_PERIOD;
    float timeSinceClock = 0.f;
    float internalTickPhase = 0.f;
    bool clockLocked = false;
    bool running = true;
    bool hadFirstTick = false;
    int prevSpeedIdx = 8;  // x1
    bool swingLong = true;

    // Per-channel state
    int quantCounter[N] = {};
    dsp::PulseGenerator trigPulse[N];
    bool prevGateHigh[N] = {};
    float retrigGapTimer[N] = {};

    // Display state (atomic for thread safety between audio + UI)
    std::atomic<uint8_t> currentInputState{0};
    std::atomic<bool> gateStates[N];
    std::atomic<bool> preGateStates[N];

    EuclogicCore() {
        for (int i = 0; i < N; i++) {
            gateStates[i].store(false);
            preGateStates[i].store(false);
        }
    }

    // ---------------------------------------------------------------
    // configureParams — called from module constructor
    // ---------------------------------------------------------------
    template<typename ModuleT>
    void configureParams(ModuleT* module) {
        const auto& sr = EuclogicConstants::speedRatios();
        const auto& sl = EuclogicConstants::speedLabels();
        const auto& qr = EuclogicConstants::quantRatios();
        const auto& ql = EuclogicConstants::quantLabels();

        module->configSwitch(ModuleT::MASTER_SPEED_PARAM, 0.f, sr.size() - 1, 8.f,
            "Master Speed", sl);
        module->configParam(ModuleT::SWING_PARAM, 50.f, 75.f, 50.f, "Swing", "%");

        module->configButton(ModuleT::RANDOM_PARAM, "Random");
        module->configButton(ModuleT::MUTATE_PARAM, "Mutate");
        module->configButton(ModuleT::UNDO_PARAM, "Undo");
        module->configButton(ModuleT::REDO_PARAM, "Redo");

        for (int i = 0; i < N; i++) {
            std::string ch = "Ch " + std::to_string(i + 1);

            module->configParam(ModuleT::STEPS_PARAM + i, 1.f, 64.f, 16.f, ch + " Steps");
            module->paramQuantities[ModuleT::STEPS_PARAM + i]->snapEnabled = true;

            module->configParam(ModuleT::HITS_PARAM + i, 0.f, 64.f, 8.f, ch + " Hits");
            module->paramQuantities[ModuleT::HITS_PARAM + i]->snapEnabled = true;

            // Replace with custom quantity that limits to steps
            auto* hitsQ = new EuclogicHitsParamQuantity<ModuleT>();
            hitsQ->channel = i;
            hitsQ->module = module;
            hitsQ->paramId = ModuleT::HITS_PARAM + i;
            hitsQ->minValue = 0.f;
            hitsQ->maxValue = 64.f;
            hitsQ->defaultValue = 8.f;
            hitsQ->name = ch + " Hits";
            hitsQ->snapEnabled = true;
            delete module->paramQuantities[ModuleT::HITS_PARAM + i];
            module->paramQuantities[ModuleT::HITS_PARAM + i] = hitsQ;

            module->configSwitch(ModuleT::QUANT_PARAM + i, 0.f, qr.size() - 1, 0.f,
                ch + " Quant", ql);

            module->configParam(ModuleT::PROB_A_PARAM + i, 0.f, 1.f, 1.f, ch + " Prob A", "%", 0.f, 100.f);
            module->configParam(ModuleT::PROB_B_PARAM + i, 0.f, 1.f, 1.f, ch + " Prob B", "%", 0.f, 100.f);
            module->configSwitch(ModuleT::RETRIG_PARAM + i, 0.f, 1.f, 1.f, ch + " Retrigger", {"Off", "On"});

            module->configInput(ModuleT::HITS_CV_INPUT + i, ch + " Hits CV");
            module->configInput(ModuleT::PROB_A_CV_INPUT + i, ch + " Prob A CV");
            module->configInput(ModuleT::PROB_B_CV_INPUT + i, ch + " Prob B CV");

            module->configOutput(ModuleT::GATE_OUTPUT + i, ch + " Gate");
            module->configOutput(ModuleT::TRIG_OUTPUT + i, ch + " Trigger");
            module->configOutput(ModuleT::LFO_OUTPUT + i, ch + " LFO");

            if (HAS_PRE_GATE) {
                module->configOutput(ModuleT::PRE_GATE_OUTPUT + i, ch + " Pre-Logic Gate");
            }
        }

        module->configInput(ModuleT::CLOCK_INPUT, "Clock");
        module->configInput(ModuleT::RESET_INPUT, "Reset");
        module->configInput(ModuleT::RUN_INPUT, "Run");

        module->configLight(ModuleT::CLOCK_LIGHT, "Clock Lock");
        module->configLight(ModuleT::RUN_LIGHT, "Running");
    }

    // ---------------------------------------------------------------
    // resetState — called from module onReset()
    // ---------------------------------------------------------------
    void resetState() {
        for (int i = 0; i < N; i++) {
            engines[i].reset();
            quantCounter[i] = 0;
            gateStates[i].store(false);
            preGateStates[i].store(false);
            prevGateHigh[i] = false;
            retrigGapTimer[i] = 0.f;
        }
        timeSinceClock = 0.f;
        internalTickPhase = 0.f;
        clockPeriod = EuclogicConstants::DEFAULT_CLOCK_PERIOD;
        running = true;
        hadFirstTick = false;
        prevSpeedIdx = 8;  // x1
        swingLong = true;
        currentInputState.store(0);
    }

    // ---------------------------------------------------------------
    // process — the main audio-rate processing
    // ---------------------------------------------------------------
    template<typename ModuleT>
    void process(ModuleT* module, float dt) {
        // Handle reset
        if (resetTrigger.process(module->inputs[ModuleT::RESET_INPUT].getVoltage(), EuclogicConstants::SCHMITT_LOW, EuclogicConstants::SCHMITT_HIGH)) {
            resetState();
        }

        // Handle run gate
        if (module->inputs[ModuleT::RUN_INPUT].isConnected()) {
            running = module->inputs[ModuleT::RUN_INPUT].getVoltage() > EuclogicConstants::SCHMITT_HIGH;
        } else {
            running = true;
        }
        module->lights[ModuleT::RUN_LIGHT].setBrightness(running ? 1.f : 0.2f);

        // Handle buttons
        if (randomTrigger.process(module->params[ModuleT::RANDOM_PARAM].getValue())) {
            truthTable.randomize();
        }
        if (mutateTrigger.process(module->params[ModuleT::MUTATE_PARAM].getValue())) {
            truthTable.mutate();
        }
        if (undoTrigger.process(module->params[ModuleT::UNDO_PARAM].getValue())) {
            truthTable.undo();
        }
        if (redoTrigger.process(module->params[ModuleT::REDO_PARAM].getValue())) {
            truthTable.redo();
        }

        // Clock handling - measure period
        timeSinceClock += dt;

        bool clockEdge = false;
        if (clockTrigger.process(module->inputs[ModuleT::CLOCK_INPUT].getVoltage(), EuclogicConstants::SCHMITT_LOW, EuclogicConstants::SCHMITT_HIGH)) {
            if (timeSinceClock > 0.001f) {
                clockPeriod = timeSinceClock;
                clockLocked = true;
            }
            timeSinceClock = 0.f;
            clockEdge = true;
        }

        // Clock timeout
        if (timeSinceClock > 2.f) {
            clockLocked = false;
            hadFirstTick = false;
        }
        module->lights[ModuleT::CLOCK_LIGHT].setBrightness(clockLocked ? 1.f : 0.2f);

        // Get master speed ratio
        const auto& speedRatios = EuclogicConstants::speedRatios();
        int speedIdx = static_cast<int>(module->params[ModuleT::MASTER_SPEED_PARAM].getValue());
        speedIdx = DSP::clamp(speedIdx, 0, (int)speedRatios.size() - 1);
        float masterSpeed = speedRatios[speedIdx];

        // Detect speed change - resync on next clock edge
        if (speedIdx != prevSpeedIdx) {
            hadFirstTick = false;
            internalTickPhase = 0.f;
            prevSpeedIdx = speedIdx;
        }

        // Get swing amount (50% = even, 66% = triplet, 75% = hard swing)
        float swingPercent = module->params[ModuleT::SWING_PARAM].getValue();
        float swingRatio = swingPercent / 100.f;

        // Determine if we should tick
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

        // Process tick
        if (shouldTick) {
            processTickImpl(module);
        }

        // Output voltages
        updateOutputs(module, dt);

        // Update LED matrix
        updateLEDs(module);
    }

private:
    // ---------------------------------------------------------------
    // processTickImpl — per-tick: euclidean engines + truth table + probability
    // ---------------------------------------------------------------
    template<typename ModuleT>
    void processTickImpl(ModuleT* module) {
        const auto& quantRatios = EuclogicConstants::quantRatios();

        bool preLogicStates[N] = {};

        for (int i = 0; i < N; i++) {
            int quantIdx = static_cast<int>(module->params[ModuleT::QUANT_PARAM + i].getValue());
            quantIdx = DSP::clamp(quantIdx, 0, (int)quantRatios.size() - 1);
            float quantRatio = quantRatios[quantIdx];

            int divAmount = static_cast<int>(1.f / quantRatio);
            if (divAmount < 1) divAmount = 1;

            quantCounter[i]++;
            if (quantCounter[i] >= divAmount) {
                quantCounter[i] = 0;

                int steps = static_cast<int>(module->params[ModuleT::STEPS_PARAM + i].getValue());
                float hitsBase = module->params[ModuleT::HITS_PARAM + i].getValue();
                float hitsCV = module->inputs[ModuleT::HITS_CV_INPUT + i].getVoltage() / 5.f * 12.f;
                int hits = static_cast<int>(hitsBase + hitsCV);
                hits = DSP::clamp(hits, 0, steps);

                engines[i].configure(steps, hits, 0);
                bool euclideanHit = engines[i].tick();

                float probABase = module->params[ModuleT::PROB_A_PARAM + i].getValue();
                float probACV = module->inputs[ModuleT::PROB_A_CV_INPUT + i].getVoltage() / 10.f;
                float probAVal = DSP::clamp(probABase + probACV, 0.f, 1.f);

                preLogicStates[i] = euclideanHit && probA[i].process(true, probAVal);
            } else {
                preLogicStates[i] = false;
            }
        }

        // Store pre-logic gate states for output
        for (int i = 0; i < N; i++) {
            preGateStates[i].store(preLogicStates[i]);
        }

        // Build input state for truth table
        uint8_t inputState = 0;
        for (int i = 0; i < N; i++) {
            if (preLogicStates[i]) inputState |= (1 << i);
        }
        currentInputState.store(inputState);

        // Apply truth table
        bool postLogicStates[N];
        truthTable.evaluate(preLogicStates, postLogicStates);

        // Apply Probability B and generate gate/trigger outputs
        for (int i = 0; i < N; i++) {
            float probBBase = module->params[ModuleT::PROB_B_PARAM + i].getValue();
            float probBCV = module->inputs[ModuleT::PROB_B_CV_INPUT + i].getVoltage() / 10.f;
            float probBVal = DSP::clamp(probBBase + probBCV, 0.f, 1.f);

            bool finalOutput = postLogicStates[i] && probB[i].process(true, probBVal);
            bool retrigger = module->params[ModuleT::RETRIG_PARAM + i].getValue() > 0.5f;

            bool shouldTrigger = finalOutput && (!prevGateHigh[i] || retrigger);
            if (shouldTrigger) {
                trigPulse[i].trigger(EuclogicConstants::TRIGGER_PULSE_DURATION);
            }

            if (finalOutput && prevGateHigh[i] && retrigger) {
                retrigGapTimer[i] = EuclogicConstants::RETRIG_GAP_DURATION;
            }

            gateStates[i].store(finalOutput);
            prevGateHigh[i] = finalOutput;
        }
    }

    // ---------------------------------------------------------------
    // updateOutputs — write voltages to output ports
    // ---------------------------------------------------------------
    template<typename ModuleT>
    void updateOutputs(ModuleT* module, float dt) {
        for (int i = 0; i < N; i++) {
            bool gate = gateStates[i].load();

            // Handle retrigger gap
            if (retrigGapTimer[i] > 0.f) {
                retrigGapTimer[i] -= dt;
                module->outputs[ModuleT::GATE_OUTPUT + i].setVoltage(0.f);
            } else {
                module->outputs[ModuleT::GATE_OUTPUT + i].setVoltage(gate ? 10.f : 0.f);
            }

            module->outputs[ModuleT::TRIG_OUTPUT + i].setVoltage(trigPulse[i].process(dt) ? 10.f : 0.f);
            module->lights[ModuleT::GATE_LIGHT + i].setBrightness(gate ? 1.f : 0.f);

            // LFO output: unipolar ramp 0-10V tracking step position
            // Fix for issue #21: use (steps-1) so phase reaches full 1.0
            int steps = engines[i].steps;
            int currentStep = engines[i].currentStep;
            float phase = (steps > 1) ? (float)currentStep / (float)(steps - 1) : 0.f;
            module->outputs[ModuleT::LFO_OUTPUT + i].setVoltage(phase * 10.f);

            // Pre-logic gate output (before truth table)
            if (HAS_PRE_GATE) {
                bool preGate = preGateStates[i].load();
                module->outputs[ModuleT::PRE_GATE_OUTPUT + i].setVoltage(preGate ? 10.f : 0.f);
            }
        }
    }

    // ---------------------------------------------------------------
    // updateLEDs — refresh truth table LED matrix
    // ---------------------------------------------------------------
    template<typename ModuleT>
    void updateLEDs(ModuleT* module) {
        constexpr int N_STATES = (1 << N);
        uint8_t currentState = currentInputState.load();
        for (int state = 0; state < N_STATES; state++) {
            uint8_t outputMask = truthTable.getMapping(state);
            bool isCurrentState = (state == currentState);
            for (int bit = 0; bit < N; bit++) {
                bool isSet = (outputMask >> bit) & 1;
                float brightness = isSet ? (isCurrentState ? 1.f : 0.5f) : (isCurrentState ? 0.2f : 0.05f);
                module->lights[ModuleT::LED_MATRIX_LIGHT + state * N + bit].setBrightness(brightness);
            }
        }
    }

public:
    // ---------------------------------------------------------------
    // JSON serialization
    // ---------------------------------------------------------------
    json_t* dataToJson() const {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "clockPeriod", json_real(clockPeriod));

        json_t* mappingJ = json_array();
        auto mapping = truthTable.serialize();
        for (int i = 0; i < TruthTableT<N>::N_STATES; i++) {
            json_array_append_new(mappingJ, json_integer(mapping[i]));
        }
        json_object_set_new(rootJ, "truthTable", mappingJ);

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) {
        json_t* periodJ = json_object_get(rootJ, "clockPeriod");
        if (periodJ) {
            clockPeriod = json_real_value(periodJ);
        }

        json_t* mappingJ = json_object_get(rootJ, "truthTable");
        if (mappingJ && json_is_array(mappingJ)) {
            std::array<uint8_t, TruthTableT<N>::N_STATES> mapping{};
            for (int i = 0; i < TruthTableT<N>::N_STATES && i < (int)json_array_size(mappingJ); i++) {
                mapping[i] = json_integer_value(json_array_get(mappingJ, i));
            }
            truthTable.deserialize(mapping);
        }
    }
};

} // namespace WiggleRoom
