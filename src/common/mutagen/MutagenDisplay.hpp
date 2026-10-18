#pragma once
/******************************************************************************
 * MUTAGEN DISPLAY
 * Vertical step editor: steps run top to bottom, value is bar length.
 *
 * Adapted from EucSeq's CVStepDisplay, which is horizontal. Running the
 * steps down the panel instead of across is what lets these modules be 6HP
 * and 4HP rather than 20HP.
 *
 * OpaqueWidget + draw(), not LightWidget + drawLayer(): only an
 * OpaqueWidget takes mouse input, which is what makes the bars editable.
 ******************************************************************************/

#include "rack.hpp"

#include "DSP.hpp"
#include "mutagen/MutagenLane.hpp"

using namespace rack;

namespace WiggleRoom {

struct MutagenDisplay : OpaqueWidget {
    MutagenLane* module = nullptr;

    MutagenDisplay() { box.size = Vec(60.f, 200.f); }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 180));
        nvgFill(args.vg);

        if (!module) {
            nvgFillColor(args.vg, nvgRGBA(80, 100, 140, 200));
            nvgFontSize(args.vg, 9);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2, "MUTAGEN", nullptr);
            OpaqueWidget::draw(args);
            return;
        }

        int steps = module->seq.steps;
        if (steps < 1) steps = 1;
        int currentStep = module->displayStep();

        float rowH = box.size.y / static_cast<float>(steps);
        float usableW = box.size.x - 2.f;

        for (int s = 0; s < steps; s++) {
            float val = module->seq.values[s];
            float y = s * rowH;
            float barW = val * usableW;

            // Bar grows left to right, so no inversion is needed.
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 1.f, y + 0.5f, barW, rowH - 1.f);
            nvgFillColor(args.vg, nvgRGBA(80, 160, 220, 230));
            nvgFill(args.vg);

            if (s == currentStep) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 1.f, y + 0.5f, usableW, rowH - 1.f);
                nvgStrokeColor(args.vg, nvgRGBA(255, 220, 80, 255));
                nvgStrokeWidth(args.vg, 2.f);
                nvgStroke(args.vg);
            }
        }

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(args.vg, nvgRGBA(80, 100, 140, 150));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);

        OpaqueWidget::draw(args);
    }

    void onDragHover(const event::DragHover& e) override {
        if (module && e.origin == this) setValueFromPos(e.pos, false);
        OpaqueWidget::onDragHover(e);
    }

    void onButton(const event::Button& e) override {
        if (!module) return;
        if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        // Only the gesture start goes on the undo stack, so dragging across
        // the screen undoes as one edit rather than dozens.
        setValueFromPos(e.pos, true);
        e.consume(this);
    }

    void onDragStart(const event::DragStart& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) e.consume(this);
    }

    void setValueFromPos(Vec pos, bool recordUndo) {
        int steps = module->seq.steps;
        if (steps <= 0) return;

        float rowH = box.size.y / static_cast<float>(steps);
        int stepIdx = static_cast<int>(pos.y / rowH);
        stepIdx = DSP::clamp(stepIdx, 0, steps - 1);

        float val = pos.x / box.size.x;
        val = DSP::clamp(val, 0.f, 1.f);

        if (recordUndo) {
            module->seq.setValue(stepIdx, val);
        } else {
            if (stepIdx >= 0 && stepIdx < StepSequence::MAX_STEPS)
                module->seq.values[stepIdx] = val;
        }
    }
};

} // namespace WiggleRoom
