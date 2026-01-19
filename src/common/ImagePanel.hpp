#pragma once
#include "rack.hpp"

namespace WiggleRoom {

/**
 * ImagePanel - Renders a PNG/JPG image as a module background
 */
struct ImagePanel : rack::widget::OpaqueWidget {
    std::string imagePath;
    int imgHandle = -1;
    bool imageLoaded = false;

    ImagePanel(const std::string& path, rack::math::Vec size) {
        imagePath = path;
        box.pos = rack::math::Vec(0.f, 0.f);
        box.size = size;
    }

    void draw(const DrawArgs& args) override {
        // Load image once
        if (!imageLoaded) {
            imgHandle = nvgCreateImage(args.vg, imagePath.c_str(), 0);
            imageLoaded = true;
        }

        // Draw dark background first to cover VCV's default white
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(20, 16, 30));
        nvgFill(args.vg);

        if (imgHandle > 0) {
            // Simple approach: pattern extent = widget size
            // This should stretch the image to fill the widget
            NVGpaint imgPaint = nvgImagePattern(
                args.vg,
                0.f, 0.f,                // Origin
                box.size.x, box.size.y,  // Extent = widget size (stretches image)
                0.f,                     // Angle
                imgHandle,
                1.0f                     // Alpha
            );

            nvgBeginPath(args.vg);
            nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
            nvgFillPaint(args.vg, imgPaint);
            nvgFill(args.vg);
        }

        OpaqueWidget::draw(args);
    }
};

} // namespace WiggleRoom
