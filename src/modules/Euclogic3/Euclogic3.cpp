/******************************************************************************
 * EUCLOGIC 3
 * 3-channel Euclidean drum sequencer with truth table logic (mid-size version)
 *
 * Signal flow:
 *   Clock -> Master Speed -> 3x Euclidean Engines -> Prob A -> Truth Table -> Prob B -> Outputs
 *
 * Features:
 *   - 3 independent Euclidean rhythm generators
 *   - Per-channel: Steps (1-64), Hits (0-Steps), Quant ratio
 *   - Probability A: Pre-logic probability gate (x3)
 *   - Truth Table: 8-state logic engine with LED matrix display
 *   - Probability B: Post-logic probability gate (x3)
 *   - Random/Mutate/Undo buttons for truth table
 *   - 3x Gate + 3x Trigger + 3x LFO + 3x Pre-Gate outputs
 ******************************************************************************/

#include "EuclogicCore.hpp"
#include "ImagePanel.hpp"

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct Euclogic3 : Module {
    static constexpr int NUM_CHANNELS = 3;

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
        ENUMS(PRE_GATE_OUTPUT, NUM_CHANNELS),  // NEW: Pre-logic gate outputs
        OUTPUTS_LEN
    };

    enum LightId {
        CLOCK_LIGHT,
        RUN_LIGHT,
        ENUMS(GATE_LIGHT, NUM_CHANNELS),
        ENUMS(LED_MATRIX_LIGHT, 24),  // 8 input states x 3 output bits
        LIGHTS_LEN
    };

    EuclogicCore<3, true> core;

    Euclogic3() {
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

// Euclidean Pattern Display - shows 3 channels
struct Euclidean3Display : LightWidget {
    Euclogic3* module = nullptr;

    Euclidean3Display() {
        box.size = Vec(100.f, 36.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 160));
        nvgFill(args.vg);

        float rowHeight = box.size.y / 3.f;

        if (!module) {
            nvgFillColor(args.vg, nvgRGBA(80, 100, 140, 200));
            nvgFontSize(args.vg, 9);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2, "EUCLIDEAN", nullptr);
            return;
        }

        for (int ch = 0; ch < 3; ch++) {
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

// Truth Table Display - 8 rows (3 inputs = 8 states)
struct TruthTable3Display : LightWidget {
    Euclogic3* module = nullptr;

    TruthTable3Display() {
        box.size = Vec(80.f, 64.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        float rowH = box.size.y / 8.f;
        float inputColW = box.size.x * 0.5f;
        float outputColW = box.size.x * 0.5f;
        float singleInputW = inputColW / 3.f;
        float singleOutputW = outputColW / 3.f;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 160));
        nvgFill(args.vg);

        uint8_t currentState = module ? module->core.currentInputState.load() : 0;

        for (int state = 0; state < 8; state++) {
            float y = state * rowH;
            bool isActive = module && (state == currentState);

            if (isActive) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 0, y, box.size.x, rowH);
                nvgFillColor(args.vg, nvgRGBA(60, 100, 160, 150));
                nvgFill(args.vg);
            }

            for (int bit = 0; bit < 3; bit++) {
                bool bitValue = (state >> (2 - bit)) & 1;
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
            for (int outBit = 0; outBit < 3; outBit++) {
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

            if (state < 7) {
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

        float rowH = box.size.y / 8.f;
        float inputColW = box.size.x * 0.5f;
        float singleOutputW = (box.size.x - inputColW) / 3.f;

        if (e.pos.x < inputColW) return;

        int row = static_cast<int>(e.pos.y / rowH);
        int outBit = static_cast<int>((e.pos.x - inputColW) / singleOutputW);

        if (row >= 0 && row < 8 && outBit >= 0 && outBit < 3) {
            module->core.truthTable.pushUndo();
            module->core.truthTable.toggleBit(row, outBit);
            e.consume(this);
        }
    }
};

struct Euclogic3Widget : ModuleWidget {
    Euclogic3Widget(Euclogic3* module) {
        setModule(module);
        box.size = Vec(28 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/Euclogic3.png"), box.size));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float panelWidth = 142.24f;

        float yEucDisplay = 16.f;
        Euclidean3Display* eucDisplay = createWidget<Euclidean3Display>(mm2px(Vec(4.f, yEucDisplay)));
        eucDisplay->module = module;
        eucDisplay->box.size = mm2px(Vec(panelWidth - 8.f, 20.f));
        addChild(eucDisplay);

        float yChannel1 = 40.f;
        float yChannel2 = 52.f;
        float yChannel3 = 64.f;
        float col1 = 10.f;
        float col2 = 24.f;
        float col3 = 38.f;
        float col4 = 52.f;
        float col5 = 66.f;
        float col6 = 80.f;
        float col7 = 94.f;
        float col8 = 108.f;
        float col9 = 126.f;

        for (int i = 0; i < Euclogic3::NUM_CHANNELS; i++) {
            float y;
            if (i == 0) y = yChannel1;
            else if (i == 1) y = yChannel2;
            else y = yChannel3;

            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col1, y)), module, Euclogic3::STEPS_PARAM + i));
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col2, y)), module, Euclogic3::HITS_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col3, y)), module, Euclogic3::HITS_CV_INPUT + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col4, y)), module, Euclogic3::QUANT_PARAM + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col5, y)), module, Euclogic3::PROB_A_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col6, y)), module, Euclogic3::PROB_A_CV_INPUT + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col7, y)), module, Euclogic3::PROB_B_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col8, y)), module, Euclogic3::PROB_B_CV_INPUT + i));
            addParam(createParamCentered<CKSS>(mm2px(Vec(col9, y)), module, Euclogic3::RETRIG_PARAM + i));
        }

        float yClockRow = 76.f;
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col1, yClockRow)), module, Euclogic3::CLOCK_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(col1 + 5.f, yClockRow - 4.f)), module, Euclogic3::CLOCK_LIGHT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col2, yClockRow)), module, Euclogic3::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col3, yClockRow)), module, Euclogic3::RUN_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(col3 + 5.f, yClockRow - 4.f)), module, Euclogic3::RUN_LIGHT));

        addParam(createParamCentered<EuclogicSnapKnob>(mm2px(Vec(col5, yClockRow)), module, Euclogic3::MASTER_SPEED_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col7, yClockRow)), module, Euclogic3::SWING_PARAM));

        float yTable = 76.f;
        TruthTable3Display* truthTableDisp = createWidget<TruthTable3Display>(mm2px(Vec(panelWidth - 50.f, yTable)));
        truthTableDisp->module = module;
        truthTableDisp->box.size = mm2px(Vec(46.f, 32.f));
        addChild(truthTableDisp);

        float yButtons = 88.f;
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col1, yButtons)), module, Euclogic3::RANDOM_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col2, yButtons)), module, Euclogic3::MUTATE_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col3, yButtons)), module, Euclogic3::UNDO_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(col4, yButtons)), module, Euclogic3::REDO_PARAM));

        // Pre-Logic gate outputs (NEW for Euclogic3)
        float yOutputs = 110.f;
        float yPreOutputs = yOutputs - 10.f;
        for (int i = 0; i < Euclogic3::NUM_CHANNELS; i++) {
            float xBase = 14.f + i * 38.f;
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase + 24.f, yPreOutputs)), module, Euclogic3::PRE_GATE_OUTPUT + i));
        }

        for (int i = 0; i < Euclogic3::NUM_CHANNELS; i++) {
            float xBase = 14.f + i * 38.f;
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase, yOutputs)), module, Euclogic3::GATE_OUTPUT + i));
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(xBase, yOutputs - 5)), module, Euclogic3::GATE_LIGHT + i));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase + 12.f, yOutputs)), module, Euclogic3::TRIG_OUTPUT + i));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xBase + 24.f, yOutputs)), module, Euclogic3::LFO_OUTPUT + i));
        }
    }
};

} // namespace WiggleRoom

Model* modelEuclogic3 = createModel<WiggleRoom::Euclogic3, WiggleRoom::Euclogic3Widget>("Euclogic3");
