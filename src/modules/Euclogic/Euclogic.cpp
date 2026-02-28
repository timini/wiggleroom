/******************************************************************************
 * EUCLOGIC
 * 4-channel Euclidean drum sequencer with truth table logic
 *
 * Signal flow:
 *   Clock -> Master Speed -> 4x Euclidean Engines -> Prob A -> Truth Table -> Prob B -> Outputs
 *
 * Features:
 *   - 4 independent Euclidean rhythm generators
 *   - Per-channel: Steps (1-64), Hits (0-Steps), Quant ratio
 *   - Probability A: Pre-logic probability gate (x4)
 *   - Truth Table: 16-state logic engine with LED matrix display
 *   - Probability B: Post-logic probability gate (x4)
 *   - Random/Mutate/Undo buttons for truth table
 *   - 4x Gate + 4x Trigger + 4x LFO + 4x Pre-Gate outputs
 ******************************************************************************/

#include "EuclogicCore.hpp"
#include "ImagePanel.hpp"

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct Euclogic : Module {
    static constexpr int NUM_CHANNELS = 4;

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
        ENUMS(LED_MATRIX_LIGHT, 64),  // 16 input states x 4 output bits
        LIGHTS_LEN
    };

    EuclogicCore<4, true> core;

    Euclogic() {
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

// Euclidean Pattern Display - shows 4 channels of rhythm patterns
struct EuclideanDisplay : LightWidget {
    Euclogic* module = nullptr;

    EuclideanDisplay() {
        box.size = Vec(200.f, 40.f);
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

// Truth Table Display - 16 rows showing all states
struct TruthTableDisplay : LightWidget {
    Euclogic* module = nullptr;

    TruthTableDisplay() {
        box.size = Vec(280.f, 160.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        float rowH = box.size.y / 16.f;
        float inputColW = box.size.x * 0.4f;
        float outputColW = box.size.x * 0.6f;
        float singleInputW = inputColW / 4.f;
        float singleOutputW = outputColW / 4.f;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 160));
        nvgFill(args.vg);

        uint8_t currentState = module ? module->core.currentInputState.load() : 0;

        for (int state = 0; state < 16; state++) {
            float y = state * rowH;
            bool isActive = module && (state == currentState);

            if (isActive) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 0, y, box.size.x, rowH);
                nvgFillColor(args.vg, nvgRGBA(60, 100, 160, 150));
                nvgFill(args.vg);
            }

            for (int bit = 0; bit < 4; bit++) {
                bool bitValue = (state >> (3 - bit)) & 1;
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
            for (int outBit = 0; outBit < 4; outBit++) {
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
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;

        float rowH = box.size.y / 16.f;
        float inputColW = box.size.x * 0.4f;
        float singleOutputW = (box.size.x - inputColW) / 4.f;

        if (e.pos.x < inputColW) return;

        int row = static_cast<int>(e.pos.y / rowH);
        int outBit = static_cast<int>((e.pos.x - inputColW) / singleOutputW);

        if (row >= 0 && row < 16 && outBit >= 0 && outBit < 4) {
            module->core.truthTable.pushUndo();
            module->core.truthTable.toggleBit(row, outBit);
            e.consume(this);
        }
    }
};

struct EuclogicWidget : ModuleWidget {
    EuclogicWidget(Euclogic* module) {
        setModule(module);
        box.size = Vec(40 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/Euclogic.png"), box.size));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float panelWidth = 203.2f;
        float halfWidth = panelWidth / 2.f;

        // ============== TOP LEFT: EUCLIDEAN PATTERN DISPLAY ==============
        float eucDisplayX = 4.f;
        float eucDisplayWidth = halfWidth - 8.f;
        float eucDisplayHeight = 32.f;
        float yEucDisplay = 22.f;

        EuclideanDisplay* eucDisplay = createWidget<EuclideanDisplay>(mm2px(Vec(eucDisplayX, yEucDisplay)));
        eucDisplay->module = module;
        eucDisplay->box.size = mm2px(Vec(eucDisplayWidth, eucDisplayHeight));
        addChild(eucDisplay);

        // ============== BOTTOM LEFT: TRUTH TABLE ==============
        float tableX = 4.f;
        float tableWidth = halfWidth - 8.f;
        float tableHeight = 54.f;
        float yTable = 58.f;

        TruthTableDisplay* truthTable = createWidget<TruthTableDisplay>(mm2px(Vec(tableX, yTable)));
        truthTable->module = module;
        truthTable->box.size = mm2px(Vec(tableWidth, tableHeight));
        addChild(truthTable);

        // ============== TOP RIGHT: CHANNEL KNOBS (compact) ==============
        float yChannelStart = 24.f;
        float yChannelSpacing = 10.f;

        float colBase = halfWidth + 4.f;
        float col1 = colBase + 0.f;
        float col2 = colBase + 11.f;
        float col3 = colBase + 22.f;
        float col4 = colBase + 33.f;
        float col5 = colBase + 44.f;
        float col6 = colBase + 55.f;
        float col7 = colBase + 66.f;
        float col8 = colBase + 77.f;
        float col9 = colBase + 88.f;

        for (int i = 0; i < Euclogic::NUM_CHANNELS; i++) {
            float y = yChannelStart + i * yChannelSpacing;

            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col1, y)), module, Euclogic::STEPS_PARAM + i));
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(col2, y)), module, Euclogic::HITS_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col3, y)), module, Euclogic::HITS_CV_INPUT + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col4, y)), module, Euclogic::QUANT_PARAM + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col5, y)), module, Euclogic::PROB_A_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col6, y)), module, Euclogic::PROB_A_CV_INPUT + i));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(col7, y)), module, Euclogic::PROB_B_PARAM + i));
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col8, y)), module, Euclogic::PROB_B_CV_INPUT + i));
            addParam(createParamCentered<CKSS>(mm2px(Vec(col9, y)), module, Euclogic::RETRIG_PARAM + i));
        }

        // ============== BOTTOM RIGHT: MASTER CONTROLS ==============
        float yMaster = 70.f;
        float yMaster2 = 84.f;
        float yOutputs = 102.f;

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colBase + 6.f, yMaster)), module, Euclogic::CLOCK_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(colBase + 6.f, yMaster - 6)), module, Euclogic::CLOCK_LIGHT));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colBase + 20.f, yMaster)), module, Euclogic::RESET_INPUT));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colBase + 34.f, yMaster)), module, Euclogic::RUN_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(colBase + 34.f, yMaster - 6)), module, Euclogic::RUN_LIGHT));

        addParam(createParamCentered<EuclogicSnapKnob>(mm2px(Vec(colBase + 52.f, yMaster)), module, Euclogic::MASTER_SPEED_PARAM));

        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(colBase + 68.f, yMaster)), module, Euclogic::SWING_PARAM));

        addParam(createParamCentered<VCVButton>(mm2px(Vec(colBase + 8.f, yMaster2)), module, Euclogic::RANDOM_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(colBase + 28.f, yMaster2)), module, Euclogic::MUTATE_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(colBase + 48.f, yMaster2)), module, Euclogic::UNDO_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(colBase + 68.f, yMaster2)), module, Euclogic::REDO_PARAM));

        float yPreOutputs = yOutputs - 12.f;
        for (int i = 0; i < Euclogic::NUM_CHANNELS; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colBase + 50.f + i * 11.f, yPreOutputs)), module, Euclogic::PRE_GATE_OUTPUT + i));
        }

        for (int i = 0; i < Euclogic::NUM_CHANNELS; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colBase + 6.f + i * 11.f, yOutputs)), module, Euclogic::GATE_OUTPUT + i));
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(colBase + 6.f + i * 11.f, yOutputs - 6)), module, Euclogic::GATE_LIGHT + i));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colBase + 50.f + i * 11.f, yOutputs)), module, Euclogic::TRIG_OUTPUT + i));
        }

        float yLFO = yOutputs + 12.f;
        for (int i = 0; i < Euclogic::NUM_CHANNELS; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colBase + 6.f + i * 11.f, yLFO)), module, Euclogic::LFO_OUTPUT + i));
        }
    }
};

} // namespace WiggleRoom

Model* modelEuclogic = createModel<WiggleRoom::Euclogic, WiggleRoom::EuclogicWidget>("Euclogic");
