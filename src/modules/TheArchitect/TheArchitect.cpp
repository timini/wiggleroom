/******************************************************************************
 * THE ARCHITECT
 * Poly-Quantizer & Chord Machine
 *
 * 8 independent quantizer tracks + chord generator, all sharing the same
 * global Key/Scale settings. Allows massive, coherent harmonic progressions.
 *
 * Features:
 *   - Global Root (C to B) and Scale (13 scales)
 *   - 8 mono V/Oct inputs → 8 quantized outputs
 *   - Chord input → 4-voice poly output (Root, 3rd, 5th, 7th)
 *   - Chord inversions (0-3)
 *   - Transpose control
 *   - CV control for Root, Scale, and Inversion
 ******************************************************************************/

#include "rack.hpp"
#include "ImagePanel.hpp"
#include <cmath>
#include <vector>
#include <string>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

// Scale definitions - each scale is a 12-bit mask indicating which notes are in the scale
// Bit 0 = root, Bit 1 = minor 2nd, etc.
static const int SCALES[] = {
    0b101011010101,  // Major (Ionian)
    0b101101010110,  // Minor (Aeolian)
    0b101101011010,  // Dorian
    0b110101011010,  // Phrygian
    0b101011010110,  // Lydian
    0b101011011010,  // Mixolydian
    0b110101101010,  // Locrian
    0b101101010101,  // Harmonic Minor
    0b101011010110,  // Melodic Minor
    0b101010110101,  // Pentatonic Major
    0b100101010010,  // Pentatonic Minor
    0b101010101010,  // Whole Tone
    0b111111111111   // Chromatic
};

static const std::vector<std::string> SCALE_NAMES = {
    "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
    "Locrian", "Harm Min", "Mel Min", "Pent Maj", "Pent Min", "Whole", "Chromatic"
};

static const std::vector<std::string> NOTE_NAMES = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static const int NUM_SCALES = 13;
static const int NUM_TRACKS = 8;

struct TheArchitect : Module {
    enum ParamId {
        ROOT_PARAM,
        SCALE_PARAM,
        TRANSPOSE_PARAM,
        INVERSION_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        ENUMS(TRACK_INPUT, NUM_TRACKS),
        CHORD_INPUT,
        ROOT_CV_INPUT,
        SCALE_CV_INPUT,
        INVERSION_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        ENUMS(TRACK_OUTPUT, NUM_TRACKS),
        CHORD_OUTPUT,
        SCALE_BUS_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        ENUMS(SCALE_LIGHT, 12),  // Show active scale notes
        LIGHTS_LEN
    };

    TheArchitect() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Root note (0-11)
        configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root", NOTE_NAMES);

        // Scale type
        configSwitch(SCALE_PARAM, 0.f, NUM_SCALES - 1, 0.f, "Scale", SCALE_NAMES);

        // Transpose (-24 to +24 semitones)
        auto* transposeParam = configParam(TRANSPOSE_PARAM, -24.f, 24.f, 0.f, "Transpose", " semitones");
        transposeParam->snapEnabled = true;

        // Chord inversion (0-3)
        configSwitch(INVERSION_PARAM, 0.f, 3.f, 0.f, "Inversion", {"Root", "1st", "2nd", "3rd"});

        // Configure track inputs/outputs
        for (int i = 0; i < NUM_TRACKS; i++) {
            configInput(TRACK_INPUT + i, "Track " + std::to_string(i + 1) + " V/Oct");
            configOutput(TRACK_OUTPUT + i, "Track " + std::to_string(i + 1) + " Quantized");
        }

        // Chord I/O
        configInput(CHORD_INPUT, "Chord V/Oct");
        configOutput(CHORD_OUTPUT, "Chord (4-voice poly)");

        // CV inputs
        configInput(ROOT_CV_INPUT, "Root CV");
        configInput(SCALE_CV_INPUT, "Scale CV");
        configInput(INVERSION_CV_INPUT, "Inversion CV");

