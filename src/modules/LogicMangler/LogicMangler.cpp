/******************************************************************************
 * LOGIC MANGLER
 * 4-input truth table logic processor
 *
 * Features:
 *   - 4x gate inputs (standalone) OR left expander from EucSeq
 *   - Interactive 16x4 truth table with click-to-toggle
 *   - Right-click to lock cells (excluded from randomize/mutate)
 *   - Per-column density knobs
 *   - Row/column randomize and mutate via header buttons
 *   - 4x Probability B knobs + CV inputs
 *   - 4x Gate + 4x Trigger outputs
 *   - Right expander sends state to EucBank
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include "euclogic/TruthTable.hpp"
#include "euclogic/ProbabilityGate.hpp"
#include "euclogic/ExpanderMessage.hpp"
#include <atomic>
#include <string>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

// ============================================================================
// LogicMangler Module
// ============================================================================

struct LogicManglerModule : Module {
    static constexpr int NUM_CHANNELS = 4;
    static constexpr float TRIGGER_PULSE_DURATION = 1e-3f;
    static constexpr float RETRIG_GAP_DURATION = 0.5e-3f;

    enum ParamId {
        RANDOM_PARAM,
        MUTATE_PARAM,
        UNDO_PARAM,
        REDO_PARAM,
        ENUMS(PROB_B_PARAM, NUM_CHANNELS),
        ENUMS(DENSITY_PARAM, NUM_CHANNELS),
        PARAMS_LEN
    };

    enum InputId {
        ENUMS(GATE_INPUT, NUM_CHANNELS),
        ENUMS(PROB_B_CV_INPUT, NUM_CHANNELS),
        INPUTS_LEN
    };

    enum OutputId {
        ENUMS(GATE_OUTPUT, NUM_CHANNELS),
        ENUMS(TRIG_OUTPUT, NUM_CHANNELS),
        OUTPUTS_LEN
    };

    enum LightId {
        ENUMS(GATE_LIGHT, NUM_CHANNELS),
        ENUMS(LED_MATRIX_LIGHT, 64),  // 16 states x 4 output bits
        LIGHTS_LEN
    };

    TruthTable truthTable;
    ProbabilityGate probB[NUM_CHANNELS];

    dsp::SchmittTrigger gateTriggers[NUM_CHANNELS];
    dsp::SchmittTrigger randomTrigger;
    dsp::SchmittTrigger mutateTrigger;
    dsp::SchmittTrigger undoTrigger;
    dsp::SchmittTrigger redoTrigger;

    dsp::PulseGenerator trigPulse[NUM_CHANNELS];
    bool prevGateHigh[NUM_CHANNELS] = {};

    std::atomic<uint8_t> currentInputState{0};
    std::atomic<bool> gateStates[NUM_CHANNELS];
    bool expanderConnected = false;

    // Expander messages
    EuclogicExpanderMessage rightMessages[2];

    LogicManglerModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configButton(RANDOM_PARAM, "Randomize");
        configButton(MUTATE_PARAM, "Mutate");
        configButton(UNDO_PARAM, "Undo");
        configButton(REDO_PARAM, "Redo");

        for (int i = 0; i < NUM_CHANNELS; i++) {
            std::string ch = "Ch " + std::to_string(i + 1);
            configParam(PROB_B_PARAM + i, 0.f, 1.f, 1.f, ch + " Prob B", "%", 0.f, 100.f);
            configParam(DENSITY_PARAM + i, 0.f, 1.f, 0.5f, ch + " Density", "%", 0.f, 100.f);
            configInput(GATE_INPUT + i, ch + " Gate");
            configInput(PROB_B_CV_INPUT + i, ch + " Prob B CV");
            configOutput(GATE_OUTPUT + i, ch + " Gate");
            configOutput(TRIG_OUTPUT + i, ch + " Trigger");
            gateStates[i].store(false);
        }

        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];
    }

    void onReset() override {
        truthTable = TruthTable();
        for (int i = 0; i < NUM_CHANNELS; i++) {
            gateStates[i].store(false);
            prevGateHigh[i] = false;
        }
        currentInputState.store(0);
    }

    void process(const ProcessArgs& args) override {
        float dt = args.sampleTime;

        // Handle buttons
        if (randomTrigger.process(params[RANDOM_PARAM].getValue())) {
            truthTable.randomize();
        }
        if (mutateTrigger.process(params[MUTATE_PARAM].getValue())) {
            truthTable.mutate();
        }
        if (undoTrigger.process(params[UNDO_PARAM].getValue())) {
            truthTable.undo();
        }
        if (redoTrigger.process(params[REDO_PARAM].getValue())) {
            truthTable.redo();
        }

        // Get input gates - from expander or direct inputs
        bool inputGates[NUM_CHANNELS] = {};
        expanderConnected = false;

        // Check left expander for EucSeq message
        if (leftExpander.module) {
            EuclogicExpanderMessage* msg = static_cast<EuclogicExpanderMessage*>(leftExpander.consumerMessage);
            if (msg && msg->valid) {
                expanderConnected = true;
                for (int i = 0; i < NUM_CHANNELS; i++) {
                    inputGates[i] = msg->gates[i];
                }
            }
        }

        // Fallback to direct gate inputs
        if (!expanderConnected) {
            for (int i = 0; i < NUM_CHANNELS; i++) {
                inputGates[i] = inputs[GATE_INPUT + i].getVoltage() > 1.0f;
            }
        }

        // Build input state for truth table
        uint8_t inputState = 0;
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (inputGates[i]) inputState |= (1 << i);
        }
        currentInputState.store(inputState);

        // Apply truth table
        bool postLogicStates[NUM_CHANNELS];
        truthTable.evaluate(inputGates, postLogicStates);

        // Apply Probability B and generate outputs
        for (int i = 0; i < NUM_CHANNELS; i++) {
            float probBBase = params[PROB_B_PARAM + i].getValue();
            float probBCV = inputs[PROB_B_CV_INPUT + i].getVoltage() / 10.f;
            float probBVal = DSP::clamp(probBBase + probBCV, 0.f, 1.f);

            bool finalOutput = postLogicStates[i] && probB[i].process(true, probBVal);

            if (finalOutput && !prevGateHigh[i]) {
                trigPulse[i].trigger(TRIGGER_PULSE_DURATION);
            }

            gateStates[i].store(finalOutput);
            prevGateHigh[i] = finalOutput;

            outputs[GATE_OUTPUT + i].setVoltage(finalOutput ? 10.f : 0.f);
            outputs[TRIG_OUTPUT + i].setVoltage(trigPulse[i].process(dt) ? 10.f : 0.f);
            lights[GATE_LIGHT + i].setBrightness(finalOutput ? 1.f : 0.f);
        }

        // Update LED matrix
        for (int state = 0; state < 16; state++) {
            uint8_t outputMask = truthTable.getMapping(state);
            bool isCurrentState = (state == inputState);
            for (int bit = 0; bit < NUM_CHANNELS; bit++) {
                bool isSet = (outputMask >> bit) & 1;
                float brightness = isSet ? (isCurrentState ? 1.f : 0.5f) : (isCurrentState ? 0.2f : 0.05f);
                lights[LED_MATRIX_LIGHT + state * NUM_CHANNELS + bit].setBrightness(brightness);
            }
        }

        // Send expander message to the right
        if (rightExpander.module) {
            EuclogicExpanderMessage* msg = static_cast<EuclogicExpanderMessage*>(rightExpander.producerMessage);

            // Forward EucSeq data if available
            if (expanderConnected && leftExpander.module) {
                EuclogicExpanderMessage* leftMsg = static_cast<EuclogicExpanderMessage*>(leftExpander.consumerMessage);
                if (leftMsg && leftMsg->valid) {
                    *msg = *leftMsg;
                }
            } else {
                msg->clear();
            }

            // Add our truth table state
            auto mapping = truthTable.serialize();
            auto locks = truthTable.serializeLocks();
            for (int i = 0; i < 16; i++) {
                msg->truthTableMapping[i] = mapping[i];
                msg->truthTableLocks[i] = locks[i];
            }
            for (int i = 0; i < NUM_CHANNELS; i++) {
                msg->postLogicGates[i] = gateStates[i].load();
                msg->probB[i] = params[PROB_B_PARAM + i].getValue();
                msg->colDensity[i] = params[DENSITY_PARAM + i].getValue();
            }
            msg->valid = true;

            rightExpander.requestMessageFlip();
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();

        json_t* mappingJ = json_array();
        auto mapping = truthTable.serialize();
        for (int i = 0; i < 16; i++) {
            json_array_append_new(mappingJ, json_integer(mapping[i]));
        }
        json_object_set_new(rootJ, "truthTable", mappingJ);

        json_t* locksJ = json_array();
        auto locks = truthTable.serializeLocks();
        for (int i = 0; i < 16; i++) {
            json_array_append_new(locksJ, json_integer(locks[i]));
        }
        json_object_set_new(rootJ, "lockMask", locksJ);

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* mappingJ = json_object_get(rootJ, "truthTable");
        if (mappingJ && json_is_array(mappingJ)) {
            std::array<uint8_t, 16> mapping{};
            for (int i = 0; i < 16 && i < (int)json_array_size(mappingJ); i++) {
                mapping[i] = json_integer_value(json_array_get(mappingJ, i));
            }
            truthTable.deserialize(mapping);
        }

        json_t* locksJ = json_object_get(rootJ, "lockMask");
        if (locksJ && json_is_array(locksJ)) {
            std::array<uint8_t, 16> locks{};
            for (int i = 0; i < 16 && i < (int)json_array_size(locksJ); i++) {
                locks[i] = json_integer_value(json_array_get(locksJ, i));
            }
            truthTable.deserializeLocks(locks);
        }
    }
};

// ============================================================================
// Truth Table Display with lock support
// ============================================================================

struct LogicManglerTruthTableDisplay : LightWidget {
    LogicManglerModule* module = nullptr;

    LogicManglerTruthTableDisplay() {
        box.size = Vec(200.f, 160.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        float rowH = box.size.y / 16.f;
        float inputColW = box.size.x * 0.35f;
        float outputColW = box.size.x * 0.65f;
        float singleInputW = inputColW / 4.f;
        float singleOutputW = outputColW / 4.f;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 160));
        nvgFill(args.vg);

        uint8_t currentState = module ? module->currentInputState.load() : 0;

        for (int state = 0; state < 16; state++) {
            float y = state * rowH;
            bool isActive = module && (state == currentState);

            if (isActive) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 0, y, box.size.x, rowH);
                nvgFillColor(args.vg, nvgRGBA(60, 100, 160, 150));
                nvgFill(args.vg);
            }

            // Input state columns
            for (int bit = 0; bit < 4; bit++) {
                bool bitValue = (state >> (3 - bit)) & 1;
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

            // Output columns with lock indicators
            uint8_t outputMask = module ? module->truthTable.getMapping(state) : 0;
            for (int outBit = 0; outBit < 4; outBit++) {
                bool isSet = (outputMask >> outBit) & 1;
                bool locked = module ? module->truthTable.isLocked(state, outBit) : false;
                float x = inputColW + outBit * singleOutputW;

                float btnSize = std::min(singleOutputW, rowH) * 0.7f;
                float btnX = x + (singleOutputW - btnSize) / 2.f;
                float btnY = y + (rowH - btnSize) / 2.f;

                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, btnX, btnY, btnSize, btnSize, 2.f);

                if (isSet) {
                    nvgFillColor(args.vg, isActive ? nvgRGBA(100, 200, 255, 255) : nvgRGBA(60, 140, 200, 200));
                } else {
                    nvgFillColor(args.vg, nvgRGBA(30, 35, 50, 120));
                }
                nvgFill(args.vg);

                // Lock indicator: thicker border
                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, btnX, btnY, btnSize, btnSize, 2.f);
                if (locked) {
                    nvgStrokeColor(args.vg, nvgRGBA(255, 180, 50, 220));
                    nvgStrokeWidth(args.vg, 1.5f);
                } else {
                    nvgStrokeColor(args.vg, isSet ? nvgRGBA(120, 180, 255, 180) : nvgRGBA(50, 60, 80, 100));
                    nvgStrokeWidth(args.vg, 0.5f);
                }
                nvgStroke(args.vg);
            }

            if (state < 15) {
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

        float rowH = box.size.y / 16.f;
        float inputColW = box.size.x * 0.35f;
        float singleOutputW = (box.size.x - inputColW) / 4.f;

        if (e.pos.x < inputColW) return;

        int row = static_cast<int>(e.pos.y / rowH);
        int outBit = static_cast<int>((e.pos.x - inputColW) / singleOutputW);

        if (row >= 0 && row < 16 && outBit >= 0 && outBit < 4) {
            if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
                // Left click: toggle bit
                module->truthTable.pushUndo();
                module->truthTable.toggleBit(row, outBit);
                e.consume(this);
            } else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
                // Right click: toggle lock
                module->truthTable.toggleLock(row, outBit);
                e.consume(this);
            }
        }
    }
};

// ============================================================================
// Widget
// ============================================================================

struct LogicManglerWidget : ModuleWidget {
    LogicManglerWidget(LogicManglerModule* module) {
        setModule(module);
        box.size = Vec(14 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/LogicMangler.png"), box.size));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float panelWidth = 71.12f;

        // Gate inputs
        float yInputs = 20.f;
        float inputSpacing = 14.f;
        for (int i = 0; i < 4; i++) {
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.f + i * inputSpacing, yInputs)), module, LogicManglerModule::GATE_INPUT + i));
        }

        // Truth table display
        float yTable = 32.f;
        LogicManglerTruthTableDisplay* tableDisplay = createWidget<LogicManglerTruthTableDisplay>(mm2px(Vec(3.f, yTable)));
        tableDisplay->module = module;
        tableDisplay->box.size = mm2px(Vec(panelWidth - 6.f, 54.f));
        addChild(tableDisplay);

        // Buttons
        float yButtons = 90.f;
        addParam(createParamCentered<VCVButton>(mm2px(Vec(10.f, yButtons)), module, LogicManglerModule::RANDOM_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(24.f, yButtons)), module, LogicManglerModule::MUTATE_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(48.f, yButtons)), module, LogicManglerModule::UNDO_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(62.f, yButtons)), module, LogicManglerModule::REDO_PARAM));

        // Density knobs
        float yDensity = 102.f;
        for (int i = 0; i < 4; i++) {
            addParam(createParamCentered<Trimpot>(mm2px(Vec(8.f + i * inputSpacing, yDensity)), module, LogicManglerModule::DENSITY_PARAM + i));
        }

        // Prob B knobs + CV
        float yProbB = 114.f;
        for (int i = 0; i < 4; i++) {
            addParam(createParamCentered<Trimpot>(mm2px(Vec(8.f + i * inputSpacing, yProbB)), module, LogicManglerModule::PROB_B_PARAM + i));
        }
        float yProbBCV = 124.f;
        for (int i = 0; i < 4; i++) {
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.f + i * inputSpacing, yProbBCV)), module, LogicManglerModule::PROB_B_CV_INPUT + i));
        }

        // Outputs
        float yOutputs = 136.f;
        for (int i = 0; i < 4; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.f + i * inputSpacing, yOutputs)), module, LogicManglerModule::GATE_OUTPUT + i));
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(8.f + i * inputSpacing, yOutputs - 5)), module, LogicManglerModule::GATE_LIGHT + i));
        }
        float yTrig = 148.f;
        for (int i = 0; i < 4; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.f + i * inputSpacing, yTrig)), module, LogicManglerModule::TRIG_OUTPUT + i));
        }
    }
};

} // namespace WiggleRoom

Model* modelLogicMangler = createModel<WiggleRoom::LogicManglerModule, WiggleRoom::LogicManglerWidget>("LogicMangler");
