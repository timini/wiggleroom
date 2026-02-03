#pragma once

#include "rack.hpp"
#include "GearBuffer.hpp"
#include <random>
#include <cmath>

namespace WiggleRoom {

/**
 * InterferenceEngine - Coordinates two gears to produce melodic sequences
 *
 * The melody emerges from mathematical interference between:
 * - Gear A (Anchor): 16-step pitch pattern (the "riff")
 * - Gear B (Modifier): Variable-length offset pattern (harmonic variation)
 *
 * Output pitch = quantize(GearA[i] + GearB[j], scaleMask)
 */
class InterferenceEngine {
public:
    // Scale definitions (same as TheArchitect)
    static constexpr int SCALES[] = {
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

    static constexpr int NUM_SCALES = 13;

    // Valid lengths for Gear B (prime numbers for interesting interference)
    static constexpr int GEAR_B_LENGTHS[] = {3, 5, 7, 9, 11, 13};
    static constexpr int NUM_GEAR_B_LENGTHS = 6;

    InterferenceEngine()
        : gearA_(16, GearBuffer::DataType::PITCH)
        , gearB_(7, GearBuffer::DataType::OFFSET)
        , offset_(0)
        , frozen_(false)
        , root_(0)
        , scaleIndex_(0)
        , scaleMask_(SCALES[0])
        , useScaleBus_(false)
        , rng_(std::random_device{}())
    {
        // Initialize Gear A with a default melodic pattern
        initializeGearA();

        // Initialize Gear B with some offsets
        initializeGearB();
    }

    // Process one clock tick
    void onClock() {
        // Store previous values for logic engine
        prevGearAValue_ = gearA_.getPitch();
        prevGearBOffset_ = getCurrentGearBOffset();

        // Advance gears
        gearA_.advance();

        if (!frozen_) {
            gearB_.advance();
        }

        // Calculate raw pitch (sum of Gear A and Gear B with offset)
        int rawPitch = gearA_.getPitch() + getCurrentGearBOffset();

        // Store previous pitch for logic engine
        prevPrevPitch_ = prevPitch_;
        prevPitch_ = quantizedPitch_;

        // Quantize to scale
        quantizedPitch_ = quantizeToScale(rawPitch);
    }

    // Reset all gears to position 0
    void reset() {
        gearA_.reset();
        gearB_.reset();
        prevPitch_ = 12;
        prevPrevPitch_ = 12;
        quantizedPitch_ = 12;
        prevGearAValue_ = 12;
        prevGearBOffset_ = 0;
    }

    // Get current quantized pitch as V/Oct (0V = C4)
    float getPitchVoltage() const {
        // quantizedPitch_ is 0-24 semitones, centered around middle C
        // 0 = C3, 12 = C4, 24 = C5
        return (quantizedPitch_ - 12.0f) / 12.0f;
    }

    // Get raw pitch (before quantization)
    int getRawPitch() const {
        return gearA_.getPitch() + getCurrentGearBOffset();
    }

    // Get quantized pitch in semitones
    int getQuantizedPitch() const {
        return quantizedPitch_;
    }

    // Get previous quantized pitch
    int getPrevPitch() const {
        return prevPitch_;
    }

    // Get pitch from two steps ago
    int getPrevPrevPitch() const {
        return prevPrevPitch_;
    }

    // Get current Gear A value (for logic engine)
    int getGearAValue() const {
        return gearA_.getPitch();
    }

    // Get previous Gear A value (for logic engine)
    int getPrevGearAValue() const {
        return prevGearAValue_;
    }

    // Get current Gear B offset (for logic engine)
    int getCurrentGearBOffset() const {
        int effectivePos = (gearB_.getPosition() + offset_) % gearB_.getLength();
        return gearB_.getValueAt(effectivePos);
    }

    // Get previous Gear B offset (for logic engine)
    int getPrevGearBOffset() const {
        return prevGearBOffset_;
    }

    // Set Gear B length (uses predefined prime lengths)
    void setGearBLengthIndex(int index) {
        index = std::min(std::max(index, 0), NUM_GEAR_B_LENGTHS - 1);
        gearB_.setLength(GEAR_B_LENGTHS[index]);
    }

    // Get current Gear B length
    int getGearBLength() const {
        return gearB_.getLength();
    }

    // Set phase offset for Gear B
    void setOffset(int off) {
        offset_ = off % 16;
    }

    // Freeze/unfreeze Gear B rotation
    void setFrozen(bool freeze) {
        frozen_ = freeze;
    }

    // Set root note (0-11)
    void setRoot(int root) {
        root_ = std::min(std::max(root, 0), 11);
    }

    // Set scale by index (0-12)
    void setScale(int scaleIdx) {
        scaleIndex_ = std::min(std::max(scaleIdx, 0), NUM_SCALES - 1);
        if (!useScaleBus_) {
            scaleMask_ = SCALES[scaleIndex_];
        }
    }

    // Update from Scale Bus (16-channel poly)
    void updateFromScaleBus(const float* voltages, int channels) {
        if (channels < 12) {
            useScaleBus_ = false;
            scaleMask_ = SCALES[scaleIndex_];
            return;
        }

        useScaleBus_ = true;

        // Reconstruct scale mask from channels 0-11
        int mask = 0;
        for (int i = 0; i < 12; i++) {
            if (voltages[i] > 0.5f) {
                mask |= (1 << i);
            }
        }
        scaleMask_ = mask;

        // Channel 15 is root V/Oct - extract root note
        if (channels >= 16) {
            float rootVolt = voltages[15];
            int rootNote = static_cast<int>(std::round(rootVolt * 12.0f)) % 12;
            if (rootNote < 0) rootNote += 12;
            root_ = rootNote;
        }
    }

    // Randomize Gear A
    void mutateGearA() {
        gearA_.randomize(rng_);
    }

    // Randomize Gear B
    void mutateGearB() {
        gearB_.randomize(rng_);
    }

    // Access to gears for visualization
    const GearBuffer& getGearA() const { return gearA_; }
    const GearBuffer& getGearB() const { return gearB_; }

    // Get current scale mask for display
    int getScaleMask() const { return scaleMask_; }
    int getRoot() const { return root_; }
    bool isUsingScaleBus() const { return useScaleBus_; }

    // Serialization
    json_t* toJson() const {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "gearA", gearA_.toJson());
        json_object_set_new(rootJ, "gearB", gearB_.toJson());
        json_object_set_new(rootJ, "offset", json_integer(offset_));
        return rootJ;
    }

