#include "rack.hpp"
#include <algorithm>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct PixelProbe : Module {
    enum ParamId {
        ZOOM_PARAM,
        OFFSET_X_PARAM,
        OFFSET_Y_PARAM,
        MODE_PARAM,
        NUM_PARAMS
    };
    enum InputId {
        X_CV_INPUT,
        Y_CV_INPUT,
        NUM_INPUTS
    };
    enum OutputId {
        HUE_OUTPUT,
        SAT_OUTPUT,
        INT_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightId { NUM_LIGHTS };

    // Placeholder media state: "image" shows static gradient, "video" animates it.
    bool videoMode = false;
    float phase = 0.f;

    PixelProbe() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(ZOOM_PARAM, 0.5f, 4.f, 1.f, "Zoom");
        configParam(OFFSET_X_PARAM, -1.f, 1.f, 0.f, "Media X offset");
        configParam(OFFSET_Y_PARAM, -1.f, 1.f, 0.f, "Media Y offset");
        configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Output range", {"Polar (0..10V)", "Bipolar (-5..5V)"});
        configInput(X_CV_INPUT, "Probe X CV");
        configInput(Y_CV_INPUT, "Probe Y CV");
        configOutput(HUE_OUTPUT, "Hue");
        configOutput(SAT_OUTPUT, "Saturation");
        configOutput(INT_OUTPUT, "Intensity");
    }

    static void rgbToHsi(float r, float g, float b, float& h, float& s, float& i) {
        i = (r + g + b) / 3.f;
        float minRgb = std::min(r, std::min(g, b));
        s = (i > 1e-6f) ? 1.f - (minRgb / i) : 0.f;

        float num = 0.5f * ((r - g) + (r - b));
        float den = std::sqrt((r - g) * (r - g) + (r - b) * (g - b));
        float theta = (den > 1e-6f) ? std::acos(clamp(num / den, -1.f, 1.f)) : 0.f;
        h = (b <= g) ? theta : 2.f * M_PI - theta;
        h /= 2.f * M_PI;
    }

    void sampleMedia(float u, float v, float& r, float& g, float& b) {
        float t = videoMode ? phase : 0.f;
        r = clamp(0.5f + 0.5f * std::sin((u + t) * 6.28318f), 0.f, 1.f);
        g = clamp(0.5f + 0.5f * std::sin((v * 1.3f + t * 0.8f) * 6.28318f), 0.f, 1.f);
        b = clamp(0.5f + 0.5f * std::sin(((u + v) * 0.7f + t * 0.6f) * 6.28318f), 0.f, 1.f);
    }

    void process(const ProcessArgs& args) override {
        phase += args.sampleTime * 0.2f;

        float xCv = inputs[X_CV_INPUT].isConnected() ? clamp(inputs[X_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f) : 0.5f;
        float yCv = inputs[Y_CV_INPUT].isConnected() ? clamp(inputs[Y_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f) : 0.5f;

        float zoom = params[ZOOM_PARAM].getValue();
        float offX = params[OFFSET_X_PARAM].getValue();
        float offY = params[OFFSET_Y_PARAM].getValue();

        float u = clamp((xCv - 0.5f) / zoom + 0.5f + offX * 0.5f, 0.f, 1.f);
        float v = clamp((yCv - 0.5f) / zoom + 0.5f + offY * 0.5f, 0.f, 1.f);

        float r, g, b;
        sampleMedia(u, v, r, g, b);

        float h, s, inten;
        rgbToHsi(r, g, b, h, s, inten);

        bool bipolar = params[MODE_PARAM].getValue() > 0.5f;
        auto scaleOut = [bipolar](float x) { return bipolar ? (x * 10.f - 5.f) : (x * 10.f); };

        outputs[HUE_OUTPUT].setVoltage(scaleOut(h));
        outputs[SAT_OUTPUT].setVoltage(scaleOut(s));
        outputs[INT_OUTPUT].setVoltage(scaleOut(inten));
    }
};

struct ProbeDisplay : Widget {
    PixelProbe* module = nullptr;

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(20, 20, 20));
        nvgFill(args.vg);

        if (!module) return;

        // Draw pseudo-media gradient
        const int steps = 16;
        for (int y = 0; y < steps; y++) {
            for (int x = 0; x < steps; x++) {
                float u = (x + 0.5f) / steps;
                float v = (y + 0.5f) / steps;
                float r, g, b;
                module->sampleMedia(u, v, r, g, b);
                nvgBeginPath(args.vg);
                float cw = box.size.x / steps;
                float ch = box.size.y / steps;
                nvgRect(args.vg, x * cw, y * ch, cw + 0.6f, ch + 0.6f);
                nvgFillColor(args.vg, nvgRGBf(r, g, b));
                nvgFill(args.vg);
            }
        }

        float xCv = module->inputs[PixelProbe::X_CV_INPUT].isConnected() ? clamp(module->inputs[PixelProbe::X_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f) : 0.5f;
        float yCv = module->inputs[PixelProbe::Y_CV_INPUT].isConnected() ? clamp(module->inputs[PixelProbe::Y_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f) : 0.5f;

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, xCv * box.size.x, yCv * box.size.y, 4.f);
        nvgStrokeColor(args.vg, nvgRGB(255, 255, 255));
        nvgStrokeWidth(args.vg, 1.2f);
        nvgStroke(args.vg);
    }
};

struct PixelProbeWidget : ModuleWidget {
    PixelProbeWidget(PixelProbe* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/PixelProbe.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        auto* display = createWidget<ProbeDisplay>(Vec(10.f, 40.f));
        display->box.size = Vec(box.size.x - 20.f, box.size.x - 20.f);
        display->module = module;
        addChild(display);

        float x = box.size.x / 2.f;
        float y = box.size.x + 35.f;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(x, y), module, PixelProbe::ZOOM_PARAM));
        addParam(createParamCentered<Trimpot>(Vec(x - 18.f, y + 32.f), module, PixelProbe::OFFSET_X_PARAM));
        addParam(createParamCentered<Trimpot>(Vec(x + 18.f, y + 32.f), module, PixelProbe::OFFSET_Y_PARAM));
        addParam(createParamCentered<CKSS>(Vec(x, y + 56.f), module, PixelProbe::MODE_PARAM));

        addInput(createInputCentered<PJ301MPort>(Vec(x - 18.f, y + 94.f), module, PixelProbe::X_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(x + 18.f, y + 94.f), module, PixelProbe::Y_CV_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(Vec(x, y + 132.f), module, PixelProbe::HUE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(x - 18.f, y + 164.f), module, PixelProbe::SAT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(x + 18.f, y + 164.f), module, PixelProbe::INT_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        auto* module = dynamic_cast<PixelProbe*>(this->module);
        assert(menu);
        menu->addChild(new MenuSeparator());
        menu->addChild(createMenuLabel("Media source"));
        menu->addChild(createCheckMenuItem("Image", "", [=]() { return module && !module->videoMode; }, [=]() { if (module) module->videoMode = false; }));
        menu->addChild(createCheckMenuItem("Video", "", [=]() { return module && module->videoMode; }, [=]() { if (module) module->videoMode = true; }));
    }
};

} // namespace WiggleRoom

Model* modelPixelProbe = createModel<WiggleRoom::PixelProbe, WiggleRoom::PixelProbeWidget>("PixelProbe");
