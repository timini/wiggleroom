/******************************************************************************
 * MUTAGEN
 * CV-addressable step sequencer whose values arrive by chance, not by knob.
 *
 * There are no per-step knobs. Values come from Randomise, from the
 * mutation rate drifting one step at a time, and from drawing on the
 * screen. Steps run top to bottom so the module fits in 6HP.
 *
 * Transport comes from its own jacks, or from an Intersect placed directly
 * to its left, in which case Intersect's trigger output is the clock.
 * MutagenX expanders stacked to the right become extra lanes.
 *
 * Signal flow:
 *   Clock -> address (CV or counter) -> mutate -> value -> CV out
 ******************************************************************************/

#include "rack.hpp"

#include "DSP.hpp"
#include "mutagen/MutagenDisplay.hpp"
#include "mutagen/MutagenLane.hpp"
#include "mutagen/MutagenModels.hpp"
#include "mutagen/PanelLabel.hpp"

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

namespace MutagenConstants {
    constexpr float SCHMITT_LOW = 0.1f;
    constexpr float SCHMITT_HIGH = 1.0f;
    constexpr float PULSE_DURATION = 0.1f;
}

struct Mutagen : MutagenLane {
    enum ParamId {
        STEPS_PARAM,
        MUTATE_RATE_PARAM,
        BANK_PARAM,
        SAVE_PARAM,
        RANDOM_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RUN_INPUT,
        RESET_INPUT,
        ADDRESS_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        CV_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        CLOCK_LIGHT,
        SAVE_LIGHT,
        LIGHTS_LEN
    };

    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger saveTrigger;
    dsp::SchmittTrigger randomTrigger;
    dsp::PulseGenerator savePulse;
    dsp::PulseGenerator clockPulse;

    MutagenExpanderMessage rightMessages[2];

    Mutagen() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(STEPS_PARAM, (float)StepSequence::MIN_STEPS,
                    (float)StepSequence::MAX_STEPS,
                    (float)StepSequence::DEFAULT_STEPS, "Steps");
        paramQuantities[STEPS_PARAM]->snapEnabled = true;

        configParam(MUTATE_RATE_PARAM, 0.f, 1.f, 0.f, "Mutation rate", "%", 0.f, 100.f);

        configParam(BANK_PARAM, 0.f, (float)(BankStore::NUM_BANKS - 1), 0.f, "Bank");
        paramQuantities[BANK_PARAM]->snapEnabled = true;

        configButton(SAVE_PARAM, "Save to bank");
        configButton(RANDOM_PARAM, "Randomise all steps");

        configInput(CLOCK_INPUT, "Clock");
        configInput(RUN_INPUT, "Run");
        configInput(RESET_INPUT, "Reset");
        configInput(ADDRESS_CV_INPUT, "Address CV");
        configOutput(CV_OUTPUT, "CV");

