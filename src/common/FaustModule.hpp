#pragma once

#include "rack.hpp"
#include "DSP.hpp"
#include <vector>
#include <cstring>

namespace WiggleRoom {

/**
 * Base class for VCV Rack modules wrapping Faust-generated DSP
 *
 * Template Parameters:
 *   FaustDSP - The Faust-generated VCVRackDSP class
 *
 * Usage:
 *   1. Include your generated Faust header (e.g., #include "moog_lpf.hpp")
 *   2. Create a module struct inheriting from FaustModule<VCVRackDSP>
 *   3. In the constructor, call config() and then mapParam() for each parameter
 *   4. Define your ModuleWidget as usual
 *
 * Example:
 *   struct MyFilter : FaustModule<VCVRackDSP> {
 *       enum ParamId { CUTOFF_PARAM, PARAMS_LEN };
 *       enum InputId { AUDIO_INPUT, CUTOFF_CV_INPUT, INPUTS_LEN };
 *       enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
 *       enum LightId { LIGHTS_LEN };
 *
 *       MyFilter() {
 *           config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
 *           configParam(CUTOFF_PARAM, 20.f, 20000.f, 1000.f, "Cutoff", " Hz");
 *           configInput(AUDIO_INPUT, "Audio");
 *           configInput(CUTOFF_CV_INPUT, "Cutoff CV");
 *           configOutput(AUDIO_OUTPUT, "Audio");
 *
 *           // Map VCV param to Faust param index 0
 *           mapParam(CUTOFF_PARAM, 0);
 *           // Map CV input to modulate Faust param 0 (V/Oct scaling)
 *           mapCVInput(CUTOFF_CV_INPUT, 0, true);
 *       }
 *   };
 */
template<typename FaustDSP>
struct FaustModule : rack::Module {

    /**
     * Mapping from VCV parameter to Faust DSP parameter
     */
    struct ParamMapping {
        int vcvParamId;      // VCV param enum value (-1 if not mapped)
        int faustParamIdx;   // Faust parameter index

        ParamMapping(int vcv, int faust)
            : vcvParamId(vcv), faustParamIdx(faust) {}
    };

    /**
     * Mapping from VCV CV input to Faust DSP parameter
     */
    struct CVMapping {
        int vcvInputId;      // VCV input enum value
        int faustParamIdx;   // Faust parameter index to modulate
        bool exponential;    // Use V/Oct exponential scaling
        float scale;         // Linear scale factor (default 1.0)

        CVMapping(int input, int faust, bool exp, float s)
            : vcvInputId(input), faustParamIdx(faust), exponential(exp), scale(s) {}
    };

protected:
    // The Faust DSP instance
    FaustDSP faustDsp;

    // Parameter mappings
    std::vector<ParamMapping> paramMappings;
    std::vector<CVMapping> cvMappings;

    // Audio I/O buffers (single sample processing)
    static constexpr int MAX_IO = 16;
    float inputBuffer[MAX_IO] = {};
    float outputBuffer[MAX_IO] = {};
    float* inputPtrs[MAX_IO];
    float* outputPtrs[MAX_IO];

    // First audio input/output (for simple mono modules)
    int audioInputId = -1;
    int audioOutputId = -1;

    bool initialized = false;

    /**
     * Map a VCV parameter (knob) directly to a Faust parameter
     *
     * @param vcvParamId    Your ParamId enum value
     * @param faustParamIdx Faust parameter index (order declared in .dsp)
     */
    void mapParam(int vcvParamId, int faustParamIdx) {
        paramMappings.emplace_back(vcvParamId, faustParamIdx);
    }

    /**
     * Map a VCV CV input to modulate a Faust parameter
     *
     * @param vcvInputId    Your InputId enum value for the CV jack
     * @param faustParamIdx Faust parameter index to modulate
     * @param exponential   If true, use V/Oct scaling (1V = 2x frequency)
     * @param scale         Linear scale factor (default 1.0)
     */
    void mapCVInput(int vcvInputId, int faustParamIdx,
                    bool exponential = false, float scale = 1.0f) {
        cvMappings.emplace_back(vcvInputId, faustParamIdx, exponential, scale);
    }

    /**
     * Set which VCV input/output are the main audio I/O
     * (for simple mono modules)
     */
    void setAudioIO(int inputId, int outputId) {
        audioInputId = inputId;
        audioOutputId = outputId;
    }