        // Scale Bus output (16-channel poly: 0-11 = scale mask, 15 = root V/Oct)
        configOutput(SCALE_BUS_OUTPUT, "Scale Bus (16ch poly)");
    }

    // Quantize a chromatic note (0-11) to the nearest note in the scale
    int quantizeToScale(int note, int scaleMask, int root) {
        // Normalize note relative to root
        int relNote = (note - root + 12) % 12;

        // Check if note is already in scale
        if ((scaleMask >> relNote) & 1) {
            return note;
        }

        // Find nearest note in scale (prefer lower)
        for (int offset = 1; offset <= 6; offset++) {
            int lower = (relNote - offset + 12) % 12;
            int upper = (relNote + offset) % 12;

            if ((scaleMask >> lower) & 1) {
                return (note - offset + 12) % 12 + (note / 12) * 12;
            }
            if ((scaleMask >> upper) & 1) {
                return (note + offset) % 12 + (note / 12) * 12;
            }
        }

        return note;  // Fallback
    }

    // Quantize a V/Oct voltage to the scale
    float quantizeVoltage(float voltage, int root, int scaleMask, int transpose) {
        // Convert voltage to MIDI note number (0V = C4 = 60)
        float midiNote = voltage * 12.f + 60.f;
        int octave = static_cast<int>(std::floor(midiNote / 12.f));
        int chromaRaw = static_cast<int>(std::round(midiNote)) % 12;
        int chroma = (chromaRaw + 12) % 12;  // Ensure positive

        // Quantize to scale
        int quantizedChroma = quantizeToScale(chroma, scaleMask, root);

        // Rebuild voltage
        float quantizedMidi = octave * 12.f + quantizedChroma + transpose;
        return (quantizedMidi - 60.f) / 12.f;
    }

    // Find the Nth scale degree above a note
    int findScaleDegree(int startChroma, int degrees, int scaleMask, int root) {
        int current = startChroma;
        int count = 0;

        while (count < degrees) {
            current = (current + 1) % 12;
            int relNote = (current - root + 12) % 12;
            if ((scaleMask >> relNote) & 1) {
                count++;
            }
        }

        return current;
    }

    void process(const ProcessArgs& args) override {
        // Get root with CV modulation
        int root = static_cast<int>(params[ROOT_PARAM].getValue());
        if (inputs[ROOT_CV_INPUT].isConnected()) {
            root += static_cast<int>(inputs[ROOT_CV_INPUT].getVoltage() * 1.2f);  // ~10V = full range
        }
        root = clamp(root, 0, 11);

        // Get scale with CV modulation
        int scaleIdx = static_cast<int>(params[SCALE_PARAM].getValue());
        if (inputs[SCALE_CV_INPUT].isConnected()) {
            scaleIdx += static_cast<int>(inputs[SCALE_CV_INPUT].getVoltage() * 1.3f);
        }
        scaleIdx = clamp(scaleIdx, 0, NUM_SCALES - 1);
        int scaleMask = SCALES[scaleIdx];

        // Get transpose
        int transpose = static_cast<int>(params[TRANSPOSE_PARAM].getValue());

        // Get inversion with CV
        int inversion = static_cast<int>(params[INVERSION_PARAM].getValue());
        if (inputs[INVERSION_CV_INPUT].isConnected()) {
            inversion += static_cast<int>(inputs[INVERSION_CV_INPUT].getVoltage() * 0.4f);
        }
        inversion = clamp(inversion, 0, 3);

        // Update scale lights
        for (int i = 0; i < 12; i++) {
            int relNote = (i - root + 12) % 12;
            bool inScale = (scaleMask >> relNote) & 1;
            lights[SCALE_LIGHT + i].setBrightness(inScale ? 1.f : 0.1f);
        }

        // Process 8 quantizer tracks
        for (int i = 0; i < NUM_TRACKS; i++) {
            if (inputs[TRACK_INPUT + i].isConnected()) {
                float inVoltage = inputs[TRACK_INPUT + i].getVoltage();
                float outVoltage = quantizeVoltage(inVoltage, root, scaleMask, transpose);
                outputs[TRACK_OUTPUT + i].setVoltage(outVoltage);
            } else {
                outputs[TRACK_OUTPUT + i].setVoltage(0.f);
            }
        }

        // Process chord generator
        if (inputs[CHORD_INPUT].isConnected()) {
            float chordIn = inputs[CHORD_INPUT].getVoltage();

            // Quantize root
            float rootVoltage = quantizeVoltage(chordIn, root, scaleMask, transpose);

            // Convert to MIDI for chord building
            float rootMidi = rootVoltage * 12.f + 60.f;
            int rootChroma = static_cast<int>(std::round(rootMidi)) % 12;
            if (rootChroma < 0) rootChroma += 12;
            int rootOctave = static_cast<int>(std::floor(rootMidi / 12.f));

            // Find chord tones (scale degrees: root, 3rd, 5th, 7th)
            int thirdChroma = findScaleDegree(rootChroma, 2, scaleMask, root);
            int fifthChroma = findScaleDegree(rootChroma, 4, scaleMask, root);
            int seventhChroma = findScaleDegree(rootChroma, 6, scaleMask, root);

            // Calculate octaves (handle wrapping)
            int thirdOctave = rootOctave + (thirdChroma < rootChroma ? 1 : 0);
            int fifthOctave = rootOctave + (fifthChroma < rootChroma ? 1 : 0);
            int seventhOctave = rootOctave + (seventhChroma < rootChroma ? 1 : 0);

            // Build chord voltages
            float v1 = (rootOctave * 12.f + rootChroma + transpose - 60.f) / 12.f;
            float v2 = (thirdOctave * 12.f + thirdChroma + transpose - 60.f) / 12.f;
            float v3 = (fifthOctave * 12.f + fifthChroma + transpose - 60.f) / 12.f;
            float v4 = (seventhOctave * 12.f + seventhChroma + transpose - 60.f) / 12.f;

            // Apply inversions
            if (inversion >= 1) v1 += 1.f;  // Root up an octave
            if (inversion >= 2) v2 += 1.f;  // 3rd up an octave
            if (inversion >= 3) v3 += 1.f;  // 5th up an octave

            // Output as 4-channel poly
            outputs[CHORD_OUTPUT].setChannels(4);
            outputs[CHORD_OUTPUT].setVoltage(v1, 0);
            outputs[CHORD_OUTPUT].setVoltage(v2, 1);
            outputs[CHORD_OUTPUT].setVoltage(v3, 2);
            outputs[CHORD_OUTPUT].setVoltage(v4, 3);
        } else {
            outputs[CHORD_OUTPUT].setChannels(0);
        }

        // Output Scale Bus (16-channel polyphonic)
        // Channels 0-11: Scale mask (10V = note active, 0V = inactive)
        // Channel 15: Root note as V/Oct (0V = C, 1/12V = C#, etc.)
        outputs[SCALE_BUS_OUTPUT].setChannels(16);

        // Write scale mask to channels 0-11
        // The mask is relative to the root, but for the bus we output
        // the absolute chromatic positions
        for (int i = 0; i < 12; i++) {
            int relNote = (i - root + 12) % 12;
            bool inScale = (scaleMask >> relNote) & 1;
            outputs[SCALE_BUS_OUTPUT].setVoltage(inScale ? 10.f : 0.f, i);
        }

        // Write root note to channel 15 (as V/Oct: 0V = C4)
        // Root is 0-11 where 0=C, so root/12 gives the V/Oct offset
        float rootVoltage = static_cast<float>(root) / 12.f;
        outputs[SCALE_BUS_OUTPUT].setVoltage(rootVoltage, 15);
    }
};

