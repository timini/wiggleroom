#pragma once

// NOTE: This file is included inside ACID9Seq.cpp, after ACID9Seq is defined
// and inside the WiggleRoom namespace. Do NOT add namespace wrapper here.

#include <cmath>
#include <string>

/**
 * PlanetaryDisplay - Visualizes the two gears of the Interference Engine
 *
 * Layout:
 * - Outer ring: 16 LEDs for Gear A (pitch)
 * - Middle ring: Variable LEDs for Gear B (offset)
 * - Center: Current note name with optional animation
 */
struct PlanetaryDisplay : rack::widget::Widget {
    ACID9Seq* module = nullptr;

    // Colors
    NVGcolor bgColor = nvgRGB(0x10, 0x10, 0x18);
    NVGcolor gearAColor = nvgRGB(0x00, 0x80, 0xff);       // Blue for Gear A
    NVGcolor gearAActiveColor = nvgRGB(0x40, 0xff, 0xff); // Bright cyan when active
    NVGcolor gearBColor = nvgRGB(0xff, 0x40, 0x80);       // Pink for Gear B
    NVGcolor gearBActiveColor = nvgRGB(0xff, 0x80, 0xff); // Bright magenta when active
    NVGcolor textColor = nvgRGB(0xff, 0xff, 0xff);
    NVGcolor alignFlashColor = nvgRGB(0xff, 0xff, 0xff);

    // Note names
    static constexpr const char* NOTE_NAMES[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    // Animation state
    float flashAlpha = 0.0f;
    bool wasAligned = false;

    PlanetaryDisplay() {
        box.size = rack::Vec(100, 100);
    }

    void draw(const DrawArgs& args) override {
        // Background circle
        float cx = box.size.x / 2;
        float cy = box.size.y / 2;
        float outerRadius = std::min(cx, cy) - 4;

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, outerRadius);
        nvgFillColor(args.vg, bgColor);
        nvgFill(args.vg);

        // Outer ring border
        nvgStrokeColor(args.vg, nvgRGBA(0x60, 0x60, 0x80, 0x80));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        float cx = box.size.x / 2;
        float cy = box.size.y / 2;
        float outerRadius = std::min(cx, cy) - 4;

        if (!module) {
            // Preview mode - draw placeholder
            drawPreview(args, cx, cy, outerRadius);
            return;
        }

        // Get gear data from module
        int gearAPos = getGearAPosition();
        int gearALen = 16;
        int gearBPos = getGearBPosition();
        int gearBLen = getGearBLength();
        int quantizedPitch = getQuantizedPitch();

        // Check for alignment (both playheads at position 0)
        bool aligned = (gearAPos == 0 && gearBPos == 0);
        if (aligned && !wasAligned) {
            flashAlpha = 1.0f;
        }
        wasAligned = aligned;

        // Decay flash
        flashAlpha *= 0.95f;

        // Draw Gear A ring (outer)
        float gearARadius = outerRadius - 5;
        drawGearRing(args, cx, cy, gearARadius, gearALen, gearAPos,
                     gearAColor, gearAActiveColor, true);

        // Draw Gear B ring (middle)
        float gearBRadius = outerRadius - 18;
        drawGearRing(args, cx, cy, gearBRadius, gearBLen, gearBPos,
                     gearBColor, gearBActiveColor, false);

        // Draw center note display
        drawNoteDisplay(args, cx, cy, quantizedPitch);

        // Draw alignment flash
        if (flashAlpha > 0.01f) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, cx, cy, outerRadius);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, (int)(flashAlpha * 60)));
            nvgFill(args.vg);
        }

        Widget::drawLayer(args, layer);
    }

private:
    void drawPreview(const DrawArgs& args, float cx, float cy, float radius) {
        // Draw some placeholder dots
        drawGearRing(args, cx, cy, radius - 5, 16, 0,
                     gearAColor, gearAActiveColor, true);
        drawGearRing(args, cx, cy, radius - 18, 7, 0,
                     gearBColor, gearBActiveColor, false);

        // Draw note name
        nvgFontSize(args.vg, 16);
        nvgFillColor(args.vg, textColor);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, cx, cy, "C4", nullptr);
    }

    void drawGearRing(const DrawArgs& args, float cx, float cy, float radius,
                      int steps, int activeStep, NVGcolor color, NVGcolor activeColor,
                      bool showPitch) {
        float dotRadius = showPitch ? 3.5f : 3.0f;

        for (int i = 0; i < steps; i++) {
            // Calculate angle (start from top, go clockwise)
            float angle = -M_PI / 2 + (2 * M_PI * i / steps);
            float x = cx + radius * cos(angle);
            float y = cy + radius * sin(angle);

            bool isActive = (i == activeStep);

            nvgBeginPath(args.vg);
            nvgCircle(args.vg, x, y, isActive ? dotRadius + 1 : dotRadius);

            if (isActive) {
                nvgFillColor(args.vg, activeColor);
            } else {
                nvgFillColor(args.vg, nvgRGBA(color.r * 255, color.g * 255,
                                               color.b * 255, 120));
            }
            nvgFill(args.vg);

            // Add glow for active step
            if (isActive) {
                nvgBeginPath(args.vg);
                nvgCircle(args.vg, x, y, dotRadius + 4);
                NVGpaint glow = nvgRadialGradient(args.vg, x, y, dotRadius,
                                                   dotRadius + 6, activeColor,
                                                   nvgRGBA(0, 0, 0, 0));
                nvgFillPaint(args.vg, glow);
                nvgFill(args.vg);
            }
        }
    }

    void drawNoteDisplay(const DrawArgs& args, float cx, float cy, int pitch) {
        // Extract note and octave
        int note = pitch % 12;
        int octave = (pitch / 12) + 2;  // Offset so 12 = C4

        const char* noteName = NOTE_NAMES[note];
        char buf[8];
        snprintf(buf, sizeof(buf), "%s%d", noteName, octave);

        // Draw note text
        nvgFontSize(args.vg, 14);
        nvgFillColor(args.vg, textColor);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, cx, cy, buf, nullptr);
    }

    // These functions will be implemented to access module state
    // They're declared here and defined in ACID9Seq.cpp after the module is defined
    int getGearAPosition();
    int getGearBPosition();
    int getGearBLength();
    int getQuantizedPitch();
};

// Static definition for NOTE_NAMES
constexpr const char* PlanetaryDisplay::NOTE_NAMES[];
