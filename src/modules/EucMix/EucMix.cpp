/******************************************************************************
 * EUCMIX
 * 4x4 CV summing matrix
 *
 * Features:
 *   - 4x CV inputs (standalone) OR left expander from chain
 *   - 4x4 toggle switch matrix
 *   - 4x mixed CV outputs
 *   - Output = sum of all inputs whose column switch is on for that row
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include "euclogic/ExpanderMessage.hpp"

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct EucMixModule : Module {
    static constexpr int SIZE = 4;

    enum ParamId {
        ENUMS(MATRIX_PARAM, SIZE * SIZE),  // 16 toggle switches
        PARAMS_LEN
    };

    enum InputId {
        ENUMS(CV_INPUT, SIZE),
        INPUTS_LEN
    };

    enum OutputId {
        ENUMS(CV_OUTPUT, SIZE),
        OUTPUTS_LEN
    };

    enum LightId {
        ENUMS(MATRIX_LIGHT, SIZE * SIZE),
        LIGHTS_LEN
    };

    bool expanderConnected = false;

    EucMixModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        for (int row = 0; row < SIZE; row++) {
            for (int col = 0; col < SIZE; col++) {
                std::string name = "R" + std::to_string(row + 1) + " <- C" + std::to_string(col + 1);
                float defaultVal = (row == col) ? 1.f : 0.f;  // diagonal on by default
                configSwitch(MATRIX_PARAM + row * SIZE + col, 0.f, 1.f, defaultVal, name, {"Off", "On"});
            }
        }

        for (int i = 0; i < SIZE; i++) {
            configInput(CV_INPUT + i, "CV " + std::to_string(i + 1));
            configOutput(CV_OUTPUT + i, "Mix " + std::to_string(i + 1));
        }
    }

    void process(const ProcessArgs& args) override {
        // Get CV values from expander or inputs
        float cvIn[SIZE] = {};
        expanderConnected = false;

        if (leftExpander.module) {
            EuclogicExpanderMessage* msg = static_cast<EuclogicExpanderMessage*>(leftExpander.consumerMessage);
            if (msg && msg->valid) {
                expanderConnected = true;
                for (int i = 0; i < SIZE; i++) {
                    cvIn[i] = msg->cv[i];
                }
            }
        }

        if (!expanderConnected) {
            for (int i = 0; i < SIZE; i++) {
                cvIn[i] = inputs[CV_INPUT + i].getVoltage();
            }
        }

        // Matrix mixing
        for (int row = 0; row < SIZE; row++) {
            float sum = 0.f;
            for (int col = 0; col < SIZE; col++) {
                if (params[MATRIX_PARAM + row * SIZE + col].getValue() > 0.5f) {
                    sum += cvIn[col];
                }
            }
            outputs[CV_OUTPUT + row].setVoltage(DSP::clamp(sum, -10.f, 10.f));
        }

        // Update lights
        for (int row = 0; row < SIZE; row++) {
            for (int col = 0; col < SIZE; col++) {
                lights[MATRIX_LIGHT + row * SIZE + col].setBrightness(
                    params[MATRIX_PARAM + row * SIZE + col].getValue() > 0.5f ? 1.f : 0.f);
            }
        }
    }
};

// ============================================================================
// Widget
// ============================================================================

struct EucMixWidget : ModuleWidget {
    EucMixWidget(EucMixModule* module) {
        setModule(module);
        box.size = Vec(6 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/EucMix.png"), box.size));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float panelWidth = 30.48f;
        float centerX = panelWidth / 2.f;

        // CV Inputs
        float yInputs = 20.f;
        float inputSpacing = 8.f;
        for (int i = 0; i < 4; i++) {
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(centerX - 6.f, yInputs + i * inputSpacing)), module, EucMixModule::CV_INPUT + i));
        }

        // 4x4 matrix of toggle switches
        float yMatrix = 56.f;
        float matrixSpacing = 8.f;
        float matrixStartX = 4.f;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                float x = matrixStartX + col * matrixSpacing;
                float y = yMatrix + row * matrixSpacing;
                addParam(createParamCentered<CKSS>(mm2px(Vec(x, y)), module, EucMixModule::MATRIX_PARAM + row * 4 + col));
            }
        }

        // CV Outputs
        float yOutputs = 100.f;
        for (int i = 0; i < 4; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(centerX + 2.f, yOutputs + i * inputSpacing)), module, EucMixModule::CV_OUTPUT + i));
        }
    }
};

} // namespace WiggleRoom

Model* modelEucMix = createModel<WiggleRoom::EucMixModule, WiggleRoom::EucMixWidget>("EucMix");
