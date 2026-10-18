/******************************************************************************
 * MUTAGEN X
 * Slave lane for Mutagen. Its own values, its own screen, its own CV out.
 *
 * Takes clock, run, reset, step address and bank index from the chain on
 * its left, and relays the same message rightward, so any number of these
 * can be stacked to build a multi-lane sequencer.
 *
 * Its Steps knob is independent. The broadcast address wraps into whatever
 * length this lane is set to, so matching lengths run in lockstep and
 * differing lengths phase against each other.
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

struct MutagenX : MutagenLane {
    enum ParamId {
        STEPS_PARAM,
        MUTATE_RATE_PARAM,
        RANDOM_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        INPUTS_LEN
    };
    enum OutputId {
        CV_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LINK_LIGHT,
        LIGHTS_LEN
    };

    dsp::SchmittTrigger randomTrigger;
    MutagenExpanderMessage rightMessages[2];

    MutagenX() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(STEPS_PARAM, (float)StepSequence::MIN_STEPS,
                    (float)StepSequence::MAX_STEPS,
                    (float)StepSequence::DEFAULT_STEPS, "Steps");
        paramQuantities[STEPS_PARAM]->snapEnabled = true;

        configParam(MUTATE_RATE_PARAM, 0.f, 1.f, 0.f, "Mutation rate", "%", 0.f, 100.f);
        configButton(RANDOM_PARAM, "Randomise all steps");

        configOutput(CV_OUTPUT, "CV");
        configLight(LINK_LIGHT, "Linked to a Mutagen chain");

        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];
    }

    void onReset() override {
        seq = StepSequence();
        addresser.reset();
        currentBank = 0;
    }

    // Accept either the master or another slave, which is what makes the
    // chain arbitrarily long.
    bool readUpstream(MutagenExpanderMessage& out) {
        if (!leftExpander.module) return false;
        Model* m = leftExpander.module->model;
        if (m != modelMutagen && m != modelMutagenX) return false;
        auto* msg = static_cast<MutagenExpanderMessage*>(
            leftExpander.module->rightExpander.consumerMessage);
        if (!msg || !msg->valid) return false;
        out = *msg;
        return true;
    }

    void process(const ProcessArgs& args) override {
        seq.setSteps(static_cast<int>(params[STEPS_PARAM].getValue()));

        if (randomTrigger.process(params[RANDOM_PARAM].getValue()))
            seq.randomize();

        MutagenExpanderMessage upstream;
        bool linked = readUpstream(upstream);
        lights[LINK_LIGHT].setBrightness(linked ? 1.f : 0.f);

        if (linked) {
            if (upstream.resetPulse) addresser.reset();

            if (upstream.bankIndex >= 0)
                applyBankIndex(upstream.bankIndex);

            // Save writes this lane's own values into its own store, so
            // recall works without anyone writing into a neighbour.
            if (upstream.saveRequest)
                banks.save(currentBank, seq);

            if (upstream.clockTick && upstream.running) {
                if (upstream.stepAddress >= 0)
                    addresser.followBroadcast(upstream.stepAddress, seq.steps);
                else
                    addresser.tick(seq.steps, false, 0.f);

                seq.mutateOnce(params[MUTATE_RATE_PARAM].getValue());
            }
        }

        outputs[CV_OUTPUT].setVoltage(outputVoltage());

        relayDownstream(linked, upstream);
    }

    void relayDownstream(bool linked, const MutagenExpanderMessage& upstream) {
        if (!rightExpander.module || rightExpander.module->model != modelMutagenX)
            return;

        auto* msg = static_cast<MutagenExpanderMessage*>(rightExpander.producerMessage);
        if (linked) {
            // Pass the chain state through untouched; each lane applies it
            // against its own length and its own banks.
            *msg = upstream;
        } else {
            msg->clear();
        }
        msg->valid = linked;
        rightExpander.requestMessageFlip();
    }

    json_t* dataToJson() override { return laneToJson(); }
    void dataFromJson(json_t* rootJ) override { laneFromJson(rootJ); }
};

struct MutagenXWidget : ModuleWidget {
    MutagenXWidget(MutagenX* module) {
        setModule(module);
        box.size = Vec(4 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/MutagenX.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(
            Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        auto* display = createWidget<MutagenDisplay>(mm2px(Vec(2.5f, 14.f)));
        display->module = module;
        display->box.size = mm2px(Vec(15.3f, 50.f));
        addChild(display);

        float xC = 10.16f;

        addChild(createPanelLabel(Vec(xC, 7.f), "MUT X", 8.f));
        addChild(createPanelLabel(Vec(xC, 74.f), "STEPS"));
        addChild(createPanelLabel(Vec(xC, 87.f), "MUTATE"));
        addChild(createPanelLabel(Vec(xC, 100.f), "RAND"));
        addChild(createPanelLabel(Vec(xC, 110.f), "CV OUT"));

        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(xC, 69.f)), module, MutagenX::LINK_LIGHT));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(xC, 80.f)), module, MutagenX::STEPS_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(xC, 93.f)), module, MutagenX::MUTATE_RATE_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(xC, 105.f)), module, MutagenX::RANDOM_PARAM));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xC, 116.f)), module, MutagenX::CV_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        auto* module = dynamic_cast<MutagenX*>(this->module);
        if (!module) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolPtrMenuItem<bool>("Bipolar output (-5V to 5V)", "",
                                                   &module->bipolarOutput));
    }
};

} // namespace WiggleRoom

Model* modelMutagenX = createModel<WiggleRoom::MutagenX, WiggleRoom::MutagenXWidget>("MutagenX");