struct TheArchitectWidget : ModuleWidget {
    TheArchitectWidget(TheArchitect* module) {
        setModule(module);
        box.size = Vec(16 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/TheArchitect.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Layout for 16HP panel (81.28mm), center = 40.64mm
        // Left column: 8 track inputs
        // Center: Root, Scale, Transpose knobs + scale lights
        // Right column: 8 track outputs + chord section

        float trackInX = 18.f;      // Moved inward from 8mm
        float trackOutX = 63.f;     // Moved inward from 73mm
        float centerX = 40.64f;

        // Global controls at top center (below title artwork ~35mm)
        addParam(createParamCentered<RoundBigBlackKnob>(
            mm2px(Vec(32, 35)), module, TheArchitect::ROOT_PARAM));
        addParam(createParamCentered<RoundBigBlackKnob>(
            mm2px(Vec(49, 35)), module, TheArchitect::SCALE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(centerX, 50)), module, TheArchitect::TRANSPOSE_PARAM));

        // Scale lights (show which notes are active) - centered
        float lightStartY = 58.f;
        for (int i = 0; i < 12; i++) {
            float x = 27.f + (i % 6) * 4.5f;
            float y = lightStartY + (i / 6) * 5.f;
            addChild(createLightCentered<SmallLight<GreenLight>>(
                mm2px(Vec(x, y)), module, TheArchitect::SCALE_LIGHT + i));
        }

        // 8 track inputs (left column) and outputs (right column)
        // Tighter spacing to fit in usable panel area (62-104mm)
        float trackStartY = 62.f;
        float trackSpacing = 6.f;
        for (int i = 0; i < NUM_TRACKS; i++) {
            float y = trackStartY + i * trackSpacing;
            addInput(createInputCentered<PJ301MPort>(
                mm2px(Vec(trackInX, y)), module, TheArchitect::TRACK_INPUT + i));
            addOutput(createOutputCentered<PJ301MPort>(
                mm2px(Vec(trackOutX, y)), module, TheArchitect::TRACK_OUTPUT + i));
        }

        // Center column: CV inputs aligned with track 1
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(28, 62)), module, TheArchitect::ROOT_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(centerX, 62)), module, TheArchitect::SCALE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(53, 62)), module, TheArchitect::CHORD_INPUT));

        // Center column: Inversion knob and CV
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(centerX, 77)), module, TheArchitect::INVERSION_PARAM));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(centerX, 89)), module, TheArchitect::INVERSION_CV_INPUT));

        // Bottom outputs: Chord and Scale Bus (aligned with track 8)
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(33, 104)), module, TheArchitect::CHORD_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(48, 104)), module, TheArchitect::SCALE_BUS_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelTheArchitect = createModel<WiggleRoom::TheArchitect, WiggleRoom::TheArchitectWidget>("TheArchitect");
