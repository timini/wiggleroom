#include "rack.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace rack;
extern Plugin* pluginInstance;

namespace WiggleRoom {

struct PixelProbe : Module {
    enum ParamId {
        PAN_X_PARAM,
        PAN_Y_PARAM,
        ZOOM_PARAM,
        MODE_PARAM,
        NUM_PARAMS
    };
    enum InputId {
        X_INPUT,
        Y_INPUT,
        NUM_INPUTS
    };
    enum OutputId {
        HUE_OUTPUT,
        SAT_OUTPUT,
        INT_OUTPUT,
        NUM_OUTPUTS
    };

    std::vector<unsigned char> pixels;
    int imgW = 0, imgH = 0;
    std::string loadedPath;

    PixelProbe() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configParam(PAN_X_PARAM, -1.f, 1.f, 0.f, "Pan X");
        configParam(PAN_Y_PARAM, -1.f, 1.f, 0.f, "Pan Y");
        configParam(ZOOM_PARAM, 0.2f, 4.f, 1.f, "Zoom");
        configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Output mode", {"Polar", "Bipolar"});
        configInput(X_INPUT, "X CV");
        configInput(Y_INPUT, "Y CV");
        configOutput(HUE_OUTPUT, "Hue");
        configOutput(SAT_OUTPUT, "Saturation");
        configOutput(INT_OUTPUT, "Intensity");
    }

    bool loadMedia(const std::string& path) {
        int c = 0;
        unsigned char* data = stbi_load(path.c_str(), &imgW, &imgH, &c, 4);
        if (!data || imgW <= 0 || imgH <= 0) {
            return false;
        }
        pixels.assign(data, data + (imgW * imgH * 4));
        stbi_image_free(data);
        loadedPath = path;
        return true;
    }

    static void rgbToHsi(float r, float g, float b, float& h, float& s, float& i) {
        float sum = r + g + b;
        i = sum / 3.f;
        float minc = std::min(r, std::min(g, b));
        s = sum > 1e-6f ? 1.f - (3.f * minc / sum) : 0.f;

        float num = 0.5f * ((r - g) + (r - b));
        float den = std::sqrt((r - g) * (r - g) + (r - b) * (g - b));
        float theta = (den > 1e-6f) ? std::acos(clamp(num / den, -1.f, 1.f)) : 0.f;
        if (b > g) theta = 2.f * M_PI - theta;
        h = theta / (2.f * M_PI);
    }

    void process(const ProcessArgs& args) override {
        if (pixels.empty() || imgW <= 0 || imgH <= 0) {
            outputs[HUE_OUTPUT].setVoltage(0.f);
            outputs[SAT_OUTPUT].setVoltage(0.f);
            outputs[INT_OUTPUT].setVoltage(0.f);
            return;
        }

        float x = clamp(inputs[X_INPUT].getVoltage() / 5.f, -1.f, 1.f);
        float y = clamp(inputs[Y_INPUT].getVoltage() / 5.f, -1.f, 1.f);
        float panX = params[PAN_X_PARAM].getValue();
        float panY = params[PAN_Y_PARAM].getValue();
        float zoom = params[ZOOM_PARAM].getValue();

        float ux = (x / zoom + panX + 1.f) * 0.5f;
        float uy = (y / zoom + panY + 1.f) * 0.5f;
        ux = clamp(ux, 0.f, 1.f);
        uy = clamp(uy, 0.f, 1.f);

        int px = clamp((int)std::round(ux * (imgW - 1)), 0, imgW - 1);
        int py = clamp((int)std::round((1.f - uy) * (imgH - 1)), 0, imgH - 1);
        int idx = (py * imgW + px) * 4;

        float r = pixels[idx] / 255.f;
        float g = pixels[idx + 1] / 255.f;
        float b = pixels[idx + 2] / 255.f;

        float h, s, intensity;
        rgbToHsi(r, g, b, h, s, intensity);

        bool bipolar = params[MODE_PARAM].getValue() > 0.5f;
        auto mapOut = [&](float v) { return bipolar ? (v * 10.f - 5.f) : (v * 10.f); };

        outputs[HUE_OUTPUT].setVoltage(mapOut(h));
        outputs[SAT_OUTPUT].setVoltage(mapOut(s));
        outputs[INT_OUTPUT].setVoltage(mapOut(intensity));
    }
};

struct LoadMediaItem : MenuItem {
    PixelProbe* module;
    void onAction(const event::Action& e) override {
        char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
        if (path) {
            module->loadMedia(path);
            std::free(path);
        }
    }
};

struct PixelCanvas : Widget {
    PixelProbe* module;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(25, 25, 25));
        nvgFill(args.vg);

        if (!module || module->pixels.empty()) return;
        int w = module->imgW;
        int h = module->imgH;
        int img = nvgCreateImageRGBA(args.vg, w, h, 0, module->pixels.data());

        if (img != 0) {
            float zoom = module->params[PixelProbe::ZOOM_PARAM].getValue();
            float panX = module->params[PixelProbe::PAN_X_PARAM].getValue();
            float panY = module->params[PixelProbe::PAN_Y_PARAM].getValue();
            float drawW = box.size.x * zoom;
            float drawH = box.size.y * zoom;
            float ox = (box.size.x - drawW) * 0.5f - panX * box.size.x * 0.5f;
            float oy = (box.size.y - drawH) * 0.5f - panY * box.size.y * 0.5f;

            NVGpaint paint = nvgImagePattern(args.vg, ox, oy, drawW, drawH, 0.f, img, 1.f);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
            nvgFillPaint(args.vg, paint);
            nvgFill(args.vg);
            nvgDeleteImage(args.vg, img);
        }

        float cx = box.size.x * 0.5f;
        float cy = box.size.y * 0.5f;
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, 4.f);
        nvgFillColor(args.vg, nvgRGB(255, 0, 0));
        nvgFill(args.vg);
    }
};

struct PixelProbeWidget : ModuleWidget {
    PixelProbeWidget(PixelProbe* module) {
        setModule(module);
        box.size = Vec(8 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        auto canvas = new PixelCanvas();
        canvas->box.pos = Vec(8, 35);
        canvas->box.size = Vec(box.size.x - 16, box.size.x - 16);
        canvas->module = module;
        addChild(canvas);

        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(25, 150), module, PixelProbe::PAN_X_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(55, 150), module, PixelProbe::PAN_Y_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(85, 150), module, PixelProbe::ZOOM_PARAM));
        addParam(createParamCentered<CKSS>(Vec(55, 180), module, PixelProbe::MODE_PARAM));

        addInput(createInputCentered<PJ301MPort>(Vec(25, 220), module, PixelProbe::X_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(55, 220), module, PixelProbe::Y_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(Vec(25, 260), module, PixelProbe::HUE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(55, 260), module, PixelProbe::SAT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(85, 260), module, PixelProbe::INT_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<PixelProbe*>(module);
        menu->addChild(new MenuSeparator());
        auto* item = createMenuItem<LoadMediaItem>("Load image/video");
        item->module = m;
        menu->addChild(item);
    }
};

} // namespace WiggleRoom

Model* modelPixelProbe = createModel<WiggleRoom::PixelProbe, WiggleRoom::PixelProbeWidget>("PixelProbe");
