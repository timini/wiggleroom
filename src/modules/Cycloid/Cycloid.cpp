/******************************************************************************
 * CYCLOID
 * Polar Euclidean Sequencer with Spirograph LFO
 * Rotating clock hand with warp/morph creates complex rhythmic patterns
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include <atomic>
#include <cmath>
#include <array>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

// Constants
constexpr float TRIGGER_PULSE_DURATION = 1e-3f;  // 1ms trigger pulse
constexpr float TRIGGER_FLASH_DURATION = 0.15f;  // 150ms visual flash
constexpr float INTERNAL_CLOCK_FREQ = 2.0f;      // 120 BPM = 2 Hz
constexpr float DEFAULT_CLOCK_PERIOD = 0.5f;     // 120 BPM
constexpr float SCHMITT_LOW = 0.1f;
constexpr float SCHMITT_HIGH = 1.0f;
constexpr int MAX_DIVISIONS = 32;

// Euclidean rhythm algorithm (Bjorklund)
inline bool euclideanHit(int step, int hits, int divisions) {
    if (hits >= divisions) return true;
    if (hits <= 0) return false;
    return ((step * hits) % divisions) < hits;
}

struct Cycloid : Module {
    enum ParamId {
        SPEED_PARAM,
        DIVISIONS_PARAM,
        HITS_PARAM,
        OFFSET_PARAM,
        RATIO_PARAM,
        DEPTH_PARAM,
        HITS_CV_PARAM,
        DEPTH_CV_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        HITS_CV_INPUT,
        DEPTH_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        TRIG_OUTPUT,
        X_OUTPUT,
        Y_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        TRIG_LIGHT,
        LIGHTS_LEN
    };

    // Clock detection
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::PulseGenerator triggerPulse;

    // Master clock phase (advances by 1.0 per clock pulse)
    float masterPhase = 0.0f;

    // LFO phases
    float mainPhase = 0.0f;   // Primary rotation (0.0 to 1.0)
    float modPhase = 0.0f;    // Secondary/warp rotation

    // Calculated hand position (for display and output)
    float handX = 0.0f;
    float handY = 0.0f;
    float handAngle = 0.0f;   // Resulting angle after warp

    // Clock period tracking for smooth interpolation
    float lastClockPeriod = DEFAULT_CLOCK_PERIOD;
    float timeSinceLastClock = 0.0f;
    float masterPhaseAtLastClock = 0.0f;

    // Slice detection state
    int lastSlice = -1;

    // Thread-safe visualization data
    std::atomic<float> displayMainPhase{0.0f};
    std::atomic<float> displayModPhase{0.0f};
    std::atomic<float> displayHandAngle{0.0f};
    std::atomic<float> displayDepth{0.0f};
    std::atomic<int> displayDivisions{8};
    std::atomic<int> displayHits{8};
    std::atomic<float> displayOffset{0.0f};
    std::atomic<float> displayFlashTime{-1.0f};
    std::atomic<int> displayFlashSlice{-1};

    // Cached Euclidean pattern for display (simple array, not atomic - ok for display)
    bool euclideanPattern[MAX_DIVISIONS] = {false};

    // Speed ratios: /8, /4, /2, x1, x2, x4, x8
    static constexpr float SPEED_RATIOS[] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
    static constexpr int NUM_SPEED_RATIOS = 7;

    // Warp ratios for secondary LFO
    static constexpr float WARP_RATIOS[] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    static constexpr int NUM_WARP_RATIOS = 10;

    Cycloid() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configSwitch(SPEED_PARAM, 0.0f, 6.0f, 3.0f, "Speed",
            {"/8", "/4", "/2", "x1", "x2", "x4", "x8"});

        configParam(DIVISIONS_PARAM, 1.0f, 32.0f, 8.0f, "Divisions", " steps");
        paramQuantities[DIVISIONS_PARAM]->snapEnabled = true;

        configParam(HITS_PARAM, 1.0f, 32.0f, 4.0f, "Hits", " triggers");
        paramQuantities[HITS_PARAM]->snapEnabled = true;

        configParam(OFFSET_PARAM, 0.0f, 1.0f, 0.0f, "Offset", " turns", 0.0f, 1.0f);

        configSwitch(RATIO_PARAM, 0.0f, 9.0f, 3.0f, "Warp Ratio",
            {"1:2", "1:1", "3:2", "2:1", "3:1", "4:1", "5:1", "6:1", "7:1", "8:1"});

        configParam(DEPTH_PARAM, 0.0f, 1.0f, 0.0f, "Warp Depth", "%", 0.0f, 100.0f);

        configParam(HITS_CV_PARAM, -1.0f, 1.0f, 0.0f, "Hits CV", "%", 0.0f, 100.0f);
        configParam(DEPTH_CV_PARAM, -1.0f, 1.0f, 0.0f, "Depth CV", "%", 0.0f, 100.0f);

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(HITS_CV_INPUT, "Hits CV");
        configInput(DEPTH_CV_INPUT, "Depth CV");

        configOutput(TRIG_OUTPUT, "Trigger");
        configOutput(X_OUTPUT, "LFO X");
        configOutput(Y_OUTPUT, "LFO Y");

        configLight(TRIG_LIGHT, "Trigger");

        for (int i = 0; i < MAX_DIVISIONS; i++) {
            euclideanPattern[i] = false;
        }
    }

    void onReset() override {
        masterPhase = 0.0f;
        mainPhase = 0.0f;
        modPhase = 0.0f;
        lastSlice = -1;
        timeSinceLastClock = 0.0f;
        lastClockPeriod = DEFAULT_CLOCK_PERIOD;
        masterPhaseAtLastClock = 0.0f;
        displayFlashTime.store(-1.0f);
        displayFlashSlice.store(-1);
    }

    void process(const ProcessArgs& args) override {
        // Handle reset
        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), SCHMITT_LOW, SCHMITT_HIGH)) {
            onReset();
        }

        // Get speed ratio from knob
        int speedIndex = (int)std::round(params[SPEED_PARAM].getValue());
        speedIndex = DSP::clamp(speedIndex, 0, NUM_SPEED_RATIOS - 1);
        float speedRatio = SPEED_RATIOS[speedIndex];

        // Get warp ratio
        int ratioIndex = (int)std::round(params[RATIO_PARAM].getValue());
        ratioIndex = DSP::clamp(ratioIndex, 0, NUM_WARP_RATIOS - 1);
        float warpRatio = WARP_RATIOS[ratioIndex];

        // Get depth with CV modulation
        float depthBase = params[DEPTH_PARAM].getValue();
        float depthCV = inputs[DEPTH_CV_INPUT].getVoltage() / 10.0f * params[DEPTH_CV_PARAM].getValue();
        float depth = DSP::clamp(depthBase + depthCV, 0.0f, 1.0f);

        // Get divisions
        int divisions = (int)std::round(params[DIVISIONS_PARAM].getValue());
        divisions = DSP::clamp(divisions, 1, MAX_DIVISIONS);

        // Calculate hits with CV modulation
        float hitsBase = params[HITS_PARAM].getValue();
        float hitsCV = inputs[HITS_CV_INPUT].getVoltage() * params[HITS_CV_PARAM].getValue() * 3.2f;
        int hits = DSP::clamp((int)std::round(hitsBase + hitsCV), 1, divisions);

        float offset = params[OFFSET_PARAM].getValue();

        // Update Euclidean pattern cache
        for (int i = 0; i < divisions; i++) {
            euclideanPattern[i] = euclideanHit(i, hits, divisions);
        }

        // Update display values
        displayDivisions.store(divisions);
        displayHits.store(hits);
        displayOffset.store(offset);
        displayDepth.store(depth);

        // Clock handling - master phase is locked to clock
        timeSinceLastClock += args.sampleTime;

        float currentMasterPhase;
        if (inputs[CLOCK_INPUT].isConnected()) {
            if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), SCHMITT_LOW, SCHMITT_HIGH)) {
                lastClockPeriod = std::max(timeSinceLastClock, 0.001f);
                timeSinceLastClock = 0.0f;
                masterPhase += 1.0f;
                masterPhaseAtLastClock = masterPhase;
            }
            currentMasterPhase = masterPhaseAtLastClock + (timeSinceLastClock / lastClockPeriod);
        } else {
            float masterIncrement = INTERNAL_CLOCK_FREQ * args.sampleTime;
            masterPhase += masterIncrement;
            currentMasterPhase = masterPhase;
        }

        // Calculate main and mod phases
        mainPhase = currentMasterPhase * speedRatio;
        modPhase = currentMasterPhase * speedRatio * warpRatio;

        // Wrap phases
        mainPhase = mainPhase - std::floor(mainPhase);
        modPhase = modPhase - std::floor(modPhase);

        // Spirograph calculation
        // Main circle + secondary circle (epicycle)
        float t1 = mainPhase * DSP::TWO_PI;
        float t2 = modPhase * DSP::TWO_PI;

        // Vector addition: main rotation + depth-scaled secondary rotation
        handX = std::cos(t1) + depth * std::cos(t2);
        handY = std::sin(t1) + depth * std::sin(t2);

        // Calculate resulting angle (for slice detection)
        handAngle = std::atan2(handY, handX);

        // Normalize angle to 0.0-1.0
        float normalizedAngle = (handAngle + DSP::PI) / DSP::TWO_PI;

        // Apply offset
        float adjustedAngle = normalizedAngle + offset;
        if (adjustedAngle >= 1.0f) adjustedAngle -= 1.0f;

        // Update display
        displayMainPhase.store(mainPhase);
        displayModPhase.store(modPhase);
        displayHandAngle.store(normalizedAngle);

        // Calculate current slice
        int currentSlice = (int)std::floor(adjustedAngle * divisions);
        if (currentSlice >= divisions) currentSlice = divisions - 1;
        if (currentSlice < 0) currentSlice = 0;

        // Detect slice crossing - only trigger on hits
        if (currentSlice != lastSlice && lastSlice >= 0) {
            bool isHit = euclideanHit(currentSlice, hits, divisions);
            if (isHit) {
                triggerPulse.trigger(TRIGGER_PULSE_DURATION);
                displayFlashTime.store(0.0f);
                displayFlashSlice.store(currentSlice);
            }
        }

        lastSlice = currentSlice;

        // Update trigger flash timer
        float flashTime = displayFlashTime.load();
        if (flashTime >= 0.0f) {
            flashTime += args.sampleTime;
            if (flashTime > TRIGGER_FLASH_DURATION) {
                displayFlashTime.store(-1.0f);
                displayFlashSlice.store(-1);
            } else {
                displayFlashTime.store(flashTime);
            }
        }

        // Process outputs
        bool trigActive = triggerPulse.process(args.sampleTime);
        outputs[TRIG_OUTPUT].setVoltage(trigActive ? 10.0f : 0.0f);
        lights[TRIG_LIGHT].setBrightness(trigActive ? 1.0f : 0.0f);

        // LFO X/Y outputs (scaled to ±5V)
        // Normalize by (1 + depth) to keep within range
        float scale = 5.0f / (1.0f + depth);
        outputs[X_OUTPUT].setVoltage(handX * scale);
        outputs[Y_OUTPUT].setVoltage(handY * scale);
    }
};

// Radar display widget
struct CycloidDisplay : LightWidget {
    Cycloid* module = nullptr;

    // Trail history for spirograph visualization
    static constexpr int TRAIL_LENGTH = 64;
    float trailX[TRAIL_LENGTH] = {0};
    float trailY[TRAIL_LENGTH] = {0};
    int trailIndex = 0;
    float lastMainPhase = -1.0f;

    CycloidDisplay() {
        box.size = Vec(120.f, 120.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        float cx = box.size.x / 2.0f;
        float cy = box.size.y / 2.0f;
        float radius = std::min(cx, cy) - 8.0f;

        // Background circle (translucent)
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, radius + 4.0f);
        nvgFillColor(args.vg, nvgRGBA(15, 15, 25, 180));
        nvgFill(args.vg);

        if (!module) {
            // Preview mode
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, cx, cy, radius);
            nvgStrokeWidth(args.vg, 1.5f);
            nvgStrokeColor(args.vg, nvgRGBA(60, 60, 80, 200));
            nvgStroke(args.vg);

            nvgFillColor(args.vg, nvgRGB(60, 60, 80));
            nvgFontSize(args.vg, 12);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, cx, cy, "CYCLOID", nullptr);
            return;
        }

        // Read atomic values
        float mainPhase = module->displayMainPhase.load();
        float modPhase = module->displayModPhase.load();
        float depth = module->displayDepth.load();
        int divisions = module->displayDivisions.load();
        int hits = module->displayHits.load();
        float offset = module->displayOffset.load();
        float flashTime = module->displayFlashTime.load();
        int flashSlice = module->displayFlashSlice.load();

        if (divisions < 1) divisions = 1;
        if (hits < 1) hits = 1;
        if (hits > divisions) hits = divisions;

        // Update trail if phase changed significantly
        if (std::abs(mainPhase - lastMainPhase) > 0.005f || mainPhase < lastMainPhase) {
            float t1 = mainPhase * DSP::TWO_PI;
            float t2 = modPhase * DSP::TWO_PI;
            trailX[trailIndex] = std::cos(t1) + depth * std::cos(t2);
            trailY[trailIndex] = std::sin(t1) + depth * std::sin(t2);
            trailIndex = (trailIndex + 1) % TRAIL_LENGTH;
            lastMainPhase = mainPhase;
        }

        // Draw spirograph trail (if depth > 0)
        if (depth > 0.01f) {
            nvgBeginPath(args.vg);
            float scale = radius * 0.45f / (1.0f + depth);
            bool first = true;
            for (int i = 0; i < TRAIL_LENGTH; i++) {
                int idx = (trailIndex + i) % TRAIL_LENGTH;
                float x = cx + trailX[idx] * scale;
                float y = cy + trailY[idx] * scale;
                if (first) {
                    nvgMoveTo(args.vg, x, y);
                    first = false;
                } else {
                    nvgLineTo(args.vg, x, y);
                }
            }
            nvgStrokeWidth(args.vg, 1.0f);
            nvgStrokeColor(args.vg, nvgRGBA(0, 150, 200, 80));
            nvgStroke(args.vg);
        }

        // Draw division spokes (dim)
        for (int i = 0; i < divisions; i++) {
            float angle = ((float)i / divisions + offset) * DSP::TWO_PI - DSP::PI / 2.0f;
            float x2 = cx + std::cos(angle) * radius;
            float y2 = cy + std::sin(angle) * radius;

            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, cx, cy);
            nvgLineTo(args.vg, x2, y2);
            nvgStrokeWidth(args.vg, 1.0f);
            nvgStrokeColor(args.vg, nvgRGBA(40, 40, 60, 120));
            nvgStroke(args.vg);
        }

        // Draw polygon outline (dim)
        nvgBeginPath(args.vg);
        for (int i = 0; i <= divisions; i++) {
            float angle = ((float)(i % divisions) / divisions + offset) * DSP::TWO_PI - DSP::PI / 2.0f;
            float x = cx + std::cos(angle) * radius;
            float y = cy + std::sin(angle) * radius;
            if (i == 0) {
                nvgMoveTo(args.vg, x, y);
            } else {
                nvgLineTo(args.vg, x, y);
            }
        }
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStrokeColor(args.vg, nvgRGBA(50, 50, 80, 150));
        nvgStroke(args.vg);

        // Draw all division vertices (small, dim)
        for (int i = 0; i < divisions; i++) {
            float angle = ((float)i / divisions + offset) * DSP::TWO_PI - DSP::PI / 2.0f;
            float x = cx + std::cos(angle) * radius;
            float y = cy + std::sin(angle) * radius;

            nvgBeginPath(args.vg);
            nvgCircle(args.vg, x, y, 2.0f);
            nvgFillColor(args.vg, nvgRGBA(60, 60, 90, 200));
            nvgFill(args.vg);
        }

        // Draw hit vertices (larger, brighter)
        for (int i = 0; i < divisions; i++) {
            bool isHit = module->euclideanPattern[i];
            if (!isHit) continue;

            float angle = ((float)i / divisions + offset) * DSP::TWO_PI - DSP::PI / 2.0f;
            float x = cx + std::cos(angle) * radius;
            float y = cy + std::sin(angle) * radius;

            bool isFlashing = (flashSlice == i && flashTime >= 0.0f);

            // Draw spoke for hits
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, cx, cy);
            nvgLineTo(args.vg, x, y);

            if (isFlashing) {
                float flash = 1.0f - (flashTime / TRIGGER_FLASH_DURATION);
                nvgStrokeColor(args.vg, nvgRGBA(255, 200, 80, (int)(255 * flash)));
                nvgStrokeWidth(args.vg, 3.0f);
            } else {
                nvgStrokeColor(args.vg, nvgRGBA(255, 100, 150, 180));
                nvgStrokeWidth(args.vg, 1.5f);
            }
            nvgStroke(args.vg);

            // Draw hit vertex
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, x, y, 4.0f);

            if (isFlashing) {
                float flash = 1.0f - (flashTime / TRIGGER_FLASH_DURATION);
                nvgFillColor(args.vg, nvgRGBA(255, 200, 80, (int)(200 + 55 * flash)));
            } else {
                nvgFillColor(args.vg, nvgRGBA(255, 100, 150, 255));
            }
            nvgFill(args.vg);
        }

        // Calculate hand position with spirograph
        float t1 = mainPhase * DSP::TWO_PI;
        float t2 = modPhase * DSP::TWO_PI;
        float hx = std::cos(t1) + depth * std::cos(t2);
        float hy = std::sin(t1) + depth * std::sin(t2);

        // Scale and rotate to display coordinates
        float handScale = radius * 0.45f / (1.0f + depth);
        float displayAngle = std::atan2(hy, hx) + offset * DSP::TWO_PI - DSP::PI / 2.0f;
        float handLen = std::sqrt(hx * hx + hy * hy) * handScale;

        float handX = cx + std::cos(displayAngle) * handLen * 2.0f;
        float handY = cy + std::sin(displayAngle) * handLen * 2.0f;

        // Hand glow
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx, cy);
        nvgLineTo(args.vg, handX, handY);
        nvgStrokeWidth(args.vg, 4.0f);
        nvgStrokeColor(args.vg, nvgRGBA(0, 200, 255, 60));
        nvgStroke(args.vg);

        // Hand line
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx, cy);
        nvgLineTo(args.vg, handX, handY);
        nvgStrokeWidth(args.vg, 2.0f);
        nvgStrokeColor(args.vg, nvgRGB(0, 220, 255));
        nvgStroke(args.vg);

        // Hand tip dot
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, handX, handY, 4.0f);
        nvgFillColor(args.vg, nvgRGB(0, 255, 255));
        nvgFill(args.vg);

        // Center dot
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, 3.0f);
        nvgFillColor(args.vg, nvgRGB(150, 150, 180));
        nvgFill(args.vg);

        // Outer ring
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, radius + 2.0f);
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStrokeColor(args.vg, nvgRGBA(80, 80, 100, 150));
        nvgStroke(args.vg);
    }
};

struct CycloidWidget : ModuleWidget {
    CycloidWidget(Cycloid* module) {
        setModule(module);
        box.size = Vec(12 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/Cycloid.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // 12HP layout: width = 182.88px
        float xCenter = box.size.x / 2.0f;
        float xLeft = 28.f;
        float xMidLeft = 62.f;
        float xMidRight = box.size.x - 62.f;
        float xRight = box.size.x - 28.f;

        // Display - centered at top
        CycloidDisplay* display = createWidget<CycloidDisplay>(Vec(xCenter - 60.f, 58.f));
        display->module = module;
        addChild(display);

        // Row 1: Speed, Divisions
        float yRow1 = 162.f;
        addParam(createParamCentered<RoundBlackKnob>(Vec(xLeft, yRow1), module, Cycloid::SPEED_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(xRight, yRow1), module, Cycloid::DIVISIONS_PARAM));

        // Row 2: Hits, Offset
        float yRow2 = 202.f;
        addParam(createParamCentered<RoundBlackKnob>(Vec(xLeft, yRow2), module, Cycloid::HITS_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(xRight, yRow2), module, Cycloid::OFFSET_PARAM));

        // Row 3: Ratio, Depth
        float yRow3 = 242.f;
        addParam(createParamCentered<RoundBlackKnob>(Vec(xLeft, yRow3), module, Cycloid::RATIO_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(xRight, yRow3), module, Cycloid::DEPTH_PARAM));

        // CV attenuverters (between main knobs)
        addParam(createParamCentered<Trimpot>(Vec(xMidLeft, yRow2), module, Cycloid::HITS_CV_PARAM));
        addParam(createParamCentered<Trimpot>(Vec(xMidRight, yRow3), module, Cycloid::DEPTH_CV_PARAM));

        // Inputs row
        float yInputs = 282.f;
        addInput(createInputCentered<PJ301MPort>(Vec(xLeft, yInputs), module, Cycloid::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xMidLeft, yInputs), module, Cycloid::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xMidRight, yInputs), module, Cycloid::HITS_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xRight, yInputs), module, Cycloid::DEPTH_CV_INPUT));

        // Outputs row
        float yOutputs = 330.f;
        addOutput(createOutputCentered<PJ301MPort>(Vec(xLeft, yOutputs), module, Cycloid::X_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xCenter, yOutputs), module, Cycloid::TRIG_OUTPUT));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(xCenter + 15.f, yOutputs - 10.f), module, Cycloid::TRIG_LIGHT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xRight, yOutputs), module, Cycloid::Y_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelCycloid = createModel<WiggleRoom::Cycloid, WiggleRoom::CycloidWidget>("Cycloid");
