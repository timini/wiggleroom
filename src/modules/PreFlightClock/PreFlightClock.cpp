#include "rack.hpp"
#include "ImagePanel.hpp"
#include <atomic>
#include <cmath>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct PreFlightClock : Module {
    enum ParamId {
        BPM_PARAM,
        PLAY_PARAM,
        STOP_PARAM,
        PARAMS_LEN
    };

    enum InputId {
        BPM_CV_INPUT,
        PLAY_TRIG_INPUT,
        STOP_TRIG_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        METRO_OUTPUT,
        RESET_OUTPUT,
        RUN_OUTPUT,
        MASTER_OUTPUT,
        X2_OUTPUT,
        X3_OUTPUT,
        X4_OUTPUT,
        X8_OUTPUT,
        X16_OUTPUT,
        X32_OUTPUT,
        DIV1_5_OUTPUT,
        DIV2_OUTPUT,
        DIV3_OUTPUT,
        DIV4_OUTPUT,
        DIV8_OUTPUT,
        DIV16_OUTPUT,
        DIV32_OUTPUT,
        OUTPUTS_LEN
    };

    enum LightId {
        STATE_RED_LIGHT,
        STATE_GREEN_LIGHT,
        STATE_BLUE_LIGHT,
        BEAT_LIGHT,
        LIGHTS_LEN
    };

    enum State {
        STOPPED,
        COUNTING_IN,
        RUNNING
    };

    // Clock state
    double beatPhase = 0.0;
    double prevBeatPhase = 0.0;
    State state = STOPPED;
    int countInBeats = 0;
    bool waitingForBeat = false;

    // Metronome synthesis
    float metroPhase = 0.f;
    float metroEnvelope = 0.f;
    float metroFreq = 440.f;
    float metroDecayTime = 0.05f;

    // Triggers and pulses
    dsp::SchmittTrigger playTrigger;
    dsp::SchmittTrigger stopTrigger;
    dsp::SchmittTrigger playButtonTrigger;
    dsp::SchmittTrigger stopButtonTrigger;
    dsp::PulseGenerator resetPulse;
    dsp::PulseGenerator masterPulse;
    dsp::PulseGenerator multPulses[6]; // x2, x3, x4, x8, x16, x32
    dsp::PulseGenerator divPulses[7];  // /1.5, /2, /3, /4, /8, /16, /32

    // Beat light decay
    float beatBrightness = 0.f;

    // Thread-safe display state
    std::atomic<int> displayCount{0};
    std::atomic<int> displayState{0};

    PreFlightClock() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(BPM_PARAM, 30.f, 300.f, 120.f, "BPM", " BPM");
        configButton(PLAY_PARAM, "Play");
        configButton(STOP_PARAM, "Stop");

        configInput(BPM_CV_INPUT, "BPM CV (0-10V = 30-300 BPM)");
        configInput(PLAY_TRIG_INPUT, "Play trigger");
        configInput(STOP_TRIG_INPUT, "Stop trigger");

        configOutput(METRO_OUTPUT, "Metronome");
        configOutput(RESET_OUTPUT, "Reset");
        configOutput(RUN_OUTPUT, "Run");
        configOutput(MASTER_OUTPUT, "Clock (x1)");
        configOutput(X2_OUTPUT, "Clock x2");
        configOutput(X3_OUTPUT, "Clock x3");
        configOutput(X4_OUTPUT, "Clock x4");
        configOutput(X8_OUTPUT, "Clock x8");
        configOutput(X16_OUTPUT, "Clock x16");
        configOutput(X32_OUTPUT, "Clock x32");
        configOutput(DIV1_5_OUTPUT, "Clock /1.5");
        configOutput(DIV2_OUTPUT, "Clock /2");
        configOutput(DIV3_OUTPUT, "Clock /3");
        configOutput(DIV4_OUTPUT, "Clock /4");
        configOutput(DIV8_OUTPUT, "Clock /8");
        configOutput(DIV16_OUTPUT, "Clock /16");
        configOutput(DIV32_OUTPUT, "Clock /32");

        configLight(STATE_RED_LIGHT, "State");
        configLight(BEAT_LIGHT, "Beat");
    }

    void onReset() override {
        beatPhase = 0.0;
        prevBeatPhase = 0.0;
        state = STOPPED;
        countInBeats = 0;
        waitingForBeat = false;
        metroPhase = 0.f;
        metroEnvelope = 0.f;
        beatBrightness = 0.f;
        displayCount.store(0);
        displayState.store(STOPPED);
    }

    void process(const ProcessArgs& args) override {
        float dt = args.sampleTime;

        // --- Read BPM ---
        float bpm = params[BPM_PARAM].getValue();
        if (inputs[BPM_CV_INPUT].isConnected()) {
            float cv = inputs[BPM_CV_INPUT].getVoltage();
            bpm += (cv / 10.f) * 270.f; // 0-10V maps to 0-270 BPM offset
        }
        bpm = clamp(bpm, 30.f, 300.f);

        // --- Play/Stop triggers ---
        bool playTriggered = false;
        bool stopTriggered = false;

        if (playButtonTrigger.process(params[PLAY_PARAM].getValue())) {
            playTriggered = true;
        }
        if (playTrigger.process(inputs[PLAY_TRIG_INPUT].getVoltage(), 0.1f, 1.f)) {
            playTriggered = true;
        }
        if (stopButtonTrigger.process(params[STOP_PARAM].getValue())) {
            stopTriggered = true;
        }
        if (stopTrigger.process(inputs[STOP_TRIG_INPUT].getVoltage(), 0.1f, 1.f)) {
            stopTriggered = true;
        }

        // --- State machine ---
        if (stopTriggered) {
            state = STOPPED;
            metroEnvelope = 0.f;
            waitingForBeat = false;
            countInBeats = 0;
        }

        if (playTriggered && state == STOPPED) {
            state = COUNTING_IN;
            waitingForBeat = true;
            countInBeats = 4;
        }

        // --- Phase accumulator (always running) ---
        double delta = static_cast<double>(bpm) / (60.0 * args.sampleRate);
        prevBeatPhase = beatPhase;
        beatPhase += delta;

        // --- Master beat detection ---
        bool masterBeat = (std::floor(beatPhase) != std::floor(prevBeatPhase));

        if (masterBeat) {
            masterPulse.trigger(1e-3f);
            beatBrightness = 1.f;

            // Count-in logic
            if (state == COUNTING_IN) {
                if (waitingForBeat) {
                    // First beat after play: fire reset
                    resetPulse.trigger(1e-3f);
                    waitingForBeat = false;
                }

                if (countInBeats > 0) {
                    // Trigger metronome beep
                    metroPhase = 0.f;
                    metroFreq = 440.f;
                    metroEnvelope = 1.f;
                    metroDecayTime = 0.05f;
                    countInBeats--;
                } else {
                    // Count-in complete: transition to RUNNING
                    state = RUNNING;
                    // Downbeat beep (higher pitch, longer decay)
                    metroPhase = 0.f;
                    metroFreq = 880.f;
                    metroEnvelope = 1.f;
                    metroDecayTime = 0.1f;
                }
            }
        }

        // --- Multiplied/divided beat detection ---
        // Multipliers
        static const float multRatios[] = {2.f, 3.f, 4.f, 8.f, 16.f, 32.f};
        for (int i = 0; i < 6; i++) {
            double r = static_cast<double>(multRatios[i]);
            if (std::floor(beatPhase * r) != std::floor(prevBeatPhase * r)) {
                multPulses[i].trigger(1e-3f);
            }
        }

        // Dividers
        static const double divRatios[] = {
            2.0 / 3.0, // /1.5
            0.5,        // /2
            1.0 / 3.0,  // /3
            0.25,       // /4
            0.125,      // /8
            0.0625,     // /16
            0.03125     // /32
        };
        for (int i = 0; i < 7; i++) {
            if (std::floor(beatPhase * divRatios[i]) != std::floor(prevBeatPhase * divRatios[i])) {
                divPulses[i].trigger(1e-3f);
            }
        }

        // --- Metronome audio synthesis ---
        float metroOut = 0.f;
        if (metroEnvelope > 0.001f) {
            metroOut = std::sin(2.f * M_PI * metroPhase) * metroEnvelope;
            metroPhase += metroFreq * dt;
            // Keep phase in [0,1) to avoid precision loss
            if (metroPhase >= 1.f) metroPhase -= 1.f;
            metroEnvelope *= std::exp(-dt / metroDecayTime);
        }

        // --- Set outputs ---
        outputs[METRO_OUTPUT].setVoltage(metroOut * 5.f);
        outputs[RESET_OUTPUT].setVoltage(resetPulse.process(dt) ? 10.f : 0.f);
        outputs[RUN_OUTPUT].setVoltage(state == RUNNING ? 10.f : 0.f);
        outputs[MASTER_OUTPUT].setVoltage(masterPulse.process(dt) ? 10.f : 0.f);

        for (int i = 0; i < 6; i++) {
            outputs[X2_OUTPUT + i].setVoltage(multPulses[i].process(dt) ? 10.f : 0.f);
        }
        for (int i = 0; i < 7; i++) {
            outputs[DIV1_5_OUTPUT + i].setVoltage(divPulses[i].process(dt) ? 10.f : 0.f);
        }

        // --- Lights ---
        beatBrightness *= std::exp(-dt / 0.05f);

        switch (state) {
            case STOPPED:
                lights[STATE_RED_LIGHT].setBrightness(0.3f);
                lights[STATE_GREEN_LIGHT].setBrightness(0.f);
                lights[STATE_BLUE_LIGHT].setBrightness(0.f);
                break;
            case COUNTING_IN:
                lights[STATE_RED_LIGHT].setBrightness(0.f);
                lights[STATE_GREEN_LIGHT].setBrightness(0.f);
                lights[STATE_BLUE_LIGHT].setBrightness(beatBrightness);
                break;
            case RUNNING:
                lights[STATE_RED_LIGHT].setBrightness(0.f);
                lights[STATE_GREEN_LIGHT].setBrightness(0.6f);
                lights[STATE_BLUE_LIGHT].setBrightness(0.f);
                break;
        }
        lights[BEAT_LIGHT].setBrightness(beatBrightness);

        // --- Update display state (thread-safe) ---
        displayState.store(static_cast<int>(state));
        displayCount.store(countInBeats);
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "state", json_integer(static_cast<int>(state)));
        json_object_set_new(rootJ, "beatPhase", json_real(beatPhase));
        json_object_set_new(rootJ, "countInBeats", json_integer(countInBeats));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* stateJ = json_object_get(rootJ, "state");
        if (stateJ) state = static_cast<State>(json_integer_value(stateJ));

        json_t* phaseJ = json_object_get(rootJ, "beatPhase");
        if (phaseJ) beatPhase = json_real_value(phaseJ);

        json_t* countJ = json_object_get(rootJ, "countInBeats");
        if (countJ) countInBeats = static_cast<int>(json_integer_value(countJ));

        prevBeatPhase = beatPhase;
        displayState.store(static_cast<int>(state));
        displayCount.store(countInBeats);
    }
};

