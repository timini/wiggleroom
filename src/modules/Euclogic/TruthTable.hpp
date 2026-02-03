#pragma once
/******************************************************************************
 * TRUTH TABLE
 * 4-input boolean logic with 16-state truth table
 * No VCV Rack dependencies - fully testable standalone
 ******************************************************************************/

#include <array>
#include <vector>
#include <cstdint>
#include <random>

namespace WiggleRoom {

struct TruthTable {
    // 16 output states: index is 4-bit input (Ch0=bit0, Ch1=bit1, Ch2=bit2, Ch3=bit3)
    // Each state is a 4-bit output mask
    std::array<uint8_t, 16> mapping{};

    // Unlimited undo history
    std::vector<std::array<uint8_t, 16>> undoHistory;

    // RNG for randomization (seeded for deterministic testing)
    std::mt19937 rng;

    TruthTable() {
        // Default: pass-through (output mirrors input)
        for (int i = 0; i < 16; i++) {
            mapping[i] = static_cast<uint8_t>(i);
        }
        // Seed with random device by default
        std::random_device rd;
        rng.seed(rd());
    }

    // Set RNG seed for deterministic testing
    void setSeed(uint32_t seed) {
        rng.seed(seed);
    }

    // Evaluate: given 4 input bools, return 4 output bools
    void evaluate(const bool inputs[4], bool outputs[4]) const {
        uint8_t index = (inputs[0] ? 1 : 0) |
                        (inputs[1] ? 2 : 0) |
                        (inputs[2] ? 4 : 0) |
                        (inputs[3] ? 8 : 0);

        uint8_t outMask = mapping[index];

        outputs[0] = (outMask & 1) != 0;
        outputs[1] = (outMask & 2) != 0;
        outputs[2] = (outMask & 4) != 0;
        outputs[3] = (outMask & 8) != 0;
    }

    // Overload for convenience
    std::array<bool, 4> evaluate(bool in0, bool in1, bool in2, bool in3) const {
        bool inputs[4] = {in0, in1, in2, in3};
        bool outputs[4];
        evaluate(inputs, outputs);
        return {outputs[0], outputs[1], outputs[2], outputs[3]};
    }

    // Set output mask for given input state (0-15)
    void setMapping(uint8_t inputState, uint8_t outputMask) {
        if (inputState < 16) {
            mapping[inputState] = outputMask & 0x0F;
        }
    }

    // Get output mask for given input state
    uint8_t getMapping(uint8_t inputState) const {
        if (inputState < 16) {
            return mapping[inputState];
        }
        return 0;
    }

    // Toggle a single output bit for a given input state
    void toggleBit(uint8_t inputState, uint8_t outputBit) {
        if (inputState < 16 && outputBit < 4) {
            mapping[inputState] ^= (1 << outputBit);
        }
    }

    // Save current state to undo history
    void pushUndo() {
        undoHistory.push_back(mapping);
    }

    // Restore previous state from undo history
    bool popUndo() {
        if (undoHistory.empty()) return false;
        mapping = undoHistory.back();
        undoHistory.pop_back();
        return true;
    }

    // Clear undo history
    void clearUndo() {
        undoHistory.clear();
    }

    // Randomize entire truth table
    void randomize() {
        pushUndo();
        std::uniform_int_distribution<int> dist(0, 15);
        for (int i = 0; i < 16; i++) {
            mapping[i] = static_cast<uint8_t>(dist(rng));
        }
    }

    // Mutate: flip 1-3 random bits in random entries
    void mutate() {
        pushUndo();
        std::uniform_int_distribution<int> entryDist(0, 15);
        std::uniform_int_distribution<int> bitDist(0, 3);
        std::uniform_int_distribution<int> countDist(1, 3);

        int numFlips = countDist(rng);
        for (int i = 0; i < numFlips; i++) {
            int entry = entryDist(rng);
            int bit = bitDist(rng);
            mapping[entry] ^= (1 << bit);
        }
    }

    // Undo last change
    void undo() {
        popUndo();
    }

    // Load preset logic functions
    void loadPreset(const char* name) {
        pushUndo();

        if (strcmp(name, "PASS") == 0 || strcmp(name, "pass") == 0) {
            // Pass-through: output = input
            for (int i = 0; i < 16; i++) {
                mapping[i] = static_cast<uint8_t>(i);
            }
        }
        else if (strcmp(name, "OR") == 0 || strcmp(name, "or") == 0) {
            // OR: all outputs true if any input true
            for (int i = 0; i < 16; i++) {
                mapping[i] = (i > 0) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "AND") == 0 || strcmp(name, "and") == 0) {
            // AND: all outputs true only if all inputs true
            for (int i = 0; i < 16; i++) {
                mapping[i] = (i == 15) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "XOR") == 0 || strcmp(name, "xor") == 0) {
            // XOR: all outputs true if odd number of inputs
            for (int i = 0; i < 16; i++) {
                int popcount = __builtin_popcount(i);
                mapping[i] = (popcount % 2 == 1) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "MAJORITY") == 0 || strcmp(name, "majority") == 0) {
            // MAJORITY: all outputs true if 2+ inputs true
            for (int i = 0; i < 16; i++) {
                int popcount = __builtin_popcount(i);
                mapping[i] = (popcount >= 2) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "NOR") == 0 || strcmp(name, "nor") == 0) {
            // NOR: all outputs true only if no inputs true
            for (int i = 0; i < 16; i++) {
                mapping[i] = (i == 0) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "NAND") == 0 || strcmp(name, "nand") == 0) {
            // NAND: all outputs true unless all inputs true
            for (int i = 0; i < 16; i++) {
                mapping[i] = (i != 15) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "ROTATE") == 0 || strcmp(name, "rotate") == 0) {
            // ROTATE: shift outputs (0→1, 1→2, 2→3, 3→0)
            for (int i = 0; i < 16; i++) {
                uint8_t rotated = ((i << 1) | (i >> 3)) & 0x0F;
                mapping[i] = rotated;
            }
        }
        else if (strcmp(name, "INVERT") == 0 || strcmp(name, "invert") == 0) {
            // INVERT: output = NOT input
            for (int i = 0; i < 16; i++) {
                mapping[i] = (~i) & 0x0F;
            }
        }
    }

    // Serialize to array for JSON
    std::array<uint8_t, 16> serialize() const {
        return mapping;
    }

    // Deserialize from array
    void deserialize(const std::array<uint8_t, 16>& data) {
        mapping = data;
    }
};

} // namespace WiggleRoom
