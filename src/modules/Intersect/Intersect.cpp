/******************************************************************************
 * INTERSECT
 * Rhythmic trigger generator using discrete gradient analysis
 * Generates Euclidean and Euclidean-adjacent rhythms from CV signals
 * A separate CV input is sampled and held on each output event, quantised
 * to the scale bus when one is patched
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include "mutagen/MutagenMessage.hpp"
#include "mutagen/MutagenModels.hpp"
#include <atomic>
#include <array>
#include <cmath>
#include <limits>

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
        SWING_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        CV_INPUT,
        DENSITY_CV_INPUT,
        SCALE_BUS_INPUT,
        SH_CV_INPUT,
        RUN_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        TRIG_OUTPUT,
        STEP_OUTPUT,
        SH_OUTPUT,
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

    // Peak tracking between samples to prevent aliasing
    float peakMinCV = 1.f;   // min normalized CV since last sample
    float peakMaxCV = 0.f;   // max normalized CV since last sample

    // Scale bus: 12 polyphonic channels of scale mask, in absolute chromatic
    // positions. Channel 16 carries the root, which we do not need: the
    // producer has already applied it to the mask.
    int scaleMask = 0xFFF;  // chromatic by default
    bool scaleConnected = false;

    // S&H CV, latched on the same event that fires the output
    float shHeldVoltage = 0.0f;

    // Swing: every second sample is held back toward the next one
    bool swingLate = false;
    float pendingSwingTimer = 0.0f;

    // Transport published to a Mutagen chain on the right
    WiggleRoom::MutagenExpanderMessage rightMessages[2];

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

        configParam(SWING_PARAM, 50.0f, 75.0f, 50.0f, "Swing", "%");

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(CV_INPUT, "CV");
        configInput(DENSITY_CV_INPUT, "Density CV");
        configInput(SCALE_BUS_INPUT, "Scale bus");
        configInput(SH_CV_INPUT, "S&H CV (normalled to CV)");
        configInput(RUN_INPUT, "Run");

        configOutput(TRIG_OUTPUT, "Trigger");
        configOutput(STEP_OUTPUT, "Step");
        configOutput(SH_OUTPUT, "S&H CV");

        configLight(TRIG_LIGHT, "Trigger");

        // Initialize buffer
        cvBuffer.fill(0.5f);

        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];
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
        peakMinCV = 1.f;
        peakMaxCV = 0.f;
        shHeldVoltage = 0.0f;
        swingLate = false;
        pendingSwingTimer = 0.0f;
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

    // Snap a V/oct voltage to the nearest note allowed by the scale bus.
    // Passes the voltage through untouched when no bus is patched.
    //
    // Bus channels 0-11 are ABSOLUTE chromatic positions: the producer has
    // already rotated the mask to its root (see TheArchitect.cpp:361). Indexing
    // by (chroma - root) would transpose it a second time and quantise to the
    // wrong scale, so the mask is indexed by absolute chroma here.
    float quantizeToScale(float voltage) const {
        if (!scaleConnected) return voltage;

        // Compare candidates against the UNROUNDED pitch. Rounding first and
        // then searching outward loses which neighbour is genuinely closer:
        // at MIDI 61.4 with only C and D allowed, it would round to C# and pick
        // C at 1.4 semitones over D at 0.6.
        float exact = voltage * 12.f + 60.f;
        int base = static_cast<int>(std::floor(exact));

        // A full octave either side always contains every set bit in the mask,
        // since it repeats every 12 semitones.
        int best = 0;
        float bestDist = std::numeric_limits<float>::max();
        for (int cand = base - 12; cand <= base + 13; cand++) {
            if (!noteInScale(cand)) continue;
            float dist = std::fabs(exact - static_cast<float>(cand));
            if (dist < bestDist) {
                bestDist = dist;
                best = cand;
            }
        }
        if (bestDist == std::numeric_limits<float>::max()) return voltage;
        return (best - 60) / 12.f;
    }

    bool noteInScale(int midiNote) const {
        int chroma = ((midiNote % 12) + 12) % 12;
        return (scaleMask >> chroma) & 1;
    }

    void process(const ProcessArgs& args) override {
        // Handle reset
        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), SCHMITT_LOW, SCHMITT_HIGH)) {
            onReset();
        }

        // Read the scale bus. Anything short of a full 12-channel mask disables
        // quantising outright, so a producer that drops its polyphony mid-patch
        // cannot leave us quantising against a stale mask.
        scaleConnected = false;
        if (inputs[SCALE_BUS_INPUT].isConnected() && inputs[SCALE_BUS_INPUT].getChannels() >= 12) {
            scaleMask = 0;
            for (int i = 0; i < 12; i++) {
                if (inputs[SCALE_BUS_INPUT].getVoltage(i) > 0.5f)
                    scaleMask |= (1 << i);
            }
            // An empty mask would leave nothing to quantise to
            scaleConnected = (scaleMask != 0);
        }

        // Get time ratio from knob
        int timeIndex = (int)std::round(params[TIME_DIV_PARAM].getValue());
        timeIndex = DSP::clamp(timeIndex, 0, NUM_TIME_RATIOS - 1);
        float timeRatio = TIME_RATIOS[timeIndex];

        bool gridSample = false;

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
                        gridSample = true;
                        divCounter = 0;
                    }
                } else {
                    // Multiplication: sample now and schedule more
                    gridSample = true;
                    clockMultCounter = 1;
                }
            }

            // Handle clock multiplication (interstitial triggers)
            if (timeRatio > 1.0f && clockMultCounter > 0 && lastClockPeriod > 0.0f) {
                float subPeriod = lastClockPeriod / timeRatio;
                int expectedCount = (int)(timeSinceLastClock / subPeriod);
                if (expectedCount >= clockMultCounter && clockMultCounter < (int)timeRatio) {
                    gridSample = true;
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
                gridSample = true;
            }
        }

        // Swing: hold back every second sample toward the next grid position.
        // 50% is even; at 75% the late sample lands halfway to the next one.
        float swingRatio = params[SWING_PARAM].getValue() / 100.0f;
        float swingDelay = (swingRatio - 0.5f) * 2.0f * effectiveSamplePeriod;

        bool shouldSample = false;
        if (gridSample) {
            // A newer grid point supersedes any sample still waiting on swing.
            // Without this, speeding the clock up fires the stale one off-grid.
            pendingSwingTimer = 0.0f;
            if (swingLate && swingDelay > 0.0f) {
                pendingSwingTimer = swingDelay;
            } else {
                shouldSample = true;
            }
            swingLate = !swingLate;
        } else if (pendingSwingTimer > 0.0f) {
            pendingSwingTimer -= args.sampleTime;
            if (pendingSwingTimer <= 0.0f) {
                pendingSwingTimer = 0.0f;
                shouldSample = true;
            }
        }

        // Get current CV and normalize
        float rawCV = inputs[CV_INPUT].getVoltage();
        bool isUnipolar = params[SCALE_PARAM].getValue() > 0.5f;
        float normalizedCV = normalizeCV(rawCV, isUnipolar);

        // Track min/max CV between clock samples to prevent aliasing
        // (ensures we detect bands the CV passed through between samples)
        peakMinCV = std::min(peakMinCV, normalizedCV);
        peakMaxCV = std::max(peakMaxCV, normalizedCV);

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

        // Run gate. Unpatched means running, so nothing changes for
        // patches built before this input existed.
        bool running = inputs[RUN_INPUT].isConnected()
                         ? inputs[RUN_INPUT].getVoltage() > SCHMITT_HIGH
                         : true;
        shouldSample = shouldSample && running;

        // The main algorithm: sample on clock
        bool firedThisSample = false;
        if (shouldSample) {
            // Swing makes the interval to the next sample uneven. A gate held
            // for the even period would run past that sample, so consecutive
            // crossings merge with no new edge between them. swingLate now says
            // whether the NEXT sample is the delayed one.
            float sampleInterval = swingLate ? (effectiveSamplePeriod + swingDelay)
                                             : (effectiveSamplePeriod - swingDelay);

            // Calculate current band from the instantaneous CV
            int currentBandIndex = (int)std::floor(normalizedCV * divisions);
            if (currentBandIndex >= divisions) currentBandIndex = divisions - 1;
            if (currentBandIndex < 0) currentBandIndex = 0;

            // Calculate the highest and lowest bands reached since last sample
            int peakHighBand = (int)std::floor(peakMaxCV * divisions);
            if (peakHighBand >= divisions) peakHighBand = divisions - 1;
            int peakLowBand = (int)std::floor(peakMinCV * divisions);
            if (peakLowBand < 0) peakLowBand = 0;

            // Use peak bands to detect crossings the instantaneous CV might miss
            // If CV went higher than current between samples, use that as the
            // effective "from" band for falling detection (and vice versa)
            int effectiveBand = currentBandIndex;
            if (lastBandIndex != -999) {
                int edgeMode = (int)std::round(params[EDGE_MODE_PARAM].getValue());

                // For falling: if CV peaked above current band, the effective
                // previous high point was peakHighBand
                // For rising: if CV dipped below current band, the effective
                // previous low point was peakLowBand
                bool shouldTrigger = false;
                int triggerBand = currentBandIndex;

                if (edgeMode == 0) {
                    // Rising: trigger if we reached higher than lastBandIndex
                    // Use current band (we're rising TO here)
                    shouldTrigger = (currentBandIndex > lastBandIndex);
                    // Also trigger if peak went higher even if current came back
                    if (!shouldTrigger && peakHighBand > lastBandIndex && peakHighBand > currentBandIndex) {
                        shouldTrigger = true;
                        triggerBand = peakHighBand;
                    }
                } else if (edgeMode == 2) {
                    // Falling: trigger if we reached lower than lastBandIndex
                    shouldTrigger = (currentBandIndex < lastBandIndex);
                    // Also trigger if peak went lower even if current came back
                    if (!shouldTrigger && peakLowBand < lastBandIndex && peakLowBand < currentBandIndex) {
                        shouldTrigger = true;
                        triggerBand = peakLowBand;
                    }
                } else {
                    // Both: any band change triggers
                    shouldTrigger = (currentBandIndex != lastBandIndex) ||
                                    (peakHighBand > lastBandIndex) ||
                                    (peakLowBand < lastBandIndex);
                    if (peakHighBand > lastBandIndex) triggerBand = peakHighBand;
                    else if (peakLowBand < lastBandIndex) triggerBand = peakLowBand;
                }

                if (shouldTrigger) {
                    bool isGateMode = params[GATE_MODE_PARAM].getValue() > 0.5f;
                    if (isGateMode) {
                        gatePulse.trigger(sampleInterval);
                    } else {
                        triggerPulse.trigger(TRIGGER_PULSE_DURATION);
                    }
                    displayFlashBand.store(DSP::clamp(triggerBand, 0, divisions - 1));
                    displayFlashTime.store(0.0f);
                    firedThisSample = true;

                    // Latch the S&H CV on the event that fires the output.
                    // With nothing patched, normal to the analysed CV input so the
                    // module is a self-contained quantised S&H.
                    float shSource = inputs[SH_CV_INPUT].isConnected()
                        ? inputs[SH_CV_INPUT].getVoltage()
                        : inputs[CV_INPUT].getVoltage();
                    shHeldVoltage = quantizeToScale(shSource);
                }
            }

            // Calculate stepped voltage (center of band)
            float bandCenter = ((float)currentBandIndex + 0.5f) / divisions;
            currentSteppedVoltage = denormalizeCV(bandCenter, isUnipolar);

            lastBandIndex = currentBandIndex;
            displayBandIndex.store(currentBandIndex);

            // Reset peak tracking for next sample period
            peakMinCV = normalizedCV;
            peakMaxCV = normalizedCV;
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
        outputs[SH_OUTPUT].setVoltage(shHeldVoltage);

        lights[TRIG_LIGHT].setBrightness(outputHigh ? 1.0f : 0.0f);

        sendToMutagen(firedThisSample, running);
    }

    // A Mutagen placed directly to the right clocks from our trigger, so
    // the pair works with no cable between them.
    void sendToMutagen(bool firedThisSample, bool running) {
        if (!rightExpander.module || rightExpander.module->model != modelMutagen)
            return;
        auto* msg = static_cast<WiggleRoom::MutagenExpanderMessage*>(rightExpander.producerMessage);
        msg->clear();
        msg->clockTick = firedThisSample;
        msg->resetPulse = resetTrigger.isHigh();
        msg->running = running;
        // Intersect has no address or bank of its own to offer.
        msg->stepAddress = -1;
        msg->bankIndex = -1;
        msg->valid = true;
        rightExpander.requestMessageFlip();
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
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(xCenter, yKnobs), module, Intersect::SWING_PARAM));

        // Inputs
        float yInputs = 215.f;
        addInput(createInputCentered<PJ301MPort>(Vec(xLeft, yInputs), module, Intersect::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xRight, yInputs), module, Intersect::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xLeft, yInputs + 45.f), module, Intersect::CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xRight, yInputs + 45.f), module, Intersect::DENSITY_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xCenter, yInputs), module, Intersect::SCALE_BUS_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(xCenter, yInputs + 45.f), module, Intersect::SH_CV_INPUT));

        // Mode switches (between inputs and outputs)
        float yModeSwitch = 290.f;
        addParam(createParamCentered<CKSS>(Vec(xLeft, yModeSwitch), module, Intersect::GATE_MODE_PARAM));
        addParam(createParamCentered<CKSSThree>(Vec(xRight, yModeSwitch), module, Intersect::EDGE_MODE_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(xCenter, yModeSwitch), module, Intersect::RUN_INPUT));

        // Outputs
        float yOutputs = 325.f;
        addOutput(createOutputCentered<PJ301MPort>(Vec(xLeft, yOutputs), module, Intersect::TRIG_OUTPUT));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(xLeft + 12.f, yOutputs - 12.f), module, Intersect::TRIG_LIGHT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xRight, yOutputs), module, Intersect::STEP_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xCenter, yOutputs), module, Intersect::SH_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelIntersect = createModel<WiggleRoom::Intersect, WiggleRoom::IntersectWidget>("Intersect");