// --- Count-in display widget ---
struct PreFlightClockDisplay : LightWidget {
    PreFlightClock* module = nullptr;

    PreFlightClockDisplay() {
        box.size = Vec(40.f, 24.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        // Background
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillColor(args.vg, nvgRGBA(10, 10, 20, 200));
        nvgFill(args.vg);

        // Border
        nvgStrokeColor(args.vg, nvgRGBA(80, 80, 120, 150));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);

        // Text
        const char* text = "--";
        NVGcolor color = nvgRGBA(100, 100, 140, 200);

        if (module) {
            int st = module->displayState.load();
            int count = module->displayCount.load();

            static char buf[4];

            switch (st) {
                case PreFlightClock::STOPPED:
                    text = "\xe2\x96\xa0"; // filled square (UTF-8)
                    color = nvgRGBA(200, 60, 60, 220);
                    break;
                case PreFlightClock::COUNTING_IN:
                    snprintf(buf, sizeof(buf), "%d", count);
                    text = buf;
                    color = nvgRGBA(80, 140, 255, 255);
                    break;
                case PreFlightClock::RUNNING:
                    text = "\xe2\x96\xb6"; // play triangle (UTF-8)
                    color = nvgRGBA(60, 220, 80, 220);
                    break;
            }
        }

        nvgFillColor(args.vg, color);
        nvgFontSize(args.vg, 16.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text, nullptr);

        LightWidget::drawLayer(args, layer);
    }
};