    void fromJson(json_t* rootJ) {
        json_t* gearAJ = json_object_get(rootJ, "gearA");
        if (gearAJ) gearA_.fromJson(gearAJ);

        json_t* gearBJ = json_object_get(rootJ, "gearB");
        if (gearBJ) gearB_.fromJson(gearBJ);

        json_t* offsetJ = json_object_get(rootJ, "offset");
        if (offsetJ) offset_ = (int)json_integer_value(offsetJ);
    }

private:
    GearBuffer gearA_;  // Anchor - 16-step pitch
    GearBuffer gearB_;  // Modifier - variable offset

    int offset_;        // Phase offset for Gear B
    bool frozen_;       // Freeze Gear B rotation

    int root_;          // Root note (0-11)
    int scaleIndex_;    // Fallback scale index
    int scaleMask_;     // Active scale mask
    bool useScaleBus_;  // Using external scale bus

    int quantizedPitch_ = 12;   // Current quantized pitch
    int prevPitch_ = 12;        // Previous pitch
    int prevPrevPitch_ = 12;    // Pitch before previous
    int prevGearAValue_ = 12;   // Previous Gear A value
    int prevGearBOffset_ = 0;   // Previous Gear B offset

    std::mt19937 rng_;

    void initializeGearA() {
        // Initialize with a simple melodic pattern (in semitones)
        // Pattern: C E G E C E A G (repeating with variations)
        static const int defaultPattern[] = {
            0, 4, 7, 4, 0, 4, 9, 7,
            2, 5, 9, 5, 2, 5, 7, 5
        };
        for (int i = 0; i < 16; i++) {
            gearA_.setValueAt(i, defaultPattern[i] + 12);  // Offset to center around C4
        }
    }

    void initializeGearB() {
        // Initialize with some offset variation
        static const int defaultOffsets[] = {
            0, 0, 2, -2, 0, 3, 0,  // 7-step pattern
            0, 0, 0, 0, 0, 0, 0, 0, 0  // Padding
        };
        for (int i = 0; i < 7; i++) {
            gearB_.setValueAt(i, defaultOffsets[i]);
        }
    }

    // Quantize pitch to scale
    int quantizeToScale(int pitch) {
        // Clamp to valid range
        pitch = std::min(std::max(pitch, 0), 36);

        // Get octave and note within octave
        int octave = pitch / 12;
        int note = pitch % 12;

        // Transpose relative to root
        int relNote = (note - root_ + 12) % 12;

        // Check if note is in scale
        if ((scaleMask_ >> relNote) & 1) {
            return pitch;
        }

        // Find nearest note in scale (prefer lower)
        for (int off = 1; off <= 6; off++) {
            int lower = (relNote - off + 12) % 12;
            int upper = (relNote + off) % 12;

            if ((scaleMask_ >> lower) & 1) {
                int newNote = (note - off + 12);
                if (newNote < 0) {
                    newNote += 12;
                    if (octave > 0) octave--;
                }
                return octave * 12 + (newNote % 12);
            }
            if ((scaleMask_ >> upper) & 1) {
                int newNote = note + off;
                if (newNote >= 12) {
                    newNote -= 12;
                    octave++;
                }
                return octave * 12 + newNote;
            }
        }

        return pitch;  // Fallback
    }
};

// Need to define constexpr array outside class for linkage
constexpr int InterferenceEngine::SCALES[];
constexpr int InterferenceEngine::GEAR_B_LENGTHS[];

} // namespace WiggleRoom
