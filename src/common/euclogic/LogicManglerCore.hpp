#pragma once
/******************************************************************************
 * LOGIC MANGLER CORE
 * N-input truth table logic processor template
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include "euclogic/TruthTable.hpp"
#include "euclogic/ProbabilityGate.hpp"
#include "euclogic/ExpanderMessage.hpp"
#include "euclogic/EuclogicModels.hpp"
#include <atomic>
#include <string>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

// ============================================================================
// LogicMangler Module Template
// ============================================================================

template<int N>
struct LogicManglerModuleT : Module {
    static constexpr int NUM_CHANNELS = N;
    static constexpr int N_STATES = (1 << N);
    static constexpr float TRIGGER_PULSE_DURATION = 1e-3f;
    static constexpr float RETRIG_GAP_DURATION = 0.5e-3f;

    enum ParamId {
        RANDOM_PARAM,
        MUTATE_PARAM,
        UNDO_PARAM,
        REDO_PARAM,
        ENUMS(PROB_B_PARAM, N),
        ENUMS(DENSITY_PARAM, N),
        ENUMS(RETRIG_PARAM, N),
        PARAMS_LEN
    };

    enum InputId {
        ENUMS(GATE_INPUT, N),
        ENUMS(PROB_B_CV_INPUT, N),
        INPUTS_LEN
    };

    enum OutputId {
        ENUMS(GATE_OUTPUT, N),
        ENUMS(TRIG_OUTPUT, N),
        OUTPUTS_LEN
    };

    enum LightId {
        ENUMS(GATE_LIGHT, N),
        ENUMS(LED_MATRIX_LIGHT, N_STATES * N),
        LIGHTS_LEN
    };

    TruthTableT<N> truthTable;
    ProbabilityGate probB[N];

    dsp::SchmittTrigger gateTriggers[N];
    dsp::SchmittTrigger randomTrigger;
    dsp::SchmittTrigger mutateTrigger;
    dsp::SchmittTrigger undoTrigger;
    dsp::SchmittTrigger redoTrigger;

    dsp::PulseGenerator trigPulse[N];
    bool prevGateHigh[N] = {};
    float retrigGapTimer[N] = {};

    std::atomic<uint8_t> currentInputState{0};
    uint8_t prevInputState = 0;
    std::atomic<bool> gateStates[N];
    bool expanderConnected = false;

    EuclogicExpanderMessageT<N> rightMessages[2];

    LogicManglerModuleT() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configButton(RANDOM_PARAM, "Randomize");
        configButton(MUTATE_PARAM, "Mutate");
        configButton(UNDO_PARAM, "Undo");
        configButton(REDO_PARAM, "Redo");

        for (int i = 0; i < N; i++) {
            std::string ch = "Ch " + std::to_string(i + 1);
            configParam(PROB_B_PARAM + i, 0.f, 1.f, 1.f, ch + " Prob B", "%", 0.f, 100.f);
            configParam(DENSITY_PARAM + i, 0.f, 1.f, 0.5f, ch + " Density", "%", 0.f, 100.f);
            configSwitch(RETRIG_PARAM + i, 0.f, 1.f, 0.f, ch + " Retrigger", {"Off", "On"});
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
        truthTable = TruthTableT<N>();
        for (int i = 0; i < N; i++) {
            gateStates[i].store(false);
            prevGateHigh[i] = false;
            retrigGapTimer[i] = 0.f;
        }
        currentInputState.store(0);
        prevInputState = 0;
    }

    void process(const ProcessArgs& args) override {
        using Models = EuclogicModels<N>;
        float dt = args.sampleTime;

        if (randomTrigger.process(params[RANDOM_PARAM].getValue())) {
            float densities[N];
            for (int i = 0; i < N; i++)
                densities[i] = params[DENSITY_PARAM + i].getValue();
            truthTable.randomizeWithDensity(densities);
        }
        if (mutateTrigger.process(params[MUTATE_PARAM].getValue())) {
            float densities[N];
            for (int i = 0; i < N; i++)
                densities[i] = params[DENSITY_PARAM + i].getValue();
            truthTable.mutateWithDensity(densities);
        }
        if (undoTrigger.process(params[UNDO_PARAM].getValue()))
            truthTable.undo();
        if (redoTrigger.process(params[REDO_PARAM].getValue()))
            truthTable.redo();

        bool inputGates[N] = {};
        expanderConnected = false;

        if (leftExpander.module && leftExpander.module->model == Models::eucSeq()) {
            auto* msg = static_cast<EuclogicExpanderMessageT<N>*>(leftExpander.module->rightExpander.consumerMessage);
            if (msg && msg->valid) {
                expanderConnected = true;
                for (int i = 0; i < N; i++)
                    inputGates[i] = msg->gates[i];
            }
        }

        if (!expanderConnected) {
            for (int i = 0; i < N; i++)
                inputGates[i] = inputs[GATE_INPUT + i].getVoltage() > 1.0f;
        }

        uint8_t inputState = 0;
        for (int i = 0; i < N; i++) {
            if (inputGates[i]) inputState |= (1 << i);
        }
        currentInputState.store(inputState);
        bool inputChanged = (inputState != prevInputState);
        prevInputState = inputState;

        bool postLogicStates[N];
        truthTable.evaluate(inputGates, postLogicStates);

        for (int i = 0; i < N; i++) {
            float probBBase = params[PROB_B_PARAM + i].getValue();
            float probBCV = inputs[PROB_B_CV_INPUT + i].getVoltage() / 10.f;
            float probBVal = DSP::clamp(probBBase + probBCV, 0.f, 1.f);

            bool finalOutput = postLogicStates[i] && probB[i].process(true, probBVal);
            bool retrigger = params[RETRIG_PARAM + i].getValue() > 0.5f;

            if (finalOutput && !prevGateHigh[i]) {
                // Rising edge — always trigger
                trigPulse[i].trigger(TRIGGER_PULSE_DURATION);
            } else if (finalOutput && prevGateHigh[i] && retrigger && inputChanged) {
                // Output stayed high across an input state change — retrigger
                trigPulse[i].trigger(TRIGGER_PULSE_DURATION);
                retrigGapTimer[i] = RETRIG_GAP_DURATION;
            }

            gateStates[i].store(finalOutput);
            prevGateHigh[i] = finalOutput;

            // Gate output with retrig gap
            if (retrigGapTimer[i] > 0.f) {
                retrigGapTimer[i] -= dt;
                outputs[GATE_OUTPUT + i].setVoltage(0.f);
            } else {
                outputs[GATE_OUTPUT + i].setVoltage(finalOutput ? 10.f : 0.f);
            }
            outputs[TRIG_OUTPUT + i].setVoltage(trigPulse[i].process(dt) ? 10.f : 0.f);
            lights[GATE_LIGHT + i].setBrightness(finalOutput ? 1.f : 0.f);
        }

        for (int state = 0; state < N_STATES; state++) {
            uint8_t outputMask = truthTable.getMapping(state);
            bool isCurrentState = (state == inputState);
            for (int bit = 0; bit < N; bit++) {
                bool isSet = (outputMask >> bit) & 1;
                float brightness = isSet ? (isCurrentState ? 1.f : 0.5f) : (isCurrentState ? 0.2f : 0.05f);
                lights[LED_MATRIX_LIGHT + state * N + bit].setBrightness(brightness);
            }
        }

        if (rightExpander.module &&
            (rightExpander.module->model == Models::eucMix() ||
             rightExpander.module->model == Models::eucBank())) {
            auto* msg = static_cast<EuclogicExpanderMessageT<N>*>(rightExpander.producerMessage);

            if (expanderConnected && leftExpander.module) {
                auto* leftMsg = static_cast<EuclogicExpanderMessageT<N>*>(leftExpander.module->rightExpander.consumerMessage);
                if (leftMsg && leftMsg->valid) {
                    *msg = *leftMsg;
                }
            } else {
                msg->clear();
            }

            auto mapping = truthTable.serialize();
            auto locks = truthTable.serializeLocks();
            for (int i = 0; i < N_STATES; i++) {
                msg->truthTableMapping[i] = mapping[i];
                msg->truthTableLocks[i] = locks[i];
            }
            for (int i = 0; i < N; i++) {
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
        for (int i = 0; i < N_STATES; i++)
            json_array_append_new(mappingJ, json_integer(mapping[i]));
        json_object_set_new(rootJ, "truthTable", mappingJ);

        json_t* locksJ = json_array();
        auto locks = truthTable.serializeLocks();
        for (int i = 0; i < N_STATES; i++)
            json_array_append_new(locksJ, json_integer(locks[i]));
        json_object_set_new(rootJ, "lockMask", locksJ);
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* mappingJ = json_object_get(rootJ, "truthTable");
        if (mappingJ && json_is_array(mappingJ)) {
            std::array<uint8_t, N_STATES> mapping{};
            for (int i = 0; i < N_STATES && i < (int)json_array_size(mappingJ); i++)
                mapping[i] = json_integer_value(json_array_get(mappingJ, i));
            truthTable.deserialize(mapping);
        }
        json_t* locksJ = json_object_get(rootJ, "lockMask");
        if (locksJ && json_is_array(locksJ)) {
            std::array<uint8_t, N_STATES> locks{};
            for (int i = 0; i < N_STATES && i < (int)json_array_size(locksJ); i++)
                locks[i] = json_integer_value(json_array_get(locksJ, i));
            truthTable.deserializeLocks(locks);
        }
    }
};

// ============================================================================
// Truth Table Display Template
// ============================================================================

template<int N>
struct LogicManglerTruthTableDisplayT : LightWidget {
    static constexpr int N_STATES = (1 << N);
    LogicManglerModuleT<N>* module = nullptr;

    LogicManglerTruthTableDisplayT() { box.size = Vec(200.f, 160.f); }

    static constexpr float HEADER_H = 10.f;

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        float rowH = (box.size.y - HEADER_H) / N_STATES;
        float inputColW = box.size.x * 0.35f;
        float outputColW = box.size.x * 0.65f;
        float singleInputW = inputColW / N;
        float singleOutputW = outputColW / N;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 160));
        nvgFill(args.vg);

        // Column number labels
        nvgFontSize(args.vg, HEADER_H * 0.85f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        for (int i = 0; i < N; i++) {
            char label[2] = {(char)('1' + i), '\0'};
            // Input column labels
            nvgFillColor(args.vg, nvgRGBA(100, 180, 255, 180));
            nvgText(args.vg, i * singleInputW + singleInputW / 2.f, HEADER_H / 2.f, label, nullptr);
            // Output column labels
            nvgFillColor(args.vg, nvgRGBA(60, 140, 200, 180));
            nvgText(args.vg, inputColW + i * singleOutputW + singleOutputW / 2.f, HEADER_H / 2.f, label, nullptr);
        }

        uint8_t currentState = module ? module->currentInputState.load() : 0;

        for (int state = 0; state < N_STATES; state++) {
            float y = HEADER_H + state * rowH;
            bool isActive = module && (state == currentState);

            if (isActive) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 0, y, box.size.x, rowH);
                nvgFillColor(args.vg, nvgRGBA(60, 100, 160, 150));
                nvgFill(args.vg);
            }

            for (int bit = 0; bit < N; bit++) {
                bool bitValue = (state >> bit) & 1;
                float x = bit * singleInputW;
                nvgFillColor(args.vg, bitValue ? nvgRGBA(100, 180, 255, 220) : nvgRGBA(80, 80, 120, 150));
                nvgFontSize(args.vg, rowH * 0.7f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgText(args.vg, x + singleInputW / 2.f, y + rowH / 2.f, bitValue ? "T" : "F", nullptr);
            }

            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, inputColW, y);
            nvgLineTo(args.vg, inputColW, y + rowH);
            nvgStrokeColor(args.vg, nvgRGBA(60, 80, 120, 150));
            nvgStrokeWidth(args.vg, 1.f);
            nvgStroke(args.vg);

            uint8_t outputMask = module ? module->truthTable.getMapping(state) : 0;
            for (int outBit = 0; outBit < N; outBit++) {
                bool isSet = (outputMask >> outBit) & 1;
                bool locked = module ? module->truthTable.isLocked(state, outBit) : false;
                float x = inputColW + outBit * singleOutputW;

                float btnSize = std::min(singleOutputW, rowH) * 0.7f;
                float btnX = x + (singleOutputW - btnSize) / 2.f;
                float btnY = y + (rowH - btnSize) / 2.f;

                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, btnX, btnY, btnSize, btnSize, 2.f);
                if (isSet)
                    nvgFillColor(args.vg, isActive ? nvgRGBA(100, 200, 255, 255) : nvgRGBA(60, 140, 200, 200));
                else
                    nvgFillColor(args.vg, nvgRGBA(30, 35, 50, 120));
                nvgFill(args.vg);

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

            if (state < N_STATES - 1) {
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

        float rowH = (box.size.y - HEADER_H) / N_STATES;
        float inputColW = box.size.x * 0.35f;
        float singleOutputW = (box.size.x - inputColW) / N;

        if (e.pos.x < inputColW) return;
        if (e.pos.y < HEADER_H) return;

        int row = static_cast<int>((e.pos.y - HEADER_H) / rowH);
        int outBit = static_cast<int>((e.pos.x - inputColW) / singleOutputW);

        if (row >= 0 && row < N_STATES && outBit >= 0 && outBit < N) {
            if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
                module->truthTable.pushUndo();
                module->truthTable.toggleBit(row, outBit);
                e.consume(this);
            } else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
                module->truthTable.toggleLock(row, outBit);
                e.consume(this);
            }
        }
    }
};

// ============================================================================
// Widget Template
// ============================================================================

template<int N>
struct LogicManglerWidgetT : ModuleWidget {
    // HP: 2ch=8, 3ch=10, 4ch=14
    static constexpr int HP = (N == 2) ? 8 : (N == 3) ? 10 : 14;
    static constexpr float PANEL_WIDTH_MM = HP * 5.08f;

    LogicManglerWidgetT(LogicManglerModuleT<N>* module, const std::string& panelFile) {
        this->setModule(module);
        this->box.size = Vec(HP * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        this->addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, panelFile), this->box.size));

        this->addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        this->addChild(createWidget<ScrewSilver>(Vec(this->box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        this->addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        this->addChild(createWidget<ScrewSilver>(Vec(this->box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float inputSpacing = (PANEL_WIDTH_MM - 16.f) / std::max(1, N - 1);

        // Gate inputs
        float yInputs = 18.f;
        for (int i = 0; i < N; i++) {
            this->addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.f + i * inputSpacing, yInputs)), module, LogicManglerModuleT<N>::GATE_INPUT + i));
        }

        // Truth table display
        float yTable = 28.f;
        // Truth table height adapts: fewer rows = smaller display
        float tableHeight = (N == 2) ? 20.f : (N == 3) ? 32.f : 48.f;
        auto* tableDisplay = createWidget<LogicManglerTruthTableDisplayT<N>>(mm2px(Vec(3.f, yTable)));
        tableDisplay->module = module;
        tableDisplay->box.size = mm2px(Vec(PANEL_WIDTH_MM - 6.f, tableHeight));
        this->addChild(tableDisplay);

        // Buttons
        float yButtons = yTable + tableHeight + 3.f;
        float btnSpacing = (PANEL_WIDTH_MM - 16.f) / 3.f;
        this->addParam(createParamCentered<VCVButton>(mm2px(Vec(8.f, yButtons)), module, LogicManglerModuleT<N>::RANDOM_PARAM));
        this->addParam(createParamCentered<VCVButton>(mm2px(Vec(8.f + btnSpacing, yButtons)), module, LogicManglerModuleT<N>::MUTATE_PARAM));
        this->addParam(createParamCentered<VCVButton>(mm2px(Vec(8.f + 2 * btnSpacing, yButtons)), module, LogicManglerModuleT<N>::UNDO_PARAM));
        this->addParam(createParamCentered<VCVButton>(mm2px(Vec(8.f + 3 * btnSpacing, yButtons)), module, LogicManglerModuleT<N>::REDO_PARAM));

        // Density knobs
        float yDensity = yButtons + 9.f;
        for (int i = 0; i < N; i++)
            this->addParam(createParamCentered<Trimpot>(mm2px(Vec(8.f + i * inputSpacing, yDensity)), module, LogicManglerModuleT<N>::DENSITY_PARAM + i));

        // Retrigger switches
        float yRetrig = yDensity + 8.f;
        for (int i = 0; i < N; i++)
            this->addParam(createParamCentered<CKSS>(mm2px(Vec(8.f + i * inputSpacing, yRetrig)), module, LogicManglerModuleT<N>::RETRIG_PARAM + i));

        // Prob B knobs + CV
        float yProbB = yRetrig + 8.f;
        for (int i = 0; i < N; i++)
            this->addParam(createParamCentered<Trimpot>(mm2px(Vec(8.f + i * inputSpacing, yProbB)), module, LogicManglerModuleT<N>::PROB_B_PARAM + i));

        float yProbBCV = yProbB + 8.f;
        for (int i = 0; i < N; i++)
            this->addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.f + i * inputSpacing, yProbBCV)), module, LogicManglerModuleT<N>::PROB_B_CV_INPUT + i));

        // Outputs
        float yOutputs = yProbBCV + 9.f;
        for (int i = 0; i < N; i++) {
            this->addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.f + i * inputSpacing, yOutputs)), module, LogicManglerModuleT<N>::GATE_OUTPUT + i));
            this->addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(8.f + i * inputSpacing, yOutputs - 5)), module, LogicManglerModuleT<N>::GATE_LIGHT + i));
        }
        float yTrig = yOutputs + 9.f;
        for (int i = 0; i < N; i++)
            this->addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.f + i * inputSpacing, yTrig)), module, LogicManglerModuleT<N>::TRIG_OUTPUT + i));
    }
};

} // namespace WiggleRoom