        configLight(CLOCK_LIGHT, "Clock");
        configLight(SAVE_LIGHT, "Saved");

        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];
    }

    void onReset() override {
        seq = StepSequence();
        addresser.reset();
        currentBank = 0;
    }

    // Transport from an Intersect on the left, if one is there.
    bool readUpstream(bool& clockTick, bool& resetPulse, bool& running) {
        if (!leftExpander.module || leftExpander.module->model != modelIntersect)
            return false;
        auto* msg = static_cast<MutagenExpanderMessage*>(
            leftExpander.module->rightExpander.consumerMessage);
        if (!msg || !msg->valid) return false;
        clockTick = msg->clockTick;
        resetPulse = msg->resetPulse;
        running = msg->running;
        return true;
    }

    void process(const ProcessArgs& args) override {
        seq.setSteps(static_cast<int>(params[STEPS_PARAM].getValue()));

        bool clockTick = false;
        bool resetPulse = false;
        bool running = true;

        if (!readUpstream(clockTick, resetPulse, running)) {
            // Fall back to our own jacks, as every expander consumer here does.
            clockTick = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(),
                                             MutagenConstants::SCHMITT_LOW,
                                             MutagenConstants::SCHMITT_HIGH);
            resetPulse = resetTrigger.process(inputs[RESET_INPUT].getVoltage(),
                                              MutagenConstants::SCHMITT_LOW,
                                              MutagenConstants::SCHMITT_HIGH);
            running = inputs[RUN_INPUT].isConnected()
                        ? inputs[RUN_INPUT].getVoltage() > MutagenConstants::SCHMITT_HIGH
                        : true;
        }

        if (resetPulse) addresser.reset();

        // Bank changes recall this lane's own slot.
        applyBankIndex(static_cast<int>(params[BANK_PARAM].getValue()));

        bool saveRequest = saveTrigger.process(params[SAVE_PARAM].getValue());
        if (saveRequest) {
            banks.save(currentBank, seq);
            savePulse.trigger(MutagenConstants::PULSE_DURATION);
        }

        if (randomTrigger.process(params[RANDOM_PARAM].getValue()))
            seq.randomize();

        if (clockTick && running) {
            bool addressed = inputs[ADDRESS_CV_INPUT].isConnected();
            // The address is latched here, so a smooth CV still steps in time.
            float cv01 = DSP::clamp(inputs[ADDRESS_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
            addresser.tick(seq.steps, addressed, cv01);

            seq.mutateOnce(params[MUTATE_RATE_PARAM].getValue());
            clockPulse.trigger(0.05f);
        }

        outputs[CV_OUTPUT].setVoltage(outputVoltage());

        lights[CLOCK_LIGHT].setBrightness(clockPulse.process(args.sampleTime) ? 1.f : 0.f);
        lights[SAVE_LIGHT].setBrightness(savePulse.process(args.sampleTime) ? 1.f : 0.f);

        sendDownstream(clockTick && running, resetPulse, running, saveRequest);
    }

    void sendDownstream(bool clockTick, bool resetPulse, bool running, bool saveRequest) {
        if (!rightExpander.module || rightExpander.module->model != modelMutagenX)
            return;

        auto* msg = static_cast<MutagenExpanderMessage*>(rightExpander.producerMessage);
        msg->clear();
        msg->clockTick = clockTick;
        msg->resetPulse = resetPulse;
        msg->running = running;
        // The resolved address, so every lane reads the same position.
        msg->stepAddress = addresser.hasPlayed() ? addresser.current(seq.steps) : -1;
        msg->bankIndex = currentBank;
        msg->saveRequest = saveRequest;
        msg->valid = true;
        rightExpander.requestMessageFlip();
    }

    json_t* dataToJson() override { return laneToJson(); }
    void dataFromJson(json_t* rootJ) override { laneFromJson(rootJ); }
};

struct MutagenWidget : ModuleWidget {
    MutagenWidget(Mutagen* module) {
        setModule(module);
        box.size = Vec(6 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Mutagen.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Vertical step editor: steps top to bottom, value left to right.
        auto* display = createWidget<MutagenDisplay>(mm2px(Vec(3.f, 14.f)));
        display->module = module;
        display->box.size = mm2px(Vec(24.5f, 50.f));
        addChild(display);

        float xL = 8.f;
        float xR = 22.f;

        // Labels are widgets because nanosvg ignores <text> in the panel.
        addChild(createPanelLabel(Vec(15.24f, 7.f), "MUTAGEN", 8.f));
        addChild(createPanelLabel(Vec(xL, 66.f), "STEPS"));
        addChild(createPanelLabel(Vec(xR, 66.f), "MUTATE"));
        addChild(createPanelLabel(Vec(xL, 77.f), "BANK"));
        addChild(createPanelLabel(Vec(xR, 77.f), "RAND"));
        addChild(createPanelLabel(Vec(xL, 88.f), "SAVE"));
        addChild(createPanelLabel(Vec(xR, 88.f), "CV OUT"));
        addChild(createPanelLabel(Vec(xL, 99.f), "CLK"));
        addChild(createPanelLabel(Vec(xR, 99.f), "RUN"));
        addChild(createPanelLabel(Vec(xL, 110.f), "RST"));
        addChild(createPanelLabel(Vec(xR, 110.f), "ADDR"));

        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(xL, 72.f)), module, Mutagen::STEPS_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(xR, 72.f)), module, Mutagen::MUTATE_RATE_PARAM));

        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(xL, 83.f)), module, Mutagen::BANK_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(xR, 83.f)), module, Mutagen::RANDOM_PARAM));

        addParam(createParamCentered<VCVButton>(mm2px(Vec(xL, 94.f)), module, Mutagen::SAVE_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(14.f, 94.f)), module, Mutagen::SAVE_LIGHT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xR, 94.f)), module, Mutagen::CV_OUTPUT));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xL, 105.f)), module, Mutagen::CLOCK_INPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(14.f, 105.f)), module, Mutagen::CLOCK_LIGHT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xR, 105.f)), module, Mutagen::RUN_INPUT));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xL, 116.f)), module, Mutagen::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xR, 116.f)), module, Mutagen::ADDRESS_CV_INPUT));
    }

    void appendContextMenu(Menu* menu) override {
        auto* module = dynamic_cast<Mutagen*>(this->module);
        if (!module) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolPtrMenuItem<bool>("Bipolar output (-5V to 5V)", "",
                                                   &module->bipolarOutput));
    }
};

} // namespace WiggleRoom

Model* modelMutagen = createModel<WiggleRoom::Mutagen, WiggleRoom::MutagenWidget>("Mutagen");