// --- Widget ---
struct PreFlightClockWidget : ModuleWidget {
    PreFlightClockWidget(PreFlightClock* module) {
        setModule(module);

        // 10HP panel
        box.size = Vec(10 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        // Panel background
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/PreFlightClock.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float xCenter = box.size.x / 2.f;
        float xLeft = box.size.x * 0.25f;
        float xRight = box.size.x * 0.75f;

        // --- Top section: BPM knob ---
        addParam(createParamCentered<RoundBigBlackKnob>(
            Vec(xCenter, 52), module, PreFlightClock::BPM_PARAM));

        // Beat LED
        addChild(createLightCentered<MediumLight<YellowLight>>(
            Vec(xCenter, 80), module, PreFlightClock::BEAT_LIGHT));

        // --- Play/Stop buttons + state LED ---
        addParam(createParamCentered<VCVButton>(
            Vec(xLeft, 104), module, PreFlightClock::PLAY_PARAM));
        addParam(createParamCentered<VCVButton>(
            Vec(xRight, 104), module, PreFlightClock::STOP_PARAM));

        // State LED (RGB)
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(
            Vec(xCenter, 104), module, PreFlightClock::STATE_RED_LIGHT));

        // Count-in display
        {
            PreFlightClockDisplay* display = new PreFlightClockDisplay();
            display->module = module;
            display->box.pos = Vec(xCenter - 20.f, 118);
            addChild(display);
        }

        // --- State outputs: Reset, Run, Metro ---
        float yStateRow = 156;
        float xCol1 = box.size.x * 0.17f;
        float xCol2 = box.size.x * 0.50f;
        float xCol3 = box.size.x * 0.83f;

        addOutput(createOutputCentered<PJ301MPort>(
            Vec(xCol1, yStateRow), module, PreFlightClock::RESET_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(xCol2, yStateRow), module, PreFlightClock::RUN_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(xCol3, yStateRow), module, PreFlightClock::METRO_OUTPUT));

        // --- Clock outputs: 4 rows of 4 (last row has 2) ---
        float yRow1 = 194;
        float yRow2 = 226;
        float yRow3 = 258;
        float yRow4 = 290;
        float xC1 = box.size.x * 0.125f;
        float xC2 = box.size.x * 0.375f;
        float xC3 = box.size.x * 0.625f;
        float xC4 = box.size.x * 0.875f;

        // Row 1: x1, x2, x3, x4
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC1, yRow1), module, PreFlightClock::MASTER_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC2, yRow1), module, PreFlightClock::X2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC3, yRow1), module, PreFlightClock::X3_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC4, yRow1), module, PreFlightClock::X4_OUTPUT));

        // Row 2: x8, x16, x32, /1.5
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC1, yRow2), module, PreFlightClock::X8_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC2, yRow2), module, PreFlightClock::X16_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC3, yRow2), module, PreFlightClock::X32_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC4, yRow2), module, PreFlightClock::DIV1_5_OUTPUT));

        // Row 3: /2, /3, /4, /8
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC1, yRow3), module, PreFlightClock::DIV2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC2, yRow3), module, PreFlightClock::DIV3_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC3, yRow3), module, PreFlightClock::DIV4_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC4, yRow3), module, PreFlightClock::DIV8_OUTPUT));

        // Row 4: /16, /32
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC2, yRow4), module, PreFlightClock::DIV16_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(xC3, yRow4), module, PreFlightClock::DIV32_OUTPUT));

        // --- CV inputs at bottom ---
        float yInputRow = 326;
        addInput(createInputCentered<PJ301MPort>(
            Vec(xCol1, yInputRow), module, PreFlightClock::BPM_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            Vec(xCol2, yInputRow), module, PreFlightClock::PLAY_TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            Vec(xCol3, yInputRow), module, PreFlightClock::STOP_TRIG_INPUT));
    }
};

} // namespace WiggleRoom

Model* modelPreFlightClock = createModel<WiggleRoom::PreFlightClock, WiggleRoom::PreFlightClockWidget>("PreFlightClock");
