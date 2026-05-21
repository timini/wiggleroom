// PixelProbe — sample a CV-addressed point in an image and emit Hue,
// Saturation, Intensity as CV. Native C++, no Faust DSP.
//
// Synthesizes Codex PRs #49 / #50 / #51 and addresses the inline Codex
// review feedback from #49 (persist loaded-image state, draw the probe
// marker at the actually-sampled coordinate, keep jacks inside the
// panel bounds) and #52 (audio/UI race on image swap; nvgImage leak).

#include "rack.hpp"
#include <osdialog.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"

using namespace rack;
extern Plugin* pluginInstance;

namespace WiggleRoom {

// Immutable image snapshot. The audio thread holds a `shared_ptr<const
// ImageData>` for the duration of a process() call so it never observes
// a half-written buffer. New loads create a fresh ImageData and atomically
// swap the module's shared_ptr — the audio thread either sees the old
// snapshot or the new one, never a torn state.
struct ImageData {
    std::vector<unsigned char> pixels;  // RGBA8
    int w = 0;
    int h = 0;
    std::string path;
};

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
    enum LightId { NUM_LIGHTS };

    // The current image. Accessed atomically (free-function shared_ptr
    // atomics, C++17). All loads (audio thread / UI thread / widget) take
    // a snapshot via atomic_load; loadMedia / clearMedia publish a new
    // snapshot via atomic_store.
    std::shared_ptr<const ImageData> currentImage = std::make_shared<ImageData>();

    // Last-sampled normalized canvas coordinate (within [0, 1]^2) after
    // pan/zoom. The widget reads these to draw the probe marker at the
    // actually-sampled location instead of the raw CV position.
    // Updated only from the audio thread; widget reads it for display
    // (single-writer / single-reader floats; tearing here is cosmetic).
    float sampledU = 0.5f;
    float sampledV = 0.5f;

