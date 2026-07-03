/******************************************************************************
 * THE ARCHITECT
 * Poly-Quantizer & Chord Machine
 *
 * 8 independent quantizer tracks + chord generator, all sharing the same
 * global Key/Scale settings. Allows massive, coherent harmonic progressions.
 *
 * Features:
 *   - Global Root (C to B) and Scale (34 scales, including all 12-TET scales from
 *     Ornament & Crime: modes, blues, bebop, exotic/world, jazz symmetric)
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

// Scale definitions - each scale is a 12-bit mask indicating which notes are in the scale.
// Bit n = 1 if semitone n above the root is in the scale (bit 0 = root).
// Built via (1 << semitone) so a Major scale [0,2,4,5,7,9,11] is referenced by its intervals
// rather than a hand-written binary literal (the previous literals had inverted bit order).
//
// Scale set covers all 12-TET scales from Ornament & Crime (braids_quantizer_scales) plus
// common Western/exotic additions.
static const int SCALES[] = {
    // --- Diatonic modes ---
    (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11),         // 0  Major (Ionian)
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),         // 1  Minor (Aeolian)
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<10),         // 2  Dorian
    (1<<0)|(1<<1)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),         // 3  Phrygian
    (1<<0)|(1<<2)|(1<<4)|(1<<6)|(1<<7)|(1<<9)|(1<<11),         // 4  Lydian
    (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<10),         // 5  Mixolydian
    (1<<0)|(1<<1)|(1<<3)|(1<<5)|(1<<6)|(1<<8)|(1<<10),         // 6  Locrian
    // --- Minor variants ---
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<11),         // 7  Harmonic Minor
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<11),         // 8  Melodic Minor (asc)
    (1<<0)|(1<<2)|(1<<3)|(1<<6)|(1<<7)|(1<<8)|(1<<11),         // 9  Hungarian Minor
    (1<<0)|(1<<2)|(1<<3)|(1<<6)|(1<<7)|(1<<9)|(1<<10),         // 10 Romanian Minor
    // --- Pentatonic / blues ---
    (1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<9),                        // 11 Pentatonic Major
    (1<<0)|(1<<3)|(1<<5)|(1<<7)|(1<<10),                       // 12 Pentatonic Minor
    (1<<0)|(1<<2)|(1<<3)|(1<<4)|(1<<7)|(1<<9),                 // 13 Blues Major
    (1<<0)|(1<<3)|(1<<5)|(1<<6)|(1<<7)|(1<<10),                // 14 Blues Minor
    // --- Symmetric ---
    (1<<0)|(1<<2)|(1<<4)|(1<<6)|(1<<8)|(1<<10),                // 15 Whole Tone
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<6)|(1<<8)|(1<<9)|(1<<11),  // 16 Diminished (W-H)
    (1<<0)|(1<<3)|(1<<4)|(1<<7)|(1<<8)|(1<<11),                // 17 Augmented
    // --- Bebop / jazz ---
    (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<8)|(1<<9)|(1<<11),  // 18 Bebop Major
    (1<<0)|(1<<2)|(1<<3)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<10),  // 19 Bebop Dorian
    // --- O_C exotic / world ---
    (1<<0)|(1<<1)|(1<<3)|(1<<4)|(1<<5)|(1<<7)|(1<<8)|(1<<10),  // 20 Folk
    (1<<0)|(1<<2)|(1<<3)|(1<<7)|(1<<8),                        // 21 Hirajoshi
    (1<<0)|(1<<1)|(1<<5)|(1<<7)|(1<<10),                       // 22 In Sen
    (1<<0)|(1<<1)|(1<<5)|(1<<6)|(1<<10),                       // 23 Iwato
    (1<<0)|(1<<2)|(1<<3)|(1<<7)|(1<<9),                        // 24 Kumoi
    (1<<0)|(1<<1)|(1<<3)|(1<<7)|(1<<8),                        // 25 Pelog (Gamelan)
    (1<<0)|(1<<1)|(1<<3)|(1<<4)|(1<<6)|(1<<8)|(1<<11),         // 26 Gypsy
    (1<<0)|(1<<1)|(1<<4)|(1<<5)|(1<<7)|(1<<8)|(1<<11),         // 27 Arabian (Double Harmonic)
    (1<<0)|(1<<1)|(1<<4)|(1<<5)|(1<<7)|(1<<8)|(1<<10),         // 28 Flamenco (Phrygian Dom)
    (1<<0)|(1<<1)|(1<<4)|(1<<5)|(1<<6)|(1<<8)|(1<<11),         // 29 Persian
    (1<<0)|(1<<1)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<11),         // 30 Neapolitan Minor
    (1<<0)|(1<<1)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<11),         // 31 Neapolitan Major
    (1<<0)|(1<<1)|(1<<4)|(1<<6)|(1<<8)|(1<<10)|(1<<11),        // 32 Enigmatic
    // --- Catch-all ---
    0xFFF                                                       // 33 Chromatic
};

static const std::vector<std::string> SCALE_NAMES = {
    "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
    "Harm Min", "Mel Min", "Hung Min", "Rom Min",
    "Pent Maj", "Pent Min", "Blues Maj", "Blues Min",
    "Whole", "Dim W-H", "Augment",
    "Bebop Maj", "Bebop Dor",
    "Folk", "Hirajoshi", "In Sen", "Iwato", "Kumoi", "Pelog",
    "Gypsy", "Arabian", "Flamenco", "Persian", "Neap Min", "Neap Maj", "Enigmatic",
    "Chromatic"
};

static const std::vector<std::string> NOTE_NAMES = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static const int NUM_SCALES = sizeof(SCALES) / sizeof(SCALES[0]);
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

    // Quantize a chromatic note (0-11) to the nearest note in the scale.
    // Returns a signed chroma that may fall outside 0-11 when the nearest
    // in-scale note lies in the adjacent octave (e.g. B wrapping up to the
    // next C in a sparse scale that omits both B and A#). The caller combines
    // this with the octave, so the octave shift is carried correctly.
    int quantizeToScale(int note, int scaleMask, int root) {
        // Normalize note relative to root
        int relNote = (note - root + 12) % 12;

        // Check if note is already in scale
        if ((scaleMask >> relNote) & 1) {
            return note;
        }

        // Find nearest note in scale (prefer lower). Return note +/- offset
        // without wrapping so an upward/downward cross-octave choice keeps
        // its true pitch instead of collapsing back into the current octave.
        for (int offset = 1; offset <= 6; offset++) {
            int lower = (relNote - offset + 12) % 12;
            int upper = (relNote + offset) % 12;

            if ((scaleMask >> lower) & 1) {
                return note - offset;
            }
            if ((scaleMask >> upper) & 1) {
                return note + offset;
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

    // Find the Nth scale degree above a note, returning the number of
    // semitones ascended (not the wrapped chroma). For short scales a high
    // degree such as the 7th spans more than one octave, so returning the
    // absolute semitone offset lets the caller keep chords strictly ascending.
    int findScaleDegree(int startChroma, int degrees, int scaleMask, int root) {
        int current = startChroma;
        int semitones = 0;
        int count = 0;

        while (count < degrees) {
            current = (current + 1) % 12;
            semitones++;
            int relNote = (current - root + 12) % 12;
            if ((scaleMask >> relNote) & 1) {
                count++;
            }
        }

        return semitones;
    }

    // Persist a scale-list schema version. The scale list was reordered and
    // expanded from 13 to 34 entries, so patches saved before this change
    // store SCALE_PARAM indices that now point at different scales. Writing a
    // marker here lets fromJson() detect and migrate legacy patches.
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "scaleSchema", json_integer(1));
        return rootJ;
    }

    void fromJson(json_t* rootJ) override {
        // Restore params (and call dataFromJson if a "data" object exists).
        Module::fromJson(rootJ);

        // Legacy patches predate the scale-list expansion and carry no schema
        // marker (either no "data" object at all, or one without "scaleSchema").
        json_t* dataJ = json_object_get(rootJ, "data");
        json_t* schemaJ = dataJ ? json_object_get(dataJ, "scaleSchema") : nullptr;
        int schema = schemaJ ? static_cast<int>(json_integer_value(schemaJ)) : 0;

        if (schema < 1) {
            // Map the original 13-scale index order onto the new 34-scale list.
            // Indices 0-8 are unchanged; only the tail moved.
            static const int LEGACY_SCALE_REMAP[13] = {
                0, 1, 2, 3, 4, 5, 6, 7, 8,
                11,  // Pent Maj  (was 9)
                12,  // Pent Min  (was 10)
                15,  // Whole     (was 11)
                33   // Chromatic (was 12)
            };
            int oldIdx = static_cast<int>(std::round(params[SCALE_PARAM].getValue()));
            if (oldIdx >= 0 && oldIdx < 13) {
                params[SCALE_PARAM].setValue(static_cast<float>(LEGACY_SCALE_REMAP[oldIdx]));
            }
        }
    }

    void process(const ProcessArgs& args) override {
        // Get root with CV modulation
        int root = static_cast<int>(params[ROOT_PARAM].getValue());
        if (inputs[ROOT_CV_INPUT].isConnected()) {
            root += static_cast<int>(inputs[ROOT_CV_INPUT].getVoltage() * 1.2f);  // ~10V = full range
        }
        root = clamp(root, 0, 11);

        // Get scale with CV modulation (10V sweeps the full scale list)
        int scaleIdx = static_cast<int>(params[SCALE_PARAM].getValue());
        if (inputs[SCALE_CV_INPUT].isConnected()) {
            scaleIdx += static_cast<int>(inputs[SCALE_CV_INPUT].getVoltage() * (NUM_SCALES * 0.1f));
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

            // Find chord tones as semitone offsets above the root (root, 3rd,
            // 5th, 7th). Offsets carry full-octave spans, so chords stay
            // strictly ascending even for short (e.g. pentatonic) scales.
            int thirdSemi = findScaleDegree(rootChroma, 2, scaleMask, root);
            int fifthSemi = findScaleDegree(rootChroma, 4, scaleMask, root);
            int seventhSemi = findScaleDegree(rootChroma, 6, scaleMask, root);

            // Build chord voltages
            int rootMidiBase = rootOctave * 12 + rootChroma + transpose;
            float v1 = (rootMidiBase - 60.f) / 12.f;
            float v2 = (rootMidiBase + thirdSemi - 60.f) / 12.f;
            float v3 = (rootMidiBase + fifthSemi - 60.f) / 12.f;
            float v4 = (rootMidiBase + seventhSemi - 60.f) / 12.f;

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
