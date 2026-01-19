/******************************************************************************
 * SUPER OSCILLATOR
 * Simple sine/saw oscillator with V/Oct tracking
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"

using namespace rack;

// Forward declaration for plugin.cpp
extern Plugin* pluginInstance;

namespace WiggleRoom {

struct SuperOsc : Module {
    enum ParamId {
        FREQ_PARAM,
        FINE_PARAM,
        WAVE_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        VOCT_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        AUDIO_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    float phase = 0.0f;
    DSP::OnePoleLPF freqSmooth{0.05f};
    bool initialized = false;

    SuperOsc() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Frequency: C1 to C9 (32.7 Hz to 8372 Hz), default C4 (261.6 Hz)
        configParam(FREQ_PARAM, -4.0f, 5.0f, 0.0f, "Frequency", " Hz", 2.0f, dsp::FREQ_C4);

        // Initialize filter to default frequency to avoid startup sweep
        freqSmooth.reset(dsp::FREQ_C4);
        configParam(FINE_PARAM, -0.5f, 0.5f, 0.0f, "Fine tune", " cents", 0.0f, 100.0f);
        configParam(WAVE_PARAM, 0.0f, 1.0f, 0.0f, "Waveform", "", 0.0f, 1.0f);

        configInput(VOCT_INPUT, "V/Oct");
        configOutput(AUDIO_OUTPUT, "Audio");
    }

    void process(const ProcessArgs& args) override {
        // Calculate frequency from knob + V/Oct input
        float pitch = params[FREQ_PARAM].getValue();
        pitch += params[FINE_PARAM].getValue();
        pitch += inputs[VOCT_INPUT].getVoltage();

        float freq = dsp::FREQ_C4 * DSP::voltToFreqMultiplier(pitch);
        freq = DSP::clamp(freq, 20.0f, args.sampleRate / 2.0f);

        // Smooth frequency changes
        freq = freqSmooth.process(freq);

        // Phase increment
        float deltaPhase = freq * args.sampleTime;

        // Get waveform mix (0 = sine, 1 = saw)
        float waveMix = params[WAVE_PARAM].getValue();

        // Generate waveforms
        float sine = std::sin(DSP::TWO_PI * phase);

        // Anti-aliased saw using polyblep
        float saw = 2.0f * phase - 1.0f;
        saw -= DSP::polyblep(phase, deltaPhase);

        // Mix between sine and saw
        float output = DSP::lerp(sine, saw, waveMix);

        // Output at 5V peak
        outputs[AUDIO_OUTPUT].setVoltage(output * 5.0f);

        // Advance phase with proper wraparound
        phase += deltaPhase;
        phase -= std::floor(phase);
    }
};

struct SuperOscWidget : ModuleWidget {
    SuperOscWidget(SuperOsc* module) {
        setModule(module);
        box.size = Vec(3 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/SuperOsc.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Knobs - centered horizontally
        float xCenter = box.size.x / 2.0f;

        addParam(createParamCentered<RoundBigBlackKnob>(Vec(xCenter, 80), module, SuperOsc::FREQ_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(xCenter, 140), module, SuperOsc::FINE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(xCenter, 190), module, SuperOsc::WAVE_PARAM));

        // Input
        addInput(createInputCentered<PJ301MPort>(Vec(xCenter, 260), module, SuperOsc::VOCT_INPUT));

        // Output
        addOutput(createOutputCentered<PJ301MPort>(Vec(xCenter, 320), module, SuperOsc::AUDIO_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelSuperOsc = createModel<WiggleRoom::SuperOsc, WiggleRoom::SuperOscWidget>("SuperOsc");