    PixelProbe() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(PAN_X_PARAM, -1.f, 1.f, 0.f, "Pan X");
        configParam(PAN_Y_PARAM, -1.f, 1.f, 0.f, "Pan Y");
        configParam(ZOOM_PARAM, 0.2f, 4.f, 1.f, "Zoom");
        configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Output range",
                     {"Polar (0..10 V)", "Bipolar (-5..+5 V)"});
        configInput(X_INPUT, "X probe (-5..+5 V)");
        configInput(Y_INPUT, "Y probe (-5..+5 V)");
        configOutput(HUE_OUTPUT, "Hue");
        configOutput(SAT_OUTPUT, "Saturation");
        configOutput(INT_OUTPUT, "Intensity");
    }

    std::shared_ptr<const ImageData> snapshotImage() const {
        return std::atomic_load(&currentImage);
    }

    void publishImage(std::shared_ptr<const ImageData> img) {
        std::atomic_store(&currentImage, std::move(img));
    }

    bool loadMedia(const std::string& path) {
        int w = 0, h = 0, c = 0;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 4);
        if (!data || w <= 0 || h <= 0) {
            if (data) stbi_image_free(data);
            return false;
        }
        auto img = std::make_shared<ImageData>();
        img->pixels.assign(data, data + (w * h * 4));
        img->w = w;
        img->h = h;
        img->path = path;
        stbi_image_free(data);
        publishImage(std::move(img));
        return true;
    }

    std::string loadedPath() const {
        return snapshotImage()->path;
    }

    static void rgbToHsi(float r, float g, float b, float& h, float& s, float& i) {
        float sum = r + g + b;
        i = sum / 3.f;
        float minc = std::min(r, std::min(g, b));
        s = (sum > 1e-6f) ? (1.f - (3.f * minc / sum)) : 0.f;

        float num = 0.5f * ((r - g) + (r - b));
        float den = std::sqrt((r - g) * (r - g) + (r - b) * (g - b));
        float theta = (den > 1e-6f) ? std::acos(clamp(num / den, -1.f, 1.f)) : 0.f;
        if (b > g) theta = 2.f * (float)M_PI - theta;
        h = theta / (2.f * (float)M_PI);
    }

    void process(const ProcessArgs& /*args*/) override {
        // Pin the current image for the duration of this block — the UI
        // thread can swap currentImage in mid-flight without invalidating
        // the buffers we're reading.
        std::shared_ptr<const ImageData> img = snapshotImage();

        float xCv = clamp(inputs[X_INPUT].getVoltage() / 5.f, -1.f, 1.f);
        float yCv = clamp(inputs[Y_INPUT].getVoltage() / 5.f, -1.f, 1.f);
        float panX = params[PAN_X_PARAM].getValue();
        float panY = params[PAN_Y_PARAM].getValue();
        float zoom = std::max(0.01f, params[ZOOM_PARAM].getValue());

        // CV (-1..+1) -> normalized image coord (0..1), with pan/zoom.
        float u = (xCv / zoom + panX + 1.f) * 0.5f;
        float v = (yCv / zoom + panY + 1.f) * 0.5f;
        u = clamp(u, 0.f, 1.f);
        v = clamp(v, 0.f, 1.f);
        sampledU = u;
        sampledV = v;

        if (!img || img->pixels.empty() || img->w <= 0 || img->h <= 0) {
            outputs[HUE_OUTPUT].setVoltage(0.f);
            outputs[SAT_OUTPUT].setVoltage(0.f);
            outputs[INT_OUTPUT].setVoltage(0.f);
            return;
        }

        int px = clamp((int)std::round(u * (img->w - 1)), 0, img->w - 1);
        // Image row 0 is at the top of the canvas; v = 0 sits at the bottom,
        // so we flip vertically when indexing the pixel buffer.
        int py = clamp((int)std::round((1.f - v) * (img->h - 1)), 0, img->h - 1);
        int idx = (py * img->w + px) * 4;

        float r = img->pixels[idx]     / 255.f;
        float g = img->pixels[idx + 1] / 255.f;
        float b = img->pixels[idx + 2] / 255.f;

        float h, s, intensity;
        rgbToHsi(r, g, b, h, s, intensity);

        bool bipolar = params[MODE_PARAM].getValue() > 0.5f;
        auto mapOut = [&](float val) {
            return bipolar ? (val * 10.f - 5.f) : (val * 10.f);
        };

        outputs[HUE_OUTPUT].setVoltage(mapOut(h));
        outputs[SAT_OUTPUT].setVoltage(mapOut(s));
        outputs[INT_OUTPUT].setVoltage(mapOut(intensity));
    }

    // ---- Persistence ------------------------------------------------------
    // Persist the loaded image path so patches survive save/load. On load
    // we attempt to re-read the file; if it has moved or been deleted, the
    // module behaves as if no image is loaded and the user can re-pick via
    // the context menu.

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        std::string p = loadedPath();
        if (!p.empty()) {
            json_object_set_new(rootJ, "loadedPath", json_string(p.c_str()));
        }
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* pJ = json_object_get(rootJ, "loadedPath");
        if (pJ && json_is_string(pJ)) {
            std::string p = json_string_value(pJ);
            if (!p.empty()) loadMedia(p);
        }
    }
};

// ---------------------------------------------------------------------------
// Widget
// ---------------------------------------------------------------------------

struct PixelCanvas : Widget {
    PixelProbe* module = nullptr;

    // GPU-side cache of the most recently uploaded image. Identified by
    // shared_ptr identity rather than by image dimensions, so we re-upload
    // whenever the module publishes a new ImageData snapshot.
    int nvgHandle = -1;
    std::shared_ptr<const ImageData> uploaded;

    // Last NanoVG context observed in draw(). Cached so the destructor can
    // free the GPU image when the widget is torn down (Codex P2 #192).
    NVGcontext* lastVg = nullptr;

    ~PixelCanvas() override {
        if (nvgHandle != -1 && lastVg) {
            nvgDeleteImage(lastVg, nvgHandle);
            nvgHandle = -1;
        }
    }

    void ensureTexture(NVGcontext* vg,
                       const std::shared_ptr<const ImageData>& img) {
        bool empty = !img || img->pixels.empty() || img->w <= 0 || img->h <= 0;
        if (empty) {
            if (nvgHandle != -1) {
                nvgDeleteImage(vg, nvgHandle);
                nvgHandle = -1;
            }
            uploaded.reset();
            return;
        }
        if (nvgHandle == -1 || img != uploaded) {
            if (nvgHandle != -1) nvgDeleteImage(vg, nvgHandle);
            nvgHandle = nvgCreateImageRGBA(vg, img->w, img->h, 0,
                                            img->pixels.data());
            uploaded = img;
        }
    }

    void draw(const DrawArgs& args) override {
        lastVg = args.vg;

        // Background
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(20, 20, 20));
        nvgFill(args.vg);

