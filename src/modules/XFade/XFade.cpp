#include "rack.hpp"
#include "ImagePanel.hpp"

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct XFade : Module {
    enum ParamId {
        XFADE_PARAM,
        XFADE_CV_AMT_PARAM,
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

    XFade() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(XFADE_PARAM, 0.f, 1.f, 0.5f, "Crossfade", "%", 0, 100);
        configParam(XFADE_CV_AMT_PARAM, -1.f, 1.f, 0.f, "CV Amount", "%", 0, 100);

        configInput(IN_A_INPUT, "Input A");
        configInput(IN_B_INPUT, "Input B");
        configInput(XFADE_CV_INPUT, "Crossfade CV");

        configOutput(OUT_OUTPUT, "Mixed Output");
    }

    void process(const ProcessArgs& args) override {
        // Read inputs
        float inA = inputs[IN_A_INPUT].getVoltage();
        float inB = inputs[IN_B_INPUT].getVoltage();

        // Calculate crossfade position (knob + CV)
        float xfade = params[XFADE_PARAM].getValue();

        if (inputs[XFADE_CV_INPUT].isConnected()) {
            float cv = inputs[XFADE_CV_INPUT].getVoltage() / 10.f;  // 0-10V -> 0-1
            float cvAmt = params[XFADE_CV_AMT_PARAM].getValue();
            xfade += cv * cvAmt;
        }

        // Clamp to 0-1
        xfade = clamp(xfade, 0.f, 1.f);

        // Linear crossfade: A * (1-x) + B * x
        float out = inA * (1.f - xfade) + inB * xfade;

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

        // Large crossfade knob
        addParam(createParamCentered<RoundBigBlackKnob>(
            Vec(xCenter, 80), module, XFade::XFADE_PARAM));

        // Small CV attenuverter knob
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(xCenter, 150), module, XFade::XFADE_CV_AMT_PARAM));

        // Input A and B
        addInput(createInputCentered<PJ301MPort>(
            Vec(xCenter - 15, 210), module, XFade::IN_A_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            Vec(xCenter + 15, 210), module, XFade::IN_B_INPUT));

        // Crossfade CV input
        addInput(createInputCentered<PJ301MPort>(
            Vec(xCenter, 260), module, XFade::XFADE_CV_INPUT));

        // Mixed output
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(xCenter, 320), module, XFade::OUT_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelXFade = createModel<WiggleRoom::XFade, WiggleRoom::XFadeWidget>("XFade");
