#pragma once
/******************************************************************************
 * EUCMIX CORE
 * NxN CV mixing matrix template
 ******************************************************************************/

#include "rack.hpp"
#include "DSP.hpp"
#include "ImagePanel.hpp"
#include "euclogic/ExpanderMessage.hpp"
#include "euclogic/EuclogicModels.hpp"
#include <cmath>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

template<int N>
struct EucMixModuleT : Module {
    static constexpr int SIZE = N;

    enum ParamId {
        ENUMS(MATRIX_PARAM, N * N),
        PARAMS_LEN
    };

    enum InputId {
        ENUMS(CV_INPUT, N),
        SCALE_BUS_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        ENUMS(CV_OUTPUT, N),
        OUTPUTS_LEN
    };

    enum LightId {
        LIGHTS_LEN
    };

    bool expanderConnected = false;
    int scaleMask = 0xFFF;
    int scaleRoot = 0;
    bool scaleConnected = false;

    EuclogicExpanderMessageT<N> rightMessages[2];

    float quantizeToScale(float voltage) {
        if (!scaleConnected) return voltage;
        float midiNote = voltage * 12.f + 60.f;
        int octave = static_cast<int>(std::floor(midiNote / 12.f));
        int chromaRaw = static_cast<int>(std::round(midiNote)) % 12;
        int chroma = (chromaRaw + 12) % 12;
        int relNote = (chroma - scaleRoot + 12) % 12;
        if ((scaleMask >> relNote) & 1) {
            float qMidi = octave * 12.f + chroma;
            return (qMidi - 60.f) / 12.f;
        }
        for (int offset = 1; offset <= 6; offset++) {
            int lower = (relNote - offset + 12) % 12;
            int upper = (relNote + offset) % 12;
            if ((scaleMask >> lower) & 1) {
                int qChroma = (chroma - offset + 12) % 12;
                float qMidi = octave * 12.f + qChroma;
                return (qMidi - 60.f) / 12.f;
            }
            if ((scaleMask >> upper) & 1) {
                int qChroma = (chroma + offset) % 12;
                float qMidi = octave * 12.f + qChroma;
                return (qMidi - 60.f) / 12.f;
            }
        }
        return voltage;
    }

    EucMixModuleT() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int row = 0; row < N; row++) {
            for (int col = 0; col < N; col++) {
                std::string name = "Out " + std::to_string(row + 1) + " <- In " + std::to_string(col + 1);
                float defaultVal = (row == col) ? 1.f : 0.f;
                configParam(MATRIX_PARAM + row * N + col, 0.f, 1.f, defaultVal, name, "%", 0.f, 100.f);
            }
        }
        for (int i = 0; i < N; i++) {
            configInput(CV_INPUT + i, "CV " + std::to_string(i + 1));
            configOutput(CV_OUTPUT + i, "Mix " + std::to_string(i + 1));
        }
        configInput(SCALE_BUS_INPUT, "Scale Bus");

        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];
    }

    void process(const ProcessArgs& args) override {
        using Models = EuclogicModels<N>;

        if (inputs[SCALE_BUS_INPUT].isConnected()) {
            int channels = inputs[SCALE_BUS_INPUT].getChannels();
            if (channels >= 12) {
                scaleConnected = true;
                scaleMask = 0;
                for (int i = 0; i < 12; i++) {
                    if (inputs[SCALE_BUS_INPUT].getVoltage(i) > 0.5f)
                        scaleMask |= (1 << i);
                }
                if (channels >= 16) {
                    scaleRoot = static_cast<int>(std::round(inputs[SCALE_BUS_INPUT].getVoltage(15) * 12.f)) % 12;
                    if (scaleRoot < 0) scaleRoot += 12;
                }
            }
        } else {
            scaleConnected = false;
        }

        float cvIn[N] = {};
        expanderConnected = false;

        if (leftExpander.module &&
            (leftExpander.module->model == Models::eucSeq() ||
             leftExpander.module->model == Models::logicMangler())) {
            auto* msg = static_cast<EuclogicExpanderMessageT<N>*>(leftExpander.module->rightExpander.consumerMessage);
            if (msg && msg->valid) {
                expanderConnected = true;
                for (int i = 0; i < N; i++)
                    cvIn[i] = msg->cv[i];
            }
        }

        if (!expanderConnected) {
            for (int i = 0; i < N; i++)
                cvIn[i] = inputs[CV_INPUT + i].getVoltage();
        }

        for (int row = 0; row < N; row++) {
            float sum = 0.f;
            float weightSum = 0.f;
            for (int col = 0; col < N; col++) {
                float w = params[MATRIX_PARAM + row * N + col].getValue();
                weightSum += w;
                sum += cvIn[col] * w;
            }
            float out = (weightSum > 0.f) ? sum / weightSum : 0.f;
            outputs[CV_OUTPUT + row].setVoltage(quantizeToScale(DSP::clamp(out, -10.f, 10.f)));
        }

        if (rightExpander.module &&
            rightExpander.module->model == Models::eucBank()) {
            auto* msg = static_cast<EuclogicExpanderMessageT<N>*>(rightExpander.producerMessage);

            if (expanderConnected && leftExpander.module) {
                auto* leftMsg = static_cast<EuclogicExpanderMessageT<N>*>(leftExpander.module->rightExpander.consumerMessage);
                if (leftMsg && leftMsg->valid)
                    *msg = *leftMsg;
                else
                    msg->clear();
            } else {
                msg->clear();
            }
            msg->valid = true;
            rightExpander.requestMessageFlip();
        }
    }
};

