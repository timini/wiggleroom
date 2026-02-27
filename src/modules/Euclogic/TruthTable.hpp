#pragma once
/******************************************************************************
 * TRUTH TABLE
 * N-input boolean logic with 2^N-state truth table
 * Templated on channel count. No VCV Rack dependencies - fully testable standalone
 ******************************************************************************/

#include <array>
#include <vector>
#include <cstdint>
#include <random>
#include <cstring>
#include <algorithm>

namespace WiggleRoom {

template<int N_CHANNELS>
struct TruthTableT {
    static constexpr int N_STATES = (1 << N_CHANNELS);
    static constexpr int N_BITS = N_CHANNELS;
    static constexpr uint8_t OUTPUT_MASK = static_cast<uint8_t>((1 << N_CHANNELS) - 1);

    // Output states: index is N_CHANNELS-bit input, each state is an N_CHANNELS-bit output mask
    std::array<uint8_t, N_STATES> mapping{};

    // Unlimited undo/redo history
    std::vector<std::array<uint8_t, N_STATES>> undoHistory;
    std::vector<std::array<uint8_t, N_STATES>> redoHistory;

    // RNG for randomization
    std::mt19937 rng;

    TruthTableT() {
        // Default: pass-through (output mirrors input)
        for (int i = 0; i < N_STATES; i++) {
            mapping[i] = static_cast<uint8_t>(i) & OUTPUT_MASK;
        }
        std::random_device rd;
        rng.seed(rd());
    }

    void setSeed(uint32_t seed) {
        rng.seed(seed);
    }

    // Evaluate: given N input bools, return N output bools
    void evaluate(const bool inputs[N_CHANNELS], bool outputs[N_CHANNELS]) const {
        uint8_t index = 0;
        for (int i = 0; i < N_CHANNELS; i++) {
            if (inputs[i]) index |= (1 << i);
        }
        uint8_t outMask = mapping[index];
        for (int i = 0; i < N_CHANNELS; i++) {
            outputs[i] = (outMask >> i) & 1;
        }
    }

    // Overload for 4-channel convenience (backward compat)
    template<int M = N_CHANNELS, typename std::enable_if<M == 4, int>::type = 0>
    std::array<bool, 4> evaluate(bool in0, bool in1, bool in2, bool in3) const {
        bool inputs[4] = {in0, in1, in2, in3};
        bool outputs[4];
        evaluate(inputs, outputs);
        return {outputs[0], outputs[1], outputs[2], outputs[3]};
    }

    void setMapping(uint8_t inputState, uint8_t outputMask) {
        if (inputState < N_STATES) {
            mapping[inputState] = outputMask & OUTPUT_MASK;
        }
    }

    uint8_t getMapping(uint8_t inputState) const {
        if (inputState < N_STATES) {
            return mapping[inputState];
        }
        return 0;
    }

    // Also accept int overload (used by displays)
    uint8_t getMapping(int inputState) const {
        return getMapping(static_cast<uint8_t>(inputState));
    }

    void toggleBit(uint8_t inputState, uint8_t outputBit) {
        if (inputState < N_STATES && outputBit < N_CHANNELS) {
            mapping[inputState] ^= (1 << outputBit);
        }
    }

    void toggleBit(int inputState, int outputBit) {
        toggleBit(static_cast<uint8_t>(inputState), static_cast<uint8_t>(outputBit));
    }

    void pushUndo() {
        undoHistory.push_back(mapping);
        redoHistory.clear();
    }

    bool undo() {
        if (undoHistory.empty()) return false;
        redoHistory.push_back(mapping);
        mapping = undoHistory.back();
        undoHistory.pop_back();
        return true;
    }

    bool redo() {
        if (redoHistory.empty()) return false;
        undoHistory.push_back(mapping);
        mapping = redoHistory.back();
        redoHistory.pop_back();
        return true;
    }

    void clearHistory() {
        undoHistory.clear();
        redoHistory.clear();
    }

    void randomize() {
        pushUndo();
        std::uniform_int_distribution<int> dist(0, OUTPUT_MASK);
        for (int i = 0; i < N_STATES; i++) {
            mapping[i] = static_cast<uint8_t>(dist(rng));
        }
    }

    void mutate() {
        pushUndo();
        std::uniform_int_distribution<int> entryDist(0, N_STATES - 1);
        std::uniform_int_distribution<int> bitDist(0, N_CHANNELS - 1);
        std::uniform_int_distribution<int> countDist(1, std::min(3, N_CHANNELS));

        int numFlips = countDist(rng);
        for (int i = 0; i < numFlips; i++) {
            int entry = entryDist(rng);
            int bit = bitDist(rng);
            mapping[entry] ^= (1 << bit);
        }
    }

    std::array<uint8_t, N_STATES> serialize() const {
        return mapping;
    }

    void deserialize(const std::array<uint8_t, N_STATES>& data) {
        mapping = data;
    }
};

// TruthTable: 4-channel version with preset support (backward compatible)
struct TruthTable : TruthTableT<4> {
    // Inherit everything from TruthTableT<4>
    using TruthTableT<4>::TruthTableT;

    // Load preset logic functions (4-channel only)
    void loadPreset(const char* name) {
        pushUndo();

        if (strcmp(name, "PASS") == 0 || strcmp(name, "pass") == 0) {
            for (int i = 0; i < 16; i++) {
                mapping[i] = static_cast<uint8_t>(i);
            }
        }
        else if (strcmp(name, "OR") == 0 || strcmp(name, "or") == 0) {
            for (int i = 0; i < 16; i++) {
                mapping[i] = (i > 0) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "AND") == 0 || strcmp(name, "and") == 0) {
            for (int i = 0; i < 16; i++) {
                mapping[i] = (i == 15) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "XOR") == 0 || strcmp(name, "xor") == 0) {
            for (int i = 0; i < 16; i++) {
                int popcount = __builtin_popcount(i);
                mapping[i] = (popcount % 2 == 1) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "MAJORITY") == 0 || strcmp(name, "majority") == 0) {
            for (int i = 0; i < 16; i++) {
                int popcount = __builtin_popcount(i);
                mapping[i] = (popcount >= 2) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "NOR") == 0 || strcmp(name, "nor") == 0) {
            for (int i = 0; i < 16; i++) {
                mapping[i] = (i == 0) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "NAND") == 0 || strcmp(name, "nand") == 0) {
            for (int i = 0; i < 16; i++) {
                mapping[i] = (i != 15) ? 0x0F : 0x00;
            }
        }
        else if (strcmp(name, "ROTATE") == 0 || strcmp(name, "rotate") == 0) {
            for (int i = 0; i < 16; i++) {
                uint8_t rotated = ((i << 1) | (i >> 3)) & 0x0F;
                mapping[i] = rotated;
            }
        }
        else if (strcmp(name, "INVERT") == 0 || strcmp(name, "invert") == 0) {
            for (int i = 0; i < 16; i++) {
                mapping[i] = (~i) & 0x0F;
            }
        }
    }
};

} // namespace WiggleRoom
