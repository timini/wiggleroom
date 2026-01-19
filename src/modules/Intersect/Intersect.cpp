/******************************************************************************
 * INTERSECT
 * Rhythmic trigger generator using discrete gradient analysis
 * Generates Euclidean and Euclidean-adjacent rhythms from CV signals
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include <atomic>
#include <array>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

// Constants
constexpr float TRIGGER_PULSE_DURATION = 1e-3f;  // 1ms trigger pulse
constexpr float TRIGGER_FLASH_DURATION = 0.1f;   // 100ms visual flash
constexpr float INTERNAL_CLOCK_FREQ = 2.0f;      // 120 BPM = 2 Hz
constexpr float DEFAULT_CLOCK_PERIOD = 0.5f;     // 120 BPM
constexpr float DENSITY_CV_SCALE = 3.2f;         // CV to density scaling
constexpr float SCHMITT_LOW = 0.1f;
constexpr float SCHMITT_HIGH = 1.0f;

struct Intersect : Module {
    enum ParamId {
        TIME_DIV_PARAM,
        DENSITY_PARAM,
        SCALE_PARAM,
        DENSITY_CV_PARAM,
        GATE_MODE_PARAM,
        EDGE_MODE_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        CV_INPUT,
        DENSITY_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        TRIG_OUTPUT,
        STEP_OUTPUT,
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
    dsp::PulseGenerator gatePulse;  // For gate mode output

    // Internal clock (when no external clock)
    float internalPhase = 0.0f;

    // Clock period tracking for multiplication
    float lastClockPeriod = DEFAULT_CLOCK_PERIOD;
    float timeSinceLastClock = 0.0f;
    int clockMultCounter = 0;

    // Effective sample period (for gate mode duration)
    float effectiveSamplePeriod = DEFAULT_CLOCK_PERIOD;

    // Instance-specific counters (NOT static!)
    int divCounter = 0;
    int historyCounter = 0;

    // Band detection state
    int lastBandIndex = -999;
    float currentSteppedVoltage = 0.0f;

    // Thread-safe visualization data (written by audio, read by UI)
    static const int BUFFER_SIZE = 128;
    std::array<float, BUFFER_SIZE> cvBuffer{};
    std::atomic<int> cvBufferWriteIndex{0};
    std::atomic<int> currentDivisions{4};
    std::atomic<int> displayBandIndex{-1};
    std::atomic<int> displayFlashBand{-1};
    std::atomic<float> displayFlashTime{-1.0f};

    // Time division options: /8, /4, /2, x1, x2, x3, x4, x8
    static constexpr float TIME_RATIOS[] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 8.0f};
    static constexpr int NUM_TIME_RATIOS = 8;

    Intersect() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configSwitch(TIME_DIV_PARAM, 0.0f, 7.0f, 3.0f, "Time Division",
            {"/8", "/4", "/2", "x1", "x2", "x3", "x4", "x8"});

        configParam(DENSITY_PARAM, 1.0f, 32.0f, 4.0f, "Density", " bands");
        paramQuantities[DENSITY_PARAM]->snapEnabled = true;

        configSwitch(SCALE_PARAM, 0.0f, 1.0f, 0.0f, "Scale", {"Bipolar (-5V/+5V)", "Unipolar (0V/10V)"});

        configParam(DENSITY_CV_PARAM, -1.0f, 1.0f, 0.0f, "Density CV", "%", 0.0f, 100.0f);

        configSwitch(GATE_MODE_PARAM, 0.0f, 1.0f, 0.0f, "Output Mode", {"Trigger", "Gate"});

        configSwitch(EDGE_MODE_PARAM, 0.0f, 2.0f, 1.0f, "Edge Mode", {"Rising", "Both", "Falling"});

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(CV_INPUT, "CV");
        configInput(DENSITY_CV_INPUT, "Density CV");

        configOutput(TRIG_OUTPUT, "Trigger");
        configOutput(STEP_OUTPUT, "Step");

        configLight(TRIG_LIGHT, "Trigger");

        // Initialize buffer
        cvBuffer.fill(0.5f);
    }

    void onReset() override {
        lastBandIndex = -999;
        currentSteppedVoltage = 0.0f;
        internalPhase = 0.0f;
        clockMultCounter = 0;
        timeSinceLastClock = 0.0f;
        effectiveSamplePeriod = DEFAULT_CLOCK_PERIOD;
        divCounter = 0;
        historyCounter = 0;
        cvBuffer.fill(0.5f);
        cvBufferWriteIndex.store(0);
        displayBandIndex.store(-1);
        displayFlashBand.store(-1);
        displayFlashTime.store(-1.0f);
    }

    // Helper: Normalize CV to 0.0-1.0 range
    float normalizeCV(float rawCV, bool isUnipolar) const {
        if (isUnipolar) {
            return DSP::clamp(rawCV / 10.0f, 0.0f, 1.0f);
        } else {
            return DSP::clamp((rawCV + 5.0f) / 10.0f, 0.0f, 1.0f);
        }
    }

    // Helper: Convert normalized value back to voltage
    float denormalizeCV(float normalized, bool isUnipolar) const {
        if (isUnipolar) {
            return normalized * 10.0f;
        } else {
            return normalized * 10.0f - 5.0f;
        }
    }

    void process(const ProcessArgs& args) override {
        // Handle reset
        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), SCHMITT_LOW, SCHMITT_HIGH)) {
            onReset();
        }

        // Get time ratio from knob
        int timeIndex = (int)std::round(params[TIME_DIV_PARAM].getValue());
        timeIndex = DSP::clamp(timeIndex, 0, NUM_TIME_RATIOS - 1);
        float timeRatio = TIME_RATIOS[timeIndex];

        bool shouldSample = false;

        // Clock handling
        if (inputs[CLOCK_INPUT].isConnected()) {
            // External clock
            timeSinceLastClock += args.sampleTime;

            if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), SCHMITT_LOW, SCHMITT_HIGH)) {
                lastClockPeriod = std::max(timeSinceLastClock, 0.001f);  // Prevent division by zero
                timeSinceLastClock = 0.0f;
                clockMultCounter = 0;

                // Calculate effective sample period based on time ratio
                effectiveSamplePeriod = lastClockPeriod / timeRatio;

                if (timeRatio <= 1.0f) {
                    // Division or x1
                    int divAmount = (int)(1.0f / timeRatio);
                    divCounter++;
                    if (divCounter >= divAmount) {
                        shouldSample = true;
                        divCounter = 0;
                    }
                } else {
                    // Multiplication: sample now and schedule more
                    shouldSample = true;
                    clockMultCounter = 1;
                }
            }

            // Handle clock multiplication (interstitial triggers)
            if (timeRatio > 1.0f && clockMultCounter > 0 && lastClockPeriod > 0.0f) {
                float subPeriod = lastClockPeriod / timeRatio;
                int expectedCount = (int)(timeSinceLastClock / subPeriod);
                if (expectedCount >= clockMultCounter && clockMultCounter < (int)timeRatio) {
                    shouldSample = true;
                    clockMultCounter++;
                }
            }
        } else {
            // Internal clock at 120 BPM, modified by time ratio
            float effectiveFreq = INTERNAL_CLOCK_FREQ * timeRatio;
            internalPhase += effectiveFreq * args.sampleTime;

            // Effective sample period for internal clock
            effectiveSamplePeriod = 1.0f / effectiveFreq;

            if (internalPhase >= 1.0f) {
                internalPhase -= std::floor(internalPhase);
                shouldSample = true;
            }
        }

        // Get current CV and normalize
        float rawCV = inputs[CV_INPUT].getVoltage();
        bool isUnipolar = params[SCALE_PARAM].getValue() > 0.5f;
        float normalizedCV = normalizeCV(rawCV, isUnipolar);

        // Update CV history for visualization (at ~60 FPS equivalent)
        int samplesPerUpdate = std::max(1, (int)(args.sampleRate / 60.0f / BUFFER_SIZE * 2));
        historyCounter++;
        if (historyCounter >= samplesPerUpdate) {
            historyCounter = 0;
            int writeIdx = cvBufferWriteIndex.load();
            cvBuffer[writeIdx] = normalizedCV;
            cvBufferWriteIndex.store((writeIdx + 1) % BUFFER_SIZE);
        }

        // Calculate divisions with CV modulation
        float densityBase = params[DENSITY_PARAM].getValue();
        float densityCV = inputs[DENSITY_CV_INPUT].getVoltage() * params[DENSITY_CV_PARAM].getValue();
        int divisions = DSP::clamp((int)(densityBase + densityCV * DENSITY_CV_SCALE), 1, 32);
        currentDivisions.store(divisions);

        // The main algorithm: sample on clock
        if (shouldSample) {
            // Calculate current band
            int currentBandIndex = (int)std::floor(normalizedCV * divisions);
            if (currentBandIndex >= divisions) currentBandIndex = divisions - 1;
            if (currentBandIndex < 0) currentBandIndex = 0;

            // Did we cross a line?
            if (currentBandIndex != lastBandIndex && lastBandIndex != -999) {
                // Check edge mode: 0=Rising, 1=Both, 2=Falling
                int edgeMode = (int)std::round(params[EDGE_MODE_PARAM].getValue());
                bool shouldTrigger = false;

                if (edgeMode == 0) {
                    // Rising only: trigger when moving to higher band
                    shouldTrigger = (currentBandIndex > lastBandIndex);
                } else if (edgeMode == 2) {
                    // Falling only: trigger when moving to lower band
                    shouldTrigger = (currentBandIndex < lastBandIndex);
                } else {
                    // Both: any band change triggers
                    shouldTrigger = true;
                }

                if (shouldTrigger) {
                    bool isGateMode = params[GATE_MODE_PARAM].getValue() > 0.5f;
                    if (isGateMode) {
                        // Gate mode: gate lasts for the effective sample period
                        gatePulse.trigger(effectiveSamplePeriod);
                    } else {
                        // Trigger mode: short 1ms pulse
                        triggerPulse.trigger(TRIGGER_PULSE_DURATION);
                    }
                    displayFlashBand.store(currentBandIndex);
                    displayFlashTime.store(0.0f);
                }
            }

            // Calculate stepped voltage (center of band)
            float bandCenter = ((float)currentBandIndex + 0.5f) / divisions;
            currentSteppedVoltage = denormalizeCV(bandCenter, isUnipolar);

            lastBandIndex = currentBandIndex;
            displayBandIndex.store(currentBandIndex);
        }

        // Update trigger flash timer
        float flashTime = displayFlashTime.load();
        if (flashTime >= 0.0f) {
            flashTime += args.sampleTime;
            if (flashTime > TRIGGER_FLASH_DURATION) {
                displayFlashTime.store(-1.0f);
                displayFlashBand.store(-1);
            } else {
                displayFlashTime.store(flashTime);
            }
        }

        // Process outputs
        bool trigPulse = triggerPulse.process(args.sampleTime);
        bool gateOutput = gatePulse.process(args.sampleTime);
        bool outputHigh = trigPulse || gateOutput;

        outputs[TRIG_OUTPUT].setVoltage(outputHigh ? 10.0f : 0.0f);
        outputs[STEP_OUTPUT].setVoltage(currentSteppedVoltage);

        lights[TRIG_LIGHT].setBrightness(outputHigh ? 1.0f : 0.0f);
    }
};

// Display widget for the oscilloscope-style visualization
struct IntersectDisplay : LightWidget {
    Intersect* module = nullptr;

    IntersectDisplay() {
        box.size = Vec(109.92f, 84.f);  // 8HP display area matching SVG
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        // Background
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(20, 20, 30));
        nvgFill(args.vg);

        if (!module) {
            nvgFillColor(args.vg, nvgRGB(60, 60, 80));
            nvgFontSize(args.vg, 10);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2, "INTERSECT", nullptr);
            return;
        }

        // Read atomic values once for consistency
        int divisions = module->currentDivisions.load();
        int bandIndex = module->displayBandIndex.load();
        int flashBand = module->displayFlashBand.load();
        float flashTime = module->displayFlashTime.load();

        if (divisions < 1) divisions = 1;
        float heightStep = box.size.y / divisions;

        // Draw active band highlight
        if (bandIndex >= 0 && bandIndex < divisions) {
            float bandTop = box.size.y - (bandIndex + 1) * heightStep;
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 0, bandTop, box.size.x, heightStep);

            if (flashBand == bandIndex && flashTime >= 0) {
                float flash = 1.0f - (flashTime / TRIGGER_FLASH_DURATION);
                nvgFillColor(args.vg, nvgRGBA(150, 100, 200, (int)(80 + 120 * flash)));
            } else {
                nvgFillColor(args.vg, nvgRGBA(100, 60, 150, 60));
            }
            nvgFill(args.vg);
        }

        // Draw grid lines
        nvgBeginPath(args.vg);
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStrokeColor(args.vg, nvgRGBA(80, 80, 100, 200));

        for (int i = 1; i < divisions; i++) {
            float y = box.size.y - (i * heightStep);
            nvgMoveTo(args.vg, 0, y);
            nvgLineTo(args.vg, box.size.x, y);
        }
        nvgStroke(args.vg);

        // Draw CV trace (copy data to local buffer for thread safety)
        int writeIdx = module->cvBufferWriteIndex.load();
        nvgBeginPath(args.vg);
        nvgStrokeWidth(args.vg, 1.5f);
        nvgStrokeColor(args.vg, nvgRGB(100, 200, 255));

        float xStep = box.size.x / (float)(Intersect::BUFFER_SIZE - 1);
        for (int i = 0; i < Intersect::BUFFER_SIZE; i++) {
            int bufIdx = (writeIdx + i) % Intersect::BUFFER_SIZE;
            float cv = module->cvBuffer[bufIdx];
            float x = i * xStep;
            float y = box.size.y - cv * box.size.y;

            if (i == 0) {
                nvgMoveTo(args.vg, x, y);
            } else {
                nvgLineTo(args.vg, x, y);
            }
        }
        nvgStroke(args.vg);

        // Draw trigger flash line
        if (flashBand >= 0 && flashBand < divisions && flashTime >= 0) {
            float flash = 1.0f - (flashTime / TRIGGER_FLASH_DURATION);
            float y = box.size.y - (flashBand + 1) * heightStep + heightStep / 2;

            nvgBeginPath(args.vg);
            nvgStrokeWidth(args.vg, 2.0f);
            nvgStrokeColor(args.vg, nvgRGBA(255, 200, 100, (int)(255 * flash)));
            nvgMoveTo(args.vg, 0, y);
            nvgLineTo(args.vg, box.size.x, y);
            nvgStroke(args.vg);
        }

        // Border
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStrokeColor(args.vg, nvgRGB(80, 80, 100));
        nvgStroke(args.vg);
    }
};

struct IntersectWidget : ModuleWidget {
    IntersectWidget(Intersect* module) {
        setModule(module);
        box.size = Vec(8 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/Intersect.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // 8HP layout: width = 121.92px, height = 380px
        float xLeft = 30.f;    // Left column center
        float xRight = 92.f;   // Right column center
        float xCenter = 60.96f; // Panel center

        // Display - positioned to match SVG placeholder
        IntersectDisplay* display = createWidget<IntersectDisplay>(Vec(6.f, 30.f));
        display->module = module;
        addChild(display);

        // Controls - positioned below display
        float yKnobs = 145.f;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(xLeft, yKnobs), module, Intersect::TIME_DIV_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(xRight, yKnobs), module, Intersect::DENSITY_PARAM));
        addParam(createParamCentered<Trimpot>(Vec(xRight, yKnobs + 25.f), module, Intersect::DENSITY_CV_PARAM));
        addParam(createParamCentered<CKSS>(Vec(xCenter, yKnobs + 40.f), module, Intersect::SCALE_PARAM));

        // Inputs
        float yInputs = 215.f;
        addInput(createInputCentered<PJ301MPort>(Vec(xLeft, yInputs), module, Intersect::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xRight, yInputs), module, Intersect::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xLeft, yInputs + 45.f), module, Intersect::CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xRight, yInputs + 45.f), module, Intersect::DENSITY_CV_INPUT));

        // Mode switches (between inputs and outputs)
        float yModeSwitch = 290.f;
        addParam(createParamCentered<CKSS>(Vec(xLeft, yModeSwitch), module, Intersect::GATE_MODE_PARAM));
        addParam(createParamCentered<CKSSThree>(Vec(xRight, yModeSwitch), module, Intersect::EDGE_MODE_PARAM));

        // Outputs
        float yOutputs = 325.f;
        addOutput(createOutputCentered<PJ301MPort>(Vec(xLeft, yOutputs), module, Intersect::TRIG_OUTPUT));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(xLeft + 12.f, yOutputs - 12.f), module, Intersect::TRIG_LIGHT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xRight, yOutputs), module, Intersect::STEP_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelIntersect = createModel<WiggleRoom::Intersect, WiggleRoom::IntersectWidget>("Intersect");