template<int N>
struct EucMixWidgetT : ModuleWidget {
    // HP: 2ch=4, 3ch=6, 4ch=8
    static constexpr int HP = (N == 2) ? 4 : (N == 3) ? 6 : 8;
    static constexpr float PANEL_WIDTH_MM = HP * 5.08f;

    EucMixWidgetT(EucMixModuleT<N>* module, const std::string& panelFile) {
        this->setModule(module);
        this->box.size = Vec(HP * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        this->addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, panelFile), this->box.size));

        this->addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        this->addChild(createWidget<ScrewSilver>(Vec(this->box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        this->addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        this->addChild(createWidget<ScrewSilver>(Vec(this->box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Layout: inputs on left (Y-axis), matrix in center, outputs on bottom (X-axis)
        float matrixSpacing = 10.f;
        float portSize = 8.f;  // space for a port
        float matrixStartX = portSize + 3.f;  // left margin for input ports + gap
        float matrixStartY = 20.f;

        // NxN matrix of knobs
        for (int row = 0; row < N; row++) {
            for (int col = 0; col < N; col++) {
                float x = matrixStartX + col * matrixSpacing;
                float y = matrixStartY + row * matrixSpacing;
                this->addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), module, EucMixModuleT<N>::MATRIX_PARAM + row * N + col));
            }
        }

        // CV Inputs on left, aligned with matrix rows
        for (int i = 0; i < N; i++) {
            float y = matrixStartY + i * matrixSpacing;
            this->addInput(createInputCentered<PJ301MPort>(mm2px(Vec(portSize / 2.f + 1.f, y)), module, EucMixModuleT<N>::CV_INPUT + i));
        }

        // CV Outputs on bottom, aligned with matrix columns
        float yOutputs = matrixStartY + N * matrixSpacing + 4.f;
        for (int i = 0; i < N; i++) {
            float x = matrixStartX + i * matrixSpacing;
            this->addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x, yOutputs)), module, EucMixModuleT<N>::CV_OUTPUT + i));
        }

        // Scale bus input below outputs
        float yScaleBus = yOutputs + 10.f;
        this->addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PANEL_WIDTH_MM / 2.f, yScaleBus)), module, EucMixModuleT<N>::SCALE_BUS_INPUT));
    }
};

} // namespace WiggleRoom
