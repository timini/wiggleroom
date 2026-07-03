/******************************************************************************
 * EUCBANK
 * Pattern storage/recall for EucSeq + LogicMangler expander chain
 *
 * Features:
 *   - 16 bank slots for storing complete chain state
 *   - Save/Load buttons
 *   - Bank select knob (0-15)
 *   - Step trigger input for bank sequencing
 *   - Left expander reads from LogicMangler
 *   - Right expander passes CV values to EucMix
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include "euclogic/ExpanderMessage.hpp"
#include <string>
#include <array>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

// ============================================================================
// Bank slot data
// ============================================================================

struct BankSlot {
    // EucSeq state
    int steps[4] = {16, 16, 16, 16};
    int hits[4] = {8, 8, 8, 8};
    int quant[4] = {0, 0, 0, 0};
    float probA[4] = {1.f, 1.f, 1.f, 1.f};
    bool retrigger[4] = {true, true, true, true};
    bool bipolar[4] = {false, false, false, false};
    float speed = 8.f;  // x1 index
    float swing = 50.f;

    // LogicMangler state
    uint8_t truthTableMapping[16];
    uint8_t truthTableLocks[16];
    float probB[4] = {1.f, 1.f, 1.f, 1.f};
    float colDensity[4] = {0.5f, 0.5f, 0.5f, 0.5f};

    std::string name;
    bool occupied = false;

    BankSlot() {
        for (int i = 0; i < 16; i++) {
            truthTableMapping[i] = static_cast<uint8_t>(i) & 0x0F;
            truthTableLocks[i] = 0;
        }
    }

    json_t* toJson() const {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "occupied", json_boolean(occupied));
        json_object_set_new(rootJ, "name", json_string(name.c_str()));
        json_object_set_new(rootJ, "speed", json_real(speed));
        json_object_set_new(rootJ, "swing", json_real(swing));

        json_t* stepsJ = json_array();
        json_t* hitsJ = json_array();
        json_t* quantJ = json_array();
        json_t* probAJ = json_array();
        json_t* retrigJ = json_array();
        json_t* bipolarJ = json_array();
        json_t* probBJ = json_array();
        json_t* densJ = json_array();
        for (int i = 0; i < 4; i++) {
            json_array_append_new(stepsJ, json_integer(steps[i]));
            json_array_append_new(hitsJ, json_integer(hits[i]));
            json_array_append_new(quantJ, json_integer(quant[i]));
            json_array_append_new(probAJ, json_real(probA[i]));
            json_array_append_new(retrigJ, json_boolean(retrigger[i]));
            json_array_append_new(bipolarJ, json_boolean(bipolar[i]));
            json_array_append_new(probBJ, json_real(probB[i]));
            json_array_append_new(densJ, json_real(colDensity[i]));
        }
        json_object_set_new(rootJ, "steps", stepsJ);
        json_object_set_new(rootJ, "hits", hitsJ);
        json_object_set_new(rootJ, "quant", quantJ);
        json_object_set_new(rootJ, "probA", probAJ);
        json_object_set_new(rootJ, "retrigger", retrigJ);
        json_object_set_new(rootJ, "bipolar", bipolarJ);
        json_object_set_new(rootJ, "probB", probBJ);
        json_object_set_new(rootJ, "colDensity", densJ);

        json_t* ttJ = json_array();
        json_t* lockJ = json_array();
        for (int i = 0; i < 16; i++) {
            json_array_append_new(ttJ, json_integer(truthTableMapping[i]));
            json_array_append_new(lockJ, json_integer(truthTableLocks[i]));
        }
        json_object_set_new(rootJ, "truthTable", ttJ);
        json_object_set_new(rootJ, "lockMask", lockJ);

        return rootJ;
    }

    void fromJson(json_t* rootJ) {
        json_t* occJ = json_object_get(rootJ, "occupied");
        if (occJ) occupied = json_boolean_value(occJ);

        json_t* nameJ = json_object_get(rootJ, "name");
        if (nameJ) name = json_string_value(nameJ);

        json_t* speedJ = json_object_get(rootJ, "speed");
        if (speedJ) speed = json_real_value(speedJ);

        json_t* swingJ = json_object_get(rootJ, "swing");
        if (swingJ) swing = json_real_value(swingJ);

        auto readIntArray = [](json_t* arrJ, int* dest, int count) {
            if (arrJ && json_is_array(arrJ)) {
                for (int i = 0; i < count && i < (int)json_array_size(arrJ); i++)
                    dest[i] = json_integer_value(json_array_get(arrJ, i));
            }
        };
        auto readFloatArray = [](json_t* arrJ, float* dest, int count) {
            if (arrJ && json_is_array(arrJ)) {
                for (int i = 0; i < count && i < (int)json_array_size(arrJ); i++)
                    dest[i] = json_real_value(json_array_get(arrJ, i));
            }
        };
        auto readBoolArray = [](json_t* arrJ, bool* dest, int count) {
            if (arrJ && json_is_array(arrJ)) {
                for (int i = 0; i < count && i < (int)json_array_size(arrJ); i++)
                    dest[i] = json_boolean_value(json_array_get(arrJ, i));
            }
        };

        readIntArray(json_object_get(rootJ, "steps"), steps, 4);
        readIntArray(json_object_get(rootJ, "hits"), hits, 4);
        readIntArray(json_object_get(rootJ, "quant"), quant, 4);
        readFloatArray(json_object_get(rootJ, "probA"), probA, 4);
        readBoolArray(json_object_get(rootJ, "retrigger"), retrigger, 4);
        readBoolArray(json_object_get(rootJ, "bipolar"), bipolar, 4);
        readFloatArray(json_object_get(rootJ, "probB"), probB, 4);
        readFloatArray(json_object_get(rootJ, "colDensity"), colDensity, 4);

        auto readUint8Array = [](json_t* arrJ, uint8_t* dest, int count) {
            if (arrJ && json_is_array(arrJ)) {
                for (int i = 0; i < count && i < (int)json_array_size(arrJ); i++)
                    dest[i] = json_integer_value(json_array_get(arrJ, i));
            }
        };
        readUint8Array(json_object_get(rootJ, "truthTable"), truthTableMapping, 16);
        readUint8Array(json_object_get(rootJ, "lockMask"), truthTableLocks, 16);
    }
};

// ============================================================================
// EucBank Module
// ============================================================================

struct EucBankModule : Module {
    static constexpr int NUM_BANKS = 16;

    enum ParamId {
        BANK_PARAM,
        SAVE_PARAM,
        LOAD_PARAM,
        PARAMS_LEN
    };

    enum InputId {
        STEP_INPUT,
        RESET_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        OUTPUTS_LEN
    };

    enum LightId {
        SAVE_LIGHT,
        LOAD_LIGHT,
        LIGHTS_LEN
    };

    BankSlot banks[NUM_BANKS];
    int currentBank = 0;

    dsp::SchmittTrigger saveTrigger;
    dsp::SchmittTrigger loadTrigger;
    dsp::SchmittTrigger stepTrigger;
    dsp::SchmittTrigger resetTrigger;

    dsp::PulseGenerator savePulse;
    dsp::PulseGenerator loadPulse;

    // Expander messages
    EuclogicExpanderMessage rightMessages[2];

    EucBankModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(BANK_PARAM, 0.f, 15.f, 0.f, "Bank");
        paramQuantities[BANK_PARAM]->snapEnabled = true;
        configButton(SAVE_PARAM, "Save");
        configButton(LOAD_PARAM, "Load");

        configInput(STEP_INPUT, "Step");
        configInput(RESET_INPUT, "Reset");

        configLight(SAVE_LIGHT, "Save");
        configLight(LOAD_LIGHT, "Load");

        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];
    }

    void process(const ProcessArgs& args) override {
        float dt = args.sampleTime;

        // Bank select from knob
        currentBank = static_cast<int>(params[BANK_PARAM].getValue());
        currentBank = DSP::clamp(currentBank, 0, NUM_BANKS - 1);

        // Step trigger
        if (stepTrigger.process(inputs[STEP_INPUT].getVoltage(), 0.1f, 1.0f)) {
            currentBank = (currentBank + 1) % NUM_BANKS;
            params[BANK_PARAM].setValue(currentBank);
        }

        // Reset
        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.0f)) {
            currentBank = 0;
            params[BANK_PARAM].setValue(0.f);
        }

        // Save: capture from left expander
        if (saveTrigger.process(params[SAVE_PARAM].getValue())) {
            if (leftExpander.module) {
                EuclogicExpanderMessage* msg = static_cast<EuclogicExpanderMessage*>(leftExpander.consumerMessage);
                if (msg && msg->valid) {
                    BankSlot& slot = banks[currentBank];
                    for (int i = 0; i < 4; i++) {
                        slot.steps[i] = msg->steps[i];
                        slot.hits[i] = msg->hits[i];
                        slot.quant[i] = msg->quant[i];
                        slot.probA[i] = msg->probA[i];
                        slot.retrigger[i] = msg->retrigger[i];
                        slot.bipolar[i] = msg->bipolar[i];
                        slot.probB[i] = msg->probB[i];
                        slot.colDensity[i] = msg->colDensity[i];
                    }
                    slot.speed = msg->speed;
                    slot.swing = msg->swing;
                    for (int i = 0; i < 16; i++) {
                        slot.truthTableMapping[i] = msg->truthTableMapping[i];
                        slot.truthTableLocks[i] = msg->truthTableLocks[i];
                    }
                    slot.occupied = true;
                    savePulse.trigger(0.1f);
                }
            }
        }

        // Load: currently stored for external use
        if (loadTrigger.process(params[LOAD_PARAM].getValue())) {
            if (banks[currentBank].occupied) {
                loadPulse.trigger(0.1f);
            }
        }

        lights[SAVE_LIGHT].setBrightness(savePulse.process(dt) ? 1.f : 0.f);
        lights[LOAD_LIGHT].setBrightness(loadPulse.process(dt) ? 1.f : 0.f);

        // Forward expander message to the right (pass-through for EucMix)
        if (rightExpander.module) {
            EuclogicExpanderMessage* msg = static_cast<EuclogicExpanderMessage*>(rightExpander.producerMessage);

            if (leftExpander.module) {
                EuclogicExpanderMessage* leftMsg = static_cast<EuclogicExpanderMessage*>(leftExpander.consumerMessage);
                if (leftMsg && leftMsg->valid) {
                    *msg = *leftMsg;
                } else {
                    msg->clear();
                }
            } else {
                msg->clear();
            }
            msg->valid = true;
            rightExpander.requestMessageFlip();
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "currentBank", json_integer(currentBank));

        json_t* banksJ = json_array();
        for (int i = 0; i < NUM_BANKS; i++) {
            json_array_append_new(banksJ, banks[i].toJson());
        }
        json_object_set_new(rootJ, "banks", banksJ);

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* bankJ = json_object_get(rootJ, "currentBank");
        if (bankJ) currentBank = json_integer_value(bankJ);

        json_t* banksJ = json_object_get(rootJ, "banks");
        if (banksJ && json_is_array(banksJ)) {
            for (int i = 0; i < NUM_BANKS && i < (int)json_array_size(banksJ); i++) {
                banks[i].fromJson(json_array_get(banksJ, i));
            }
        }
    }
};

// ============================================================================
// Bank Display
// ============================================================================

struct BankDisplay : LightWidget {
    EucBankModule* module = nullptr;

    BankDisplay() {
        box.size = Vec(80.f, 120.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 180));
        nvgFill(args.vg);

        float cellH = box.size.y / 16.f;

        for (int i = 0; i < 16; i++) {
            float y = i * cellH;
            bool isCurrent = module && (i == module->currentBank);
            bool isOccupied = module && module->banks[i].occupied;

            if (isCurrent) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 0, y, box.size.x, cellH);
                nvgFillColor(args.vg, nvgRGBA(60, 100, 160, 200));
                nvgFill(args.vg);
            }

            // Bank number
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", i + 1);
            nvgFillColor(args.vg, isCurrent ? nvgRGBA(255, 255, 255, 255) : nvgRGBA(120, 140, 180, 200));
            nvgFontSize(args.vg, cellH * 0.7f);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, 4.f, y + cellH / 2.f, buf, nullptr);

            // Occupied indicator
            if (isOccupied) {
                float dotR = cellH * 0.2f;
                nvgBeginPath(args.vg);
                nvgCircle(args.vg, box.size.x - 10.f, y + cellH / 2.f, dotR);
                nvgFillColor(args.vg, nvgRGBA(100, 200, 100, 255));
                nvgFill(args.vg);
            }

            // Bank name
            if (isOccupied && module && !module->banks[i].name.empty()) {
                nvgFillColor(args.vg, nvgRGBA(180, 200, 220, 200));
                nvgFontSize(args.vg, cellH * 0.55f);
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgText(args.vg, 18.f, y + cellH / 2.f, module->banks[i].name.c_str(), nullptr);
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

struct EucBankWidget : ModuleWidget {
    EucBankWidget(EucBankModule* module) {
        setModule(module);
        box.size = Vec(8 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/EucBank.png"), box.size));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float centerX = 20.32f;

        // Bank display
        BankDisplay* bankDisp = createWidget<BankDisplay>(mm2px(Vec(3.f, 16.f)));
        bankDisp->module = module;
        bankDisp->box.size = mm2px(Vec(34.f, 70.f));
        addChild(bankDisp);

        // Bank select knob
        float yControls = 92.f;
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(centerX, yControls)), module, EucBankModule::BANK_PARAM));

        // Save/Load buttons
        float yButtons = 106.f;
        addParam(createParamCentered<VCVButton>(mm2px(Vec(centerX - 8.f, yButtons)), module, EucBankModule::SAVE_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(centerX - 8.f, yButtons - 5)), module, EucBankModule::SAVE_LIGHT));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(centerX + 8.f, yButtons)), module, EucBankModule::LOAD_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(centerX + 8.f, yButtons - 5)), module, EucBankModule::LOAD_LIGHT));

        // Step / Reset inputs
        float yInputs = 122.f;
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(centerX - 8.f, yInputs)), module, EucBankModule::STEP_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(centerX + 8.f, yInputs)), module, EucBankModule::RESET_INPUT));
    }
};

} // namespace WiggleRoom

Model* modelEucBank = createModel<WiggleRoom::EucBankModule, WiggleRoom::EucBankWidget>("EucBank");
