#include "rack.hpp"
#include "ImagePanel.hpp"

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct XFade : Module {
    enum ParamId {
        XFADE_PARAM,
        XFADE_CV_AMT_PARAM,
        MODE_PARAM,
        OFFSET_PARAM,
        NUM_PARAMS
    };
    enum InputId {
        IN_A_INPUT,
        IN_B_INPUT,
        XFADE_CV_INPUT,
        NUM_INPUTS
    };
    enum OutputId {
        OUT_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightId {
        NUM_LIGHTS
    };

    enum Mode {
        MODE_XFADE,
        MODE_RING,
        MODE_FOLD
    };

    XFade() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configSwitch(MODE_PARAM, 0.f, 2.f, 0.f, "Mode", {"Crossfade", "Ring Mod", "Fold"});
        configParam(XFADE_PARAM, 0.f, 1.f, 0.5f, "Mix / Amount", "%", 0, 100);
        configParam(XFADE_CV_AMT_PARAM, -1.f, 1.f, 0.f, "CV Amount", "%", 0, 100);
        configSwitch(OFFSET_PARAM, 0.f, 1.f, 0.f, "Offset", {"0V", "+5V"});

        configInput(IN_A_INPUT, "Input A");
        configInput(IN_B_INPUT, "Input B");
        configInput(XFADE_CV_INPUT, "CV");

        configOutput(OUT_OUTPUT, "Output");
    }

    // Fold signal into ±limit range
    float fold(float x, float limit) {
        if (limit <= 0.f) return 0.f;

        // Normalize to ±1 range relative to limit
        x = x / limit;

        // Fold using triangle wave function
        // This maps any value into -1 to 1 range
        x = std::fmod(x + 1.f, 4.f);
        if (x < 0.f) x += 4.f;
        if (x < 2.f) {
            x = x - 1.f;
        } else {
            x = 3.f - x;
        }

        return x * limit;
    }

    void process(const ProcessArgs& args) override {
        float inA = inputs[IN_A_INPUT].getVoltage();
        float inB = inputs[IN_B_INPUT].getVoltage();

        // Get knob + CV
        float knob = params[XFADE_PARAM].getValue();
        if (inputs[XFADE_CV_INPUT].isConnected()) {
            float cv = inputs[XFADE_CV_INPUT].getVoltage() / 10.f;
            float cvAmt = params[XFADE_CV_AMT_PARAM].getValue();
            knob += cv * cvAmt;
        }
        knob = clamp(knob, 0.f, 1.f);

        int mode = static_cast<int>(params[MODE_PARAM].getValue());
        float out = 0.f;

        switch (mode) {
            case MODE_XFADE:
                // Linear crossfade: A * (1-x) + B * x
                out = inA * (1.f - knob) + inB * knob;
                break;

            case MODE_RING:
                // Ring mod: A * B, normalized so ±5V * ±5V = ±5V
                // Knob controls output level
                out = (inA * inB / 5.f) * (knob * 2.f);  // knob 0.5 = unity
                break;

            case MODE_FOLD:
                // Sum A + B, then fold within threshold
                // Knob controls fold threshold: 0.1 = tight fold, 1.0 = 10V (no fold)
                {
                    float sum = inA + inB;
                    float threshold = 1.f + knob * 9.f;  // 1V to 10V
                    out = fold(sum, threshold);
                }
                break;
        }

        // Apply +5V offset if enabled
        if (params[OFFSET_PARAM].getValue() > 0.5f) {
            out += 5.f;
        }

        outputs[OUT_OUTPUT].setVoltage(out);
    }
};

struct XFadeWidget : ModuleWidget {
    XFadeWidget(XFade* module) {
        setModule(module);
        box.size = Vec(4 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/XFade.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float xCenter = box.size.x / 2.0f;

        // Mode switch (3-position)
        addParam(createParamCentered<CKSSThree>(
            Vec(xCenter, 45), module, XFade::MODE_PARAM));

        // Main knob (mix/amount)
        addParam(createParamCentered<RoundBigBlackKnob>(
            Vec(xCenter, 100), module, XFade::XFADE_PARAM));

        // CV attenuverter knob
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(xCenter, 160), module, XFade::XFADE_CV_AMT_PARAM));

        // Input A and B
        addInput(createInputCentered<PJ301MPort>(
            Vec(xCenter - 15, 210), module, XFade::IN_A_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            Vec(xCenter + 15, 210), module, XFade::IN_B_INPUT));

        // CV input
        addInput(createInputCentered<PJ301MPort>(
            Vec(xCenter, 260), module, XFade::XFADE_CV_INPUT));

        // Offset toggle (+5V)
        addParam(createParamCentered<CKSS>(
            Vec(xCenter, 290), module, XFade::OFFSET_PARAM));

        // Output
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(xCenter, 330), module, XFade::OUT_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelXFade = createModel<WiggleRoom::XFade, WiggleRoom::XFadeWidget>("XFade");