    /**
     * Update all Faust parameters from VCV params and CV inputs
     */
    void updateFaustParams() {
        // First, apply knob values
        for (const auto& mapping : paramMappings) {
            float value = params[mapping.vcvParamId].getValue();
            faustDsp.setParamValue(mapping.faustParamIdx, value);
        }

        // Then, apply CV modulation
        for (const auto& cv : cvMappings) {
            if (!inputs[cv.vcvInputId].isConnected()) continue;

            float voltage = inputs[cv.vcvInputId].getVoltage();
            float currentValue = faustDsp.getParamValue(cv.faustParamIdx);

            if (cv.exponential) {
                // V/Oct: multiply current value by 2^voltage
                currentValue *= DSP::voltToFreqMultiplier(voltage);
            } else {
                // Linear: add scaled voltage to current value
                currentValue += voltage * cv.scale;
            }

            // Clamp to Faust parameter range
            float minVal = faustDsp.getParamMin(cv.faustParamIdx);
            float maxVal = faustDsp.getParamMax(cv.faustParamIdx);
            currentValue = DSP::clamp(currentValue, minVal, maxVal);

            faustDsp.setParamValue(cv.faustParamIdx, currentValue);
        }
    }

    /**
     * Convert VCV audio voltage (+/-5V) to Faust range (+/-1.0)
     */
    static float vcvToFaust(float voltage) {
        return voltage * 0.2f;  // 5V -> 1.0
    }

    /**
     * Convert Faust output (+/-1.0) to VCV voltage (+/-5V)
     */
    static float faustToVCV(float sample) {
        return sample * 5.0f;  // 1.0 -> 5V
    }

public:
    FaustModule() {
        // Initialize buffer pointers
        for (int i = 0; i < MAX_IO; i++) {
            inputPtrs[i] = &inputBuffer[i];
            outputPtrs[i] = &outputBuffer[i];
        }
    }

    /**
     * Called when sample rate changes
     */
    void onSampleRateChange(const rack::engine::Module::SampleRateChangeEvent& e) override {
        faustDsp.init(static_cast<int>(e.sampleRate));
        initialized = true;
    }

    /**
     * Main audio processing - called every sample
     *
     * Override this in your module if you need custom processing.
     * Default implementation handles simple mono in -> mono out.
     */
    void process(const rack::engine::Module::ProcessArgs& args) override {
        // Initialize DSP on first run
        if (!initialized) {
            faustDsp.init(static_cast<int>(args.sampleRate));
            initialized = true;
        }

        // Update Faust parameters from VCV knobs and CV
        updateFaustParams();

        // Prepare input buffers
        int numInputs = faustDsp.getNumInputs();
        for (int i = 0; i < numInputs && i < MAX_IO; i++) {
            // Check if there's a mapped audio input
            if (audioInputId >= 0 && i == 0) {
                if (inputs[audioInputId].isConnected()) {
                    inputBuffer[i] = vcvToFaust(inputs[audioInputId].getVoltage());
                } else {
                    inputBuffer[i] = 0.0f;
                }
            } else {
                inputBuffer[i] = 0.0f;
            }
        }

        // Process one sample through Faust DSP
        faustDsp.compute(1, inputPtrs, outputPtrs);

        // Write outputs
        int numOutputs = faustDsp.getNumOutputs();
        for (int i = 0; i < numOutputs && i < MAX_IO; i++) {
            if (audioOutputId >= 0 && i == 0) {
                outputs[audioOutputId].setVoltage(faustToVCV(outputBuffer[i]));
            }
        }
    }

    /**
     * Save Faust DSP state to JSON
     */
    json_t* dataToJson() override {
        json_t* rootJ = json_object();

        // Save all Faust parameter values
        int numParams = faustDsp.getNumParams();
        for (int i = 0; i < numParams; i++) {
            const char* path = faustDsp.getParamPath(i);
            float value = faustDsp.getParamValue(i);
            json_object_set_new(rootJ, path, json_real(value));
        }

        return rootJ;
    }

    /**
     * Load Faust DSP state from JSON
     */
    void dataFromJson(json_t* rootJ) override {
        int numParams = faustDsp.getNumParams();
        for (int i = 0; i < numParams; i++) {
            const char* path = faustDsp.getParamPath(i);
            json_t* valueJ = json_object_get(rootJ, path);
            if (valueJ) {
                float value = 0.0f;
                if (json_is_real(valueJ)) {
                    value = static_cast<float>(json_real_value(valueJ));
                } else if (json_is_integer(valueJ)) {
                    value = static_cast<float>(json_integer_value(valueJ));
                } else {
                    continue;
                }
                faustDsp.setParamValue(i, value);
            }
        }
    }
};

} // namespace WiggleRoom
