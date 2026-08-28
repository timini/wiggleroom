#pragma once
/******************************************************************************
 * PANEL LABEL
 * Small centred caption drawn over the panel.
 *
 * Rack's SVG renderer (nanosvg) ignores <text> elements, so labels baked
 * into a panel SVG never appear. Drawing them as widgets instead keeps the
 * panel art simple and guarantees every jack is named, which the older
 * PNG-panel modules here do not manage.
 ******************************************************************************/

#include "rack.hpp"

using namespace rack;

namespace WiggleRoom {

struct PanelLabel : TransparentWidget {
    std::string text;
    float fontSize = 7.f;
    NVGcolor color = nvgRGBA(205, 215, 235, 230);

    PanelLabel() { box.size = Vec(40.f, 12.f); }

    void draw(const DrawArgs& args) override {
        if (text.empty()) return;
        nvgFillColor(args.vg, color);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), nullptr);
        TransparentWidget::draw(args);
    }
};

// Centre a caption on a panel position given in millimetres.
inline PanelLabel* createPanelLabel(Vec posMm, const std::string& text,
                                    float fontSize = 7.f) {
    auto* label = new PanelLabel;
    label->text = text;
    label->fontSize = fontSize;
    label->box.size = mm2px(Vec(14.f, 4.f));
    label->box.pos = mm2px(posMm).minus(label->box.size.div(2));
    return label;
}

} // namespace WiggleRoom
