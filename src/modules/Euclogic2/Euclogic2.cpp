/******************************************************************************
 * EUCLOGIC 2
 * 2-channel Euclidean drum sequencer with truth table logic (compact version)
 *
 * Signal flow:
 *   Clock -> Master Speed -> 2x Euclidean Engines -> Prob A -> Truth Table -> Prob B -> Outputs
 *
 * Features:
 *   - 2 independent Euclidean rhythm generators
 *   - Per-channel: Steps (1-64), Hits (0-Steps), Quant ratio
 *   - Probability A: Pre-logic probability gate (x2)
 *   - Truth Table: 4-state logic engine with LED matrix display
 *   - Probability B: Post-logic probability gate (x2)
 *   - Random/Mutate/Undo buttons for truth table
 *   - 2x Gate + 2x Trigger + 2x LFO + 2x Pre-Gate outputs
 ******************************************************************************/

#include "EuclogicCore.hpp"
#include "ImagePanel.hpp"

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct Euclogic2 : Module {
    static constexpr int NUM_CHANNELS = 2;

    enum ParamId {
        MASTER_SPEED_PARAM,
        SWING_PARAM,
        RANDOM_PARAM,
        MUTATE_PARAM,
        UNDO_PARAM,
        REDO_PARAM,
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
        ENUMS(PRE_GATE_OUTPUT, NUM_CHANNELS),
        OUTPUTS_LEN
    };

    enum LightId {
        CLOCK_LIGHT,
        RUN_LIGHT,
        ENUMS(GATE_LIGHT, NUM_CHANNELS),
        ENUMS(LED_MATRIX_LIGHT, 8),  // 4 input states x 2 output bits
        LIGHTS_LEN
    };

    EuclogicCore<2, true> core;

    Euclogic2() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        core.configureParams(this);
    }

    void onReset() override {
        core.resetState();
    }

    void process(const ProcessArgs& args) override {
        core.process(this, args.sampleTime);
    }

    json_t* dataToJson() override {
        return core.dataToJson();
    }

    void dataFromJson(json_t* rootJ) override {
        core.dataFromJson(rootJ);
    }
};

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
            const auto& engine = module->core.engines[ch];
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

        uint8_t currentState = module ? module->core.currentInputState.load() : 0;

        for (int state = 0; state < 4; state++) {
            float y = state * rowH;
            bool isActive = module && (state == currentState);

            if (isActive) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 0, y, box.size.x, rowH);
                nvgFillColor(args.vg, nvgRGBA(60, 100, 160, 150));
                nvgFill(args.vg);
            }

            for (int bit = 0; bit < 2; bit++) {
                bool bitValue = (state >> (1 - bit)) & 1;
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

            uint8_t outputMask = module ? module->core.truthTable.getMapping(state) : 0;
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
            module->core.truthTable.pushUndo();
            module->core.truthTable.toggleBit(row, outBit);
            e.consume(this);
        }
    }
};

struct Euclogic2Widget : ModuleWidget {
    Euclogic2Widget(Euclogic2* module) {
        setModule(module);
        box.size = Vec(20 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/Euclogic2.png"), box.size));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float panelWidth = 101.6f;

        float yEucDisplay = 16.f;
        Euclidean2Display* eucDisplay = createWidget<Euclidean2Display>(mm2px(Vec(4.f, yEucDisplay)));
        eucDisplay->module = module;
        eucDisplay->box.size = mm2px(Vec(panelWidth - 8.f, 16.f));
        addChild(eucDisplay);

        float yChannel1 = 36.f;
        float yChannel2 = 48.f;
        float col1 = 8.f;
        float col2 = 20.f;
        float col3 = 32.f;
        float col4 = 44.f;
        float col5 = 56.f;
        float col6 = 68.f;
        float col7 = 80.f;
        float col8 = 92.f;

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

        float yRetrig = 58.f;
        addParam(createParamCentered<CKSS>(mm2px(Vec(col1, yRetrig)), module, Euclogic2::RETRIG_PARAM + 0));
        addParam(createParamCentered<CKSS>(mm2px(Vec(col2, yRetrig)), module, Euclogic2::RETRIG_PARAM + 1));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col5, yRetrig)), module, Euclogic2::CLOCK_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(col5 + 5.f, yRetrig - 4.f)), module, Euclogic2::CLOCK_LIGHT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col6, yRetrig)), module, Euclogic2::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col7, yRetrig)), module, Euclogic2::RUN_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(col7 + 5.f, yRetrig - 4.f)), module, Euclogic2::RUN_LIGHT));

        float yTable = 68.f;
        TruthTable2Display* truthTable = createWidget<TruthTable2Display>(mm2px(Vec(panelWidth - 54.f, yTable)));
        truthTable->module = module;
        truthTable->box.size = mm2px(Vec(50.f, 26.f));
        addChild(truthTable);

        float ySpeedRow = 78.f;
        addParam(createParamCentered<EuclogicSnapKnob>(mm2px(Vec(col1 + 6.f, ySpeedRow)), module, Euclogic2::MASTER_SPEED_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col3, ySpeedRow)), module, Euclogic2::SWING_PARAM));

        float yButtons = 94.f;
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col1, yButtons)), module, Euclogic2::RANDOM_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col2, yButtons)), module, Euclogic2::MUTATE_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col3, yButtons)), module, Euclogic2::UNDO_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col4, yButtons)), module, Euclogic2::REDO_PARAM));

        float yOutputs = 110.f;
        float yPreOutputs = yOutputs - 10.f;
        for (int i = 0; i < Euclogic2::NUM_CHANNELS; i++) {
            float xBase = 12.f + i * 40.f;
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase + 36.f, yPreOutputs)), module, Euclogic2::PRE_GATE_OUTPUT + i));
        }

        for (int i = 0; i < Euclogic2::NUM_CHANNELS; i++) {
            float xBase = 12.f + i * 40.f;
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase, yOutputs)), module, Euclogic2::GATE_OUTPUT + i));
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(xBase, yOutputs - 5)), module, Euclogic2::GATE_LIGHT + i));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase + 12.f, yOutputs)), module, Euclogic2::TRIG_OUTPUT + i));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase + 24.f, yOutputs)), module, Euclogic2::LFO_OUTPUT + i));
        }
    }
};

} // namespace WiggleRoom

Model* modelEuclogic2 = createModel<WiggleRoom::Euclogic2, WiggleRoom::Euclogic2Widget>("Euclogic2");
