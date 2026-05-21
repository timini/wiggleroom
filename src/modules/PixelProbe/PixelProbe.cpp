#include "rack.hpp"
#include "ImagePanel.hpp"
#include <string>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct PixelProbe;

struct PixelCanvas : Widget {
    PixelProbe* module = nullptr;
    Rect viewRect;

    void draw(const DrawArgs& args) override;
    void onButton(const event::Button& e) override;
    void onDragMove(const event::DragMove& e) override;
    void onHoverScroll(const event::HoverScroll& e) override;
};

struct PixelProbe : Module {
    enum ParamId { MODE_PARAM, NUM_PARAMS };
    enum InputId { X_INPUT, Y_INPUT, NUM_INPUTS };
    enum OutputId { HUE_OUTPUT, SAT_OUTPUT, INT_OUTPUT, NUM_OUTPUTS };
    enum LightId { NUM_LIGHTS };

    std::string mediaPath;
    int imageHandle = -1;
    float zoom = 1.f;
    Vec offset = Vec(0.f, 0.f);
    Vec probe = Vec(0.5f, 0.5f);

    PixelProbe() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Output mode", {"Bipolar (-5V..+5V)", "Unipolar (0V..10V)"});
        configInput(X_INPUT, "Probe X CV");
        configInput(Y_INPUT, "Probe Y CV");
        configOutput(HUE_OUTPUT, "Hue");
        configOutput(SAT_OUTPUT, "Saturation");
        configOutput(INT_OUTPUT, "Intensity");
    }

    void setMedia(const std::string& path) {
        mediaPath = path;
        imageHandle = -1;
    }

    void process(const ProcessArgs& args) override {
        float x = probe.x;
        float y = probe.y;
        if (inputs[X_INPUT].isConnected())
            x = clamp(inputs[X_INPUT].getVoltage() / 10.f, 0.f, 1.f);
        if (inputs[Y_INPUT].isConnected())
            y = clamp(inputs[Y_INPUT].getVoltage() / 10.f, 0.f, 1.f);

        float cx = x - 0.5f;
        float cy = y - 0.5f;
        float hue = std::atan2(cy, cx) / (2.f * M_PI) + 0.5f;
        float sat = clamp(std::sqrt(cx * cx + cy * cy) * 2.f, 0.f, 1.f);
        float intensity = clamp(1.f - sat * 0.5f, 0.f, 1.f);

        bool bipolar = params[MODE_PARAM].getValue() < 0.5f;
        auto cv = [bipolar](float v) { return bipolar ? (v * 10.f - 5.f) : (v * 10.f); };
        outputs[HUE_OUTPUT].setVoltage(cv(hue));
        outputs[SAT_OUTPUT].setVoltage(cv(sat));
        outputs[INT_OUTPUT].setVoltage(cv(intensity));
    }
};

void PixelCanvas::draw(const DrawArgs& args) {
    nvgSave(args.vg);
    nvgBeginPath(args.vg);
    nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(args.vg, nvgRGB(20, 20, 20));
    nvgFill(args.vg);

    if (module && !module->mediaPath.empty()) {
        if (module->imageHandle < 0) {
            module->imageHandle = nvgCreateImage(args.vg, module->mediaPath.c_str(), 0);
        }
        if (module->imageHandle >= 0) {
            int w = 0, h = 0;
            nvgImageSize(args.vg, module->imageHandle, &w, &h);
            if (w > 0 && h > 0) {
                float side = std::min(box.size.x, box.size.y) * module->zoom;
                Vec c = box.size.div(2.f).plus(module->offset);
                NVGpaint paint = nvgImagePattern(args.vg, c.x - side * 0.5f, c.y - side * 0.5f, side, side * ((float)h / (float)w), 0.f, module->imageHandle, 1.f);
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
                nvgFillPaint(args.vg, paint);
                nvgFill(args.vg);
            }
        }
    }

    if (module) {
        Vec p = Vec(module->probe.x * box.size.x, module->probe.y * box.size.y);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, p.x, p.y, 4.f);
        nvgStrokeWidth(args.vg, 2.f);
        nvgStrokeColor(args.vg, nvgRGB(255, 255, 255));
        nvgStroke(args.vg);
    }

    nvgRestore(args.vg);
}

void PixelCanvas::onButton(const event::Button& e) {
    if (!module) return;
    if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
        module->probe = Vec(clamp(e.pos.x / box.size.x, 0.f, 1.f), clamp(e.pos.y / box.size.y, 0.f, 1.f));
        e.consume(this);
    }
}

void PixelCanvas::onDragMove(const event::DragMove& e) {
    if (!module) return;
    if ((APP->window->getMods() & RACK_MOD_MASK_SHIFT) != 0) {
        module->offset = module->offset.plus(e.mouseDelta);
    } else {
        Vec p = e.pos;
        module->probe = Vec(clamp(p.x / box.size.x, 0.f, 1.f), clamp(p.y / box.size.y, 0.f, 1.f));
    }
    e.consume(this);
}

void PixelCanvas::onHoverScroll(const event::HoverScroll& e) {
    if (!module) return;
    module->zoom = clamp(module->zoom + e.scrollDelta.y * -0.05f, 0.25f, 4.f);
    e.consume(this);
}

struct PixelProbeWidget : ModuleWidget {
    PixelProbeWidget(PixelProbe* module) {
        setModule(module);
        box.size = Vec(8 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(asset::plugin(pluginInstance, "res/XFade.png"), box.size));

        auto* canvas = new PixelCanvas();
        canvas->module = module;
        canvas->box.pos = Vec(12, 50);
        canvas->box.size = Vec(box.size.x - 24, box.size.x - 24);
        addChild(canvas);

        addParam(createParamCentered<CKSS>(Vec(box.size.x * 0.5f, 40), module, PixelProbe::MODE_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(25, 280), module, PixelProbe::X_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(75, 280), module, PixelProbe::Y_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(25, 330), module, PixelProbe::HUE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(60, 330), module, PixelProbe::SAT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(95, 330), module, PixelProbe::INT_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        auto* module = dynamic_cast<PixelProbe*>(this->module);
        assert(menu);
        menu->addChild(new MenuSeparator());

        struct LoadItem : MenuItem {
            PixelProbe* module;
            void onAction(const event::Action& e) override {
                std::string path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, "Media\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.mp4;*.mov;*.mkv\0");
                if (!path.empty() && module) module->setMedia(path);
            }
        };

        LoadItem* item = createMenuItem<LoadItem>("Load Image/Video");
        item->module = module;
        menu->addChild(item);
    }
};

} // namespace WiggleRoom

Model* modelPixelProbe = createModel<WiggleRoom::PixelProbe, WiggleRoom::PixelProbeWidget>("PixelProbe");