        if (!module) return;

        std::shared_ptr<const ImageData> img = module->snapshotImage();
        ensureTexture(args.vg, img);

        float zoom = std::max(0.01f, module->params[PixelProbe::ZOOM_PARAM].getValue());
        float panX = module->params[PixelProbe::PAN_X_PARAM].getValue();
        float panY = module->params[PixelProbe::PAN_Y_PARAM].getValue();
        float drawW = box.size.x * zoom;
        float drawH = box.size.y * zoom;
        float ox = (box.size.x - drawW) * 0.5f - panX * box.size.x * 0.5f;
        float oy = (box.size.y - drawH) * 0.5f - panY * box.size.y * 0.5f;

        if (nvgHandle != -1) {
            NVGpaint paint = nvgImagePattern(args.vg, ox, oy, drawW, drawH,
                                              0.f, nvgHandle, 1.f);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
            nvgFillPaint(args.vg, paint);
            nvgFill(args.vg);
        }

        // Probe marker at the actually-sampled point.
        float sx, sy;
        if (nvgHandle != -1) {
            sx = ox + module->sampledU * drawW;
            sy = oy + (1.f - module->sampledV) * drawH;
        } else {
            // No image: fall back to canvas-relative position.
            sx = module->sampledU * box.size.x;
            sy = (1.f - module->sampledV) * box.size.y;
        }
        sx = clamp(sx, 0.f, box.size.x);
        sy = clamp(sy, 0.f, box.size.y);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, sx, sy, 4.f);
        nvgStrokeColor(args.vg, nvgRGB(255, 255, 255));
        nvgStrokeWidth(args.vg, 1.5f);
        nvgStroke(args.vg);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, sx, sy, 1.6f);
        nvgFillColor(args.vg, nvgRGB(255, 70, 70));
        nvgFill(args.vg);
    }
};

struct LoadMediaItem : MenuItem {
    PixelProbe* module = nullptr;
    void onAction(const event::Action& /*e*/) override {
        if (!module) return;
        char* path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, nullptr);
        if (path) {
            module->loadMedia(path);
            std::free(path);
        }
    }
};

struct PixelProbeWidget : ModuleWidget {
    PixelProbeWidget(PixelProbe* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/PixelProbe.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(
            Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH,
                RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // 8 HP panel: box.size.x = 120 px, box.size.y = 380 px.
        // Square canvas at the top, controls + jacks below — all kept inside
        // the 380 px panel height.
        const float margin = 8.f;
        auto* canvas = new PixelCanvas();
        canvas->box.pos = Vec(margin, 22.f);
        canvas->box.size = Vec(box.size.x - 2 * margin,
                                box.size.x - 2 * margin);
        canvas->module = module;
        addChild(canvas);

        const float colL = box.size.x * 0.25f;
        const float colC = box.size.x * 0.50f;
        const float colR = box.size.x * 0.75f;

        const float yKnobs = canvas->box.pos.y + canvas->box.size.y + 18.f;
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(colL, yKnobs), module, PixelProbe::PAN_X_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(colC, yKnobs), module, PixelProbe::PAN_Y_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(colR, yKnobs), module, PixelProbe::ZOOM_PARAM));

        const float ySwitch = yKnobs + 28.f;
        addParam(createParamCentered<CKSS>(
            Vec(colC, ySwitch), module, PixelProbe::MODE_PARAM));

        const float yInputs  = ySwitch + 32.f;
        addInput(createInputCentered<PJ301MPort>(
            Vec(colL, yInputs), module, PixelProbe::X_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            Vec(colR, yInputs), module, PixelProbe::Y_INPUT));

        const float yOutputs = yInputs + 32.f;
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(colL, yOutputs), module, PixelProbe::HUE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(colC, yOutputs), module, PixelProbe::SAT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(colR, yOutputs), module, PixelProbe::INT_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<PixelProbe*>(this->module);
        menu->addChild(new MenuSeparator());
        auto* item = createMenuItem<LoadMediaItem>("Load image…");
        item->module = m;
        menu->addChild(item);
        if (m) {
            std::string p = m->loadedPath();
            if (!p.empty()) {
                menu->addChild(createMenuLabel(std::string("Loaded: ") + p));
            }
        }
    }
};

} // namespace WiggleRoom

Model* modelPixelProbe = createModel<WiggleRoom::PixelProbe,
                                      WiggleRoom::PixelProbeWidget>("PixelProbe");
