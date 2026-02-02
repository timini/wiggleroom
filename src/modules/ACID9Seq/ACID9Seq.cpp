/******************************************************************************
 * ACID-9 "INTERFERENCE" SEQUENCER
 * Polyrhythmic generative acid sequencer using two rotating buffers ("Gears")
 *
 * Architecture:
 *   - Gear A (Anchor): 16-step pitch pattern (the "riff")
 *   - Gear B (Modifier): Variable-length offset pattern (harmonic variation)
 *
 * The melody emerges from mathematical interference between gears.
 * Gate, Slide, and Accent are derived from interval-based logic modes.
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include "InterferenceEngine.hpp"
#include "LogicEngine.hpp"
#include <random>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

// Forward declaration for display
struct ACID9Seq;

// Clock div/mult ratios
static const std::vector<float> CLOCK_RATIOS = {
    1.f/8, 1.f/6, 1.f/4, 1.f/3, 1.f/2,
    1.f,  // x1 (index 5)
    2.f, 3.f, 4.f, 6.f, 8.f
};

static const std::vector<std::string> CLOCK_RATIO_LABELS = {
    "/8", "/6", "/4", "/3", "/2", "x1", "x2", "x3", "x4", "x6", "x8"
};

// Gear B length labels
static const std::vector<std::string> GEAR_B_LEN_LABELS = {
    "3", "5", "7", "9", "11", "13"
};

// Build logic mode labels from LogicEngine
static std::vector<std::string> buildModeLabels() {
    std::vector<std::string> labels;
    for (int i = 0; i < LogicEngine::NUM_MODES_INT; i++) {
        labels.push_back(LogicEngine::getModeShortName(i));
    }
    return labels;
}

static const std::vector<std::string> LOGIC_MODE_LABELS = buildModeLabels();

struct ACID9Seq : Module {
    enum ParamId {
        CLOCK_DIV_PARAM,
        GEAR_B_LEN_PARAM,
        OFFSET_PARAM,
        THRESHOLD_PARAM,
        GATE_MODE_PARAM,
        GATE_PROB_PARAM,
        SLIDE_MODE_PARAM,
        SLIDE_PROB_PARAM,
        ACCENT_MODE_PARAM,
        ACCENT_PROB_PARAM,
        MUTATE_A_PARAM,
        MUTATE_B_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        SCALE_BUS_INPUT,
        VOCT_INPUT,
        FREEZE_INPUT,
        INJECT_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        PITCH_OUTPUT,
        GATE_OUTPUT,
        ACCENT_OUTPUT,
        SLIDE_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        MUTATE_A_LIGHT,
        MUTATE_B_LIGHT,
        GATE_LIGHT,
        SLIDE_LIGHT,
        ACCENT_LIGHT,
        CLOCK_LIGHT,
        LIGHTS_LEN
    };

    InterferenceEngine engine;
    LogicEngine logic;

    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger mutateATrigger;
    dsp::SchmittTrigger mutateBTrigger;
    dsp::SchmittTrigger injectTrigger;

    dsp::PulseGenerator gatePulse;
    dsp::PulseGenerator accentPulse;
    dsp::PulseGenerator mutateAPulse;
    dsp::PulseGenerator mutateBPulse;
    dsp::PulseGenerator clockPulse;

    // Clock division/multiplication state
    int clockDivCounter = 0;
    float timeSinceExtClock = 0.f;
    float clockPeriod = 0.5f;
    int prevClockDivIdx = 5;
    int multTicksFired = 0;  // How many mult ticks fired since last ext clock

    // Output state
    bool gateHigh = false;
    bool slideActive = false;
    bool accentActive = false;
    float slideVoltage = 0.0f;
    float targetVoltage = 0.0f;

    // Random for probability
    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> probDist{0.0f, 1.0f};

    // Slide smoothing
    static constexpr float SLIDE_TIME = 0.05f;

    ACID9Seq() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Clock div/mult
        configSwitch(CLOCK_DIV_PARAM, 0.f, CLOCK_RATIOS.size() - 1, 5.f,
            "Clock Div/Mult", CLOCK_RATIO_LABELS);

        // Gear B length
        configSwitch(GEAR_B_LEN_PARAM, 0.f, 5.f, 2.f, "Gear B Length", GEAR_B_LEN_LABELS);

        // Phase offset
        auto* offsetParam = configParam(OFFSET_PARAM, 0.f, 15.f, 0.f, "Offset", " steps");
        offsetParam->snapEnabled = true;

        // Threshold for Leap/Step modes
        auto* threshParam = configParam(THRESHOLD_PARAM, 1.f, 12.f, 3.f, "Threshold", " st");
        threshParam->snapEnabled = true;

        // Gate logic
        configSwitch(GATE_MODE_PARAM, 0.f, LogicEngine::NUM_MODES_INT - 1, 0.f,
            "Gate Mode", LOGIC_MODE_LABELS);
        configParam(GATE_PROB_PARAM, 0.f, 1.f, 1.f, "Gate Probability", "%", 0.f, 100.f);

        // Slide logic
        configSwitch(SLIDE_MODE_PARAM, 0.f, LogicEngine::NUM_MODES_INT - 1, 6.f,
            "Slide Mode", LOGIC_MODE_LABELS);  // Default: Leap
        configParam(SLIDE_PROB_PARAM, 0.f, 1.f, 0.5f, "Slide Probability", "%", 0.f, 100.f);

        // Accent logic
        configSwitch(ACCENT_MODE_PARAM, 0.f, LogicEngine::NUM_MODES_INT - 1, 5.f,
            "Accent Mode", LOGIC_MODE_LABELS);  // Default: Drop
        configParam(ACCENT_PROB_PARAM, 0.f, 1.f, 0.5f, "Accent Probability", "%", 0.f, 100.f);

        // Mutate buttons
        configButton(MUTATE_A_PARAM, "Mutate Gear A");
        configButton(MUTATE_B_PARAM, "Mutate Gear B");

        // Inputs
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(SCALE_BUS_INPUT, "Scale Bus (16ch poly)");
        configInput(VOCT_INPUT, "V/Oct Transpose");
        configInput(FREEZE_INPUT, "Freeze Gear B");
        configInput(INJECT_INPUT, "Inject (randomize Gear B)");

        // Outputs
        configOutput(PITCH_OUTPUT, "Pitch (1V/Oct)");
        configOutput(GATE_OUTPUT, "Gate");
        configOutput(ACCENT_OUTPUT, "Accent");
        configOutput(SLIDE_OUTPUT, "Slide");
    }

    void advanceSequence() {
        // Advance engine
        engine.onClock();

        // Update logic engine with current state
        logic.update(
            engine.getQuantizedPitch(),
            engine.getPrevPitch(),
            engine.getPrevPrevPitch(),
            engine.getGearAValue(),
            engine.getPrevGearAValue(),
            engine.getCurrentGearBOffset(),
            engine.getPrevGearBOffset()
        );

        // Get parameters
        int threshold = static_cast<int>(params[THRESHOLD_PARAM].getValue());

        int gateMode = static_cast<int>(params[GATE_MODE_PARAM].getValue());
        float gateProb = params[GATE_PROB_PARAM].getValue();

        int slideMode = static_cast<int>(params[SLIDE_MODE_PARAM].getValue());
        float slideProb = params[SLIDE_PROB_PARAM].getValue();

        int accentMode = static_cast<int>(params[ACCENT_MODE_PARAM].getValue());
        float accentProb = params[ACCENT_PROB_PARAM].getValue();

        // Evaluate logic with probability
        gateHigh = logic.evaluateWithProb(gateMode, threshold, gateProb, probDist(rng));
        slideActive = logic.evaluateWithProb(slideMode, threshold, slideProb, probDist(rng));
        accentActive = logic.evaluateWithProb(accentMode, threshold, accentProb, probDist(rng));

        // Get target voltage
        targetVoltage = engine.getPitchVoltage();

        // Add V/Oct transpose
        if (inputs[VOCT_INPUT].isConnected()) {
            targetVoltage += inputs[VOCT_INPUT].getVoltage();
        }

        // Trigger outputs
        if (gateHigh) {
            gatePulse.trigger(0.001f);
        }
        if (accentActive && gateHigh) {
            accentPulse.trigger(0.001f);
        }

        clockPulse.trigger(0.01f);
    }

    void process(const ProcessArgs& args) override {
        // Update engine parameters
        int gearBLenIdx = static_cast<int>(params[GEAR_B_LEN_PARAM].getValue());
        engine.setGearBLengthIndex(gearBLenIdx);

        int offset = static_cast<int>(params[OFFSET_PARAM].getValue());
        engine.setOffset(offset);

        // Scale Bus input
        if (inputs[SCALE_BUS_INPUT].isConnected()) {
            int channels = inputs[SCALE_BUS_INPUT].getChannels();
            float voltages[16] = {0};
            for (int i = 0; i < std::min(channels, 16); i++) {
                voltages[i] = inputs[SCALE_BUS_INPUT].getVoltage(i);
            }
            engine.updateFromScaleBus(voltages, channels);
        }

        // Freeze control
        bool frozen = inputs[FREEZE_INPUT].isConnected() &&
                      inputs[FREEZE_INPUT].getVoltage() > 1.0f;
        engine.setFrozen(frozen);

        // Process mutate buttons
        if (mutateATrigger.process(params[MUTATE_A_PARAM].getValue())) {
            engine.mutateGearA();
            mutateAPulse.trigger(0.1f);
        }

        if (mutateBTrigger.process(params[MUTATE_B_PARAM].getValue())) {
            engine.mutateGearB();
            mutateBPulse.trigger(0.1f);
        }

        // Process inject input
        if (injectTrigger.process(inputs[INJECT_INPUT].getVoltage())) {
            engine.mutateGearB();
            mutateBPulse.trigger(0.1f);
        }

        // Process reset
        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
            engine.reset();
            logic.reset();
            gateHigh = false;
            slideActive = false;
            clockDivCounter = 0;
            timeSinceExtClock = 0.f;
            multTicksFired = 0;
        }

        // Clock div/mult
        int clockDivIdx = static_cast<int>(params[CLOCK_DIV_PARAM].getValue());
        clockDivIdx = DSP::clamp(clockDivIdx, 0, (int)CLOCK_RATIOS.size() - 1);
        float clockRatio = CLOCK_RATIOS[clockDivIdx];

        if (clockDivIdx != prevClockDivIdx) {
            clockDivCounter = 0;
            timeSinceExtClock = 0.f;
            multTicksFired = 0;
            prevClockDivIdx = clockDivIdx;
        }

        bool extClockTick = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage());

        // Track time between external clocks
        if (extClockTick) {
            if (timeSinceExtClock > 0.001f) {  // Ignore very fast re-triggers
                clockPeriod = timeSinceExtClock;
            }
            timeSinceExtClock = 0.f;
            multTicksFired = 0;
        } else {
            timeSinceExtClock += args.sampleTime;
        }

        // Handle clock division/multiplication
        if (clockRatio < 1.f) {
            // Division: advance every N external clocks
            int divisor = static_cast<int>(1.f / clockRatio + 0.5f);
            if (extClockTick) {
                clockDivCounter++;
                if (clockDivCounter >= divisor) {
                    clockDivCounter = 0;
                    advanceSequence();
                }
            }
        } else if (clockRatio == 1.f) {
            // 1:1 - just follow external clock
            if (extClockTick) {
                advanceSequence();
            }
        } else {
            // Multiplication: generate N ticks per external clock period
            int multiplier = static_cast<int>(clockRatio + 0.5f);

            if (extClockTick) {
                // First tick on external clock
                advanceSequence();
                multTicksFired = 1;
            } else if (clockPeriod > 0.001f && multiplier > 1) {
                // Generate intermediate ticks between external clocks
                float internalPeriod = clockPeriod / multiplier;
                int expectedTicks = static_cast<int>(timeSinceExtClock / internalPeriod) + 1;

                // Fire any ticks we haven't fired yet (but not more than multiplier)
                while (multTicksFired < expectedTicks && multTicksFired < multiplier) {
                    advanceSequence();
                    multTicksFired++;
                }
            }
        }

        // Update slide voltage
        if (slideActive) {
            float slew = 1.0f - std::exp(-args.sampleTime / SLIDE_TIME);
            slideVoltage += (targetVoltage - slideVoltage) * slew;
        } else {
            slideVoltage = targetVoltage;
        }

        // Outputs
        outputs[PITCH_OUTPUT].setVoltage(slideVoltage);
        outputs[GATE_OUTPUT].setVoltage(gateHigh ? 10.0f : 0.0f);
        outputs[ACCENT_OUTPUT].setVoltage(accentPulse.process(args.sampleTime) ? 10.0f : 0.0f);
        outputs[SLIDE_OUTPUT].setVoltage(slideActive ? 10.0f : 0.0f);

        // Lights
        lights[MUTATE_A_LIGHT].setSmoothBrightness(mutateAPulse.process(args.sampleTime) ? 1.0f : 0.0f, args.sampleTime);
        lights[MUTATE_B_LIGHT].setSmoothBrightness(mutateBPulse.process(args.sampleTime) ? 1.0f : 0.0f, args.sampleTime);
        lights[GATE_LIGHT].setSmoothBrightness(gateHigh ? 1.0f : 0.0f, args.sampleTime);
        lights[SLIDE_LIGHT].setSmoothBrightness(slideActive ? 1.0f : 0.0f, args.sampleTime);
        lights[ACCENT_LIGHT].setSmoothBrightness(accentActive ? 1.0f : 0.0f, args.sampleTime);
        lights[CLOCK_LIGHT].setSmoothBrightness(clockPulse.process(args.sampleTime) ? 1.0f : 0.0f, args.sampleTime);
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "engine", engine.toJson());
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* engineJ = json_object_get(rootJ, "engine");
        if (engineJ) {
            engine.fromJson(engineJ);
        }
    }
};

// Include the planetary display after ACID9Seq is defined
#include "widgets/PlanetaryDisplay.hpp"

// Implement PlanetaryDisplay accessor methods
int PlanetaryDisplay::getGearAPosition() {
    if (!module) return 0;
    return static_cast<ACID9Seq*>(module)->engine.getGearA().getPosition();
}

int PlanetaryDisplay::getGearBPosition() {
    if (!module) return 0;
    return static_cast<ACID9Seq*>(module)->engine.getGearB().getPosition();
}

int PlanetaryDisplay::getGearBLength() {
    if (!module) return 7;
    return static_cast<ACID9Seq*>(module)->engine.getGearBLength();
}

int PlanetaryDisplay::getQuantizedPitch() {
    if (!module) return 12;
    return static_cast<ACID9Seq*>(module)->engine.getQuantizedPitch();
}

struct ACID9SeqWidget : ModuleWidget {
    ACID9SeqWidget(ACID9Seq* module) {
        setModule(module);

        // 16HP width
        box.size = Vec(16 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        // Background panel
        addChild(new ImagePanel(
            asset::plugin(pluginInstance, "res/ACID9Seq.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Layout for 16HP
        const float modWidth = 16 * 5.08f;  // 16HP in mm
        const float col1 = 10.0f;
        const float col2 = modWidth / 2;
        const float col3 = modWidth - 10.0f;

        // Row positions
        const float row1 = 20.0f;
        const float row2 = 36.0f;
        const float row3 = 50.0f;
        const float row4 = 64.0f;
        const float row5 = 78.0f;
        const float row6 = 92.0f;
        const float row7 = 108.0f;

        // === TOP: Planetary Display (smaller) ===
        PlanetaryDisplay* display = new PlanetaryDisplay();
        display->box.size = Vec(70, 70);
        display->box.pos = mm2px(Vec((modWidth - 23) / 2, 14));
        display->module = module;
        addChild(display);

        // === GEAR CONTROLS ===
        // Clock Div/Mult
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col1, row2)), module, ACID9Seq::CLOCK_DIV_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(
            mm2px(Vec(col1, row2 - 6)), module, ACID9Seq::CLOCK_LIGHT));

        // Gear B Length
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col3, row2)), module, ACID9Seq::GEAR_B_LEN_PARAM));

        // Offset
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col1, row3)), module, ACID9Seq::OFFSET_PARAM));

        // Threshold
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col3, row3)), module, ACID9Seq::THRESHOLD_PARAM));

        // === LOGIC MODE CONTROLS ===
        // Gate: Mode + Prob
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col1, row4)), module, ACID9Seq::GATE_MODE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col3, row4)), module, ACID9Seq::GATE_PROB_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(
            mm2px(Vec(col2, row4)), module, ACID9Seq::GATE_LIGHT));

        // Slide: Mode + Prob
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col1, row5)), module, ACID9Seq::SLIDE_MODE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col3, row5)), module, ACID9Seq::SLIDE_PROB_PARAM));
        addChild(createLightCentered<SmallLight<YellowLight>>(
            mm2px(Vec(col2, row5)), module, ACID9Seq::SLIDE_LIGHT));

        // Accent: Mode + Prob
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col1, row6)), module, ACID9Seq::ACCENT_MODE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col3, row6)), module, ACID9Seq::ACCENT_PROB_PARAM));
        addChild(createLightCentered<SmallLight<RedLight>>(
            mm2px(Vec(col2, row6)), module, ACID9Seq::ACCENT_LIGHT));

        // === MUTATE BUTTONS ===
        addParam(createParamCentered<VCVButton>(
            mm2px(Vec(col1, row7 - 6)), module, ACID9Seq::MUTATE_A_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(
            mm2px(Vec(col1 + 5, row7 - 9)), module, ACID9Seq::MUTATE_A_LIGHT));

        addParam(createParamCentered<VCVButton>(
            mm2px(Vec(col3, row7 - 6)), module, ACID9Seq::MUTATE_B_PARAM));
        addChild(createLightCentered<SmallLight<RedLight>>(
            mm2px(Vec(col3 - 5, row7 - 9)), module, ACID9Seq::MUTATE_B_LIGHT));

        // === BOTTOM ROW - I/O ===
        const float ioY = row7 + 6;
        float ioX = 6.0f;
        const float ioSpacing = 8.0f;

        // Inputs (left side)
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(ioX, ioY)), module, ACID9Seq::CLOCK_INPUT));
        ioX += ioSpacing;

        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(ioX, ioY)), module, ACID9Seq::RESET_INPUT));
        ioX += ioSpacing;

        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(ioX, ioY)), module, ACID9Seq::SCALE_BUS_INPUT));
        ioX += ioSpacing;

        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(ioX, ioY)), module, ACID9Seq::VOCT_INPUT));
        ioX += ioSpacing;

        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(ioX, ioY)), module, ACID9Seq::FREEZE_INPUT));
        ioX += ioSpacing;

        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(ioX, ioY)), module, ACID9Seq::INJECT_INPUT));
        ioX += ioSpacing + 2;

        // Outputs (right side)
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(ioX, ioY)), module, ACID9Seq::PITCH_OUTPUT));
        ioX += ioSpacing;

        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(ioX, ioY)), module, ACID9Seq::GATE_OUTPUT));
        ioX += ioSpacing;

        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(ioX, ioY)), module, ACID9Seq::ACCENT_OUTPUT));
        ioX += ioSpacing;

        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(ioX, ioY)), module, ACID9Seq::SLIDE_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelACID9Seq = createModel<WiggleRoom::ACID9Seq, WiggleRoom::ACID9SeqWidget>("ACID9Seq");
