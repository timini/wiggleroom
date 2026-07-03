#pragma once
/******************************************************************************
 * TRUTH TABLE
 * N-input boolean logic with 2^N-state truth table
 * Templated on channel count. No VCV Rack dependencies - fully testable standalone
 *
 * Enhanced with:
 *   - Per-cell lock mask (excluded from randomize/mutate)
 *   - Row/column randomize and mutate (respecting locks)
 *   - Per-column density control
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

    // Per-cell lock mask: if bit is set, that output bit is locked for that input state
    std::array<uint8_t, N_STATES> lockMask{};

    // Unlimited undo/redo history
    struct HistoryEntry {
        std::array<uint8_t, N_STATES> mapping;
        std::array<uint8_t, N_STATES> lockMask;
    };
    std::vector<HistoryEntry> undoHistory;
    std::vector<HistoryEntry> redoHistory;

    // RNG for randomization
    std::mt19937 rng;

    TruthTableT() {
        // Default: pass-through (output mirrors input)
        for (int i = 0; i < N_STATES; i++) {
            mapping[i] = static_cast<uint8_t>(i) & OUTPUT_MASK;
            lockMask[i] = 0;
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

    // Overload for 4-channel convenience
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

    // ---------------------------------------------------------------
    // Lock management
    // ---------------------------------------------------------------
    void toggleLock(int inputState, int outputBit) {
        if (inputState >= 0 && inputState < N_STATES && outputBit >= 0 && outputBit < N_CHANNELS) {
            lockMask[inputState] ^= (1 << outputBit);
        }
    }

    bool isLocked(int inputState, int outputBit) const {
        if (inputState >= 0 && inputState < N_STATES && outputBit >= 0 && outputBit < N_CHANNELS) {
            return (lockMask[inputState] >> outputBit) & 1;
        }
        return false;
    }

    // ---------------------------------------------------------------
    // Undo/Redo (now includes lock state)
    // ---------------------------------------------------------------
    void pushUndo() {
        undoHistory.push_back({mapping, lockMask});
        redoHistory.clear();
    }

    bool undo() {
        if (undoHistory.empty()) return false;
        redoHistory.push_back({mapping, lockMask});
        auto& entry = undoHistory.back();
        mapping = entry.mapping;
        lockMask = entry.lockMask;
        undoHistory.pop_back();
        return true;
    }

    bool redo() {
        if (redoHistory.empty()) return false;
        undoHistory.push_back({mapping, lockMask});
        auto& entry = redoHistory.back();
        mapping = entry.mapping;
        lockMask = entry.lockMask;
        redoHistory.pop_back();
        return true;
    }

    void clearHistory() {
        undoHistory.clear();
        redoHistory.clear();
    }

    // ---------------------------------------------------------------
    // Randomize / Mutate (respect locks)
    // ---------------------------------------------------------------
    void randomize() {
        pushUndo();
        std::uniform_int_distribution<int> dist(0, 1);
        for (int i = 0; i < N_STATES; i++) {
            uint8_t newVal = 0;
            for (int bit = 0; bit < N_CHANNELS; bit++) {
                if (isLocked(i, bit)) {
                    // Preserve locked bit
                    newVal |= (mapping[i] & (1 << bit));
                } else {
                    if (dist(rng)) newVal |= (1 << bit);
                }
            }
            mapping[i] = newVal;
        }
    }

    void mutate() {
        pushUndo();
        // Find unlocked cells
        struct Cell { int state; int bit; };
        std::vector<Cell> unlocked;
        for (int i = 0; i < N_STATES; i++) {
            for (int bit = 0; bit < N_CHANNELS; bit++) {
                if (!isLocked(i, bit)) {
                    unlocked.push_back({i, bit});
                }
            }
        }
        if (unlocked.empty()) return;

        std::uniform_int_distribution<int> countDist(1, std::min(3, (int)unlocked.size()));
        std::uniform_int_distribution<int> cellDist(0, (int)unlocked.size() - 1);

        int numFlips = countDist(rng);
        for (int i = 0; i < numFlips; i++) {
            auto& cell = unlocked[cellDist(rng)];
            mapping[cell.state] ^= (1 << cell.bit);
        }
    }

    // ---------------------------------------------------------------
    // Row/Column operations (respect locks)
    // ---------------------------------------------------------------
    void randomizeRow(int row) {
        if (row < 0 || row >= N_STATES) return;
        pushUndo();
        std::uniform_int_distribution<int> dist(0, 1);
        for (int bit = 0; bit < N_CHANNELS; bit++) {
            if (!isLocked(row, bit)) {
                if (dist(rng))
                    mapping[row] |= (1 << bit);
                else
                    mapping[row] &= ~(1 << bit);
            }
        }
    }

    void randomizeColumn(int col) {
        if (col < 0 || col >= N_CHANNELS) return;
        pushUndo();
        std::uniform_int_distribution<int> dist(0, 1);
        for (int i = 0; i < N_STATES; i++) {
            if (!isLocked(i, col)) {
                if (dist(rng))
                    mapping[i] |= (1 << col);
                else
                    mapping[i] &= ~(1 << col);
            }
        }
    }

    void mutateRow(int row) {
        if (row < 0 || row >= N_STATES) return;
        pushUndo();
        std::vector<int> unlockedBits;
        for (int bit = 0; bit < N_CHANNELS; bit++) {
            if (!isLocked(row, bit)) unlockedBits.push_back(bit);
        }
        if (unlockedBits.empty()) return;
        std::uniform_int_distribution<int> dist(0, (int)unlockedBits.size() - 1);
        int bit = unlockedBits[dist(rng)];
        mapping[row] ^= (1 << bit);
    }

    void mutateColumn(int col) {
        if (col < 0 || col >= N_CHANNELS) return;
        pushUndo();
        std::vector<int> unlockedRows;
        for (int i = 0; i < N_STATES; i++) {
            if (!isLocked(i, col)) unlockedRows.push_back(i);
        }
        if (unlockedRows.empty()) return;
        std::uniform_int_distribution<int> dist(0, (int)unlockedRows.size() - 1);
        int row = unlockedRows[dist(rng)];
        mapping[row] ^= (1 << col);
    }

    // Set column density: probability of 1s in unlocked cells
    void setColumnDensity(int col, float density) {
        if (col < 0 || col >= N_CHANNELS) return;
        pushUndo();
        std::uniform_real_distribution<float> dist(0.f, 1.f);
        for (int i = 0; i < N_STATES; i++) {
            if (!isLocked(i, col)) {
                if (dist(rng) < density)
                    mapping[i] |= (1 << col);
                else
                    mapping[i] &= ~(1 << col);
            }
        }
    }

    void setRowDensity(int row, float density) {
        if (row < 0 || row >= N_STATES) return;
        pushUndo();
        std::uniform_real_distribution<float> dist(0.f, 1.f);
        for (int bit = 0; bit < N_CHANNELS; bit++) {
            if (!isLocked(row, bit)) {
                if (dist(rng) < density)
                    mapping[row] |= (1 << bit);
                else
                    mapping[row] &= ~(1 << bit);
            }
        }
    }

    // ---------------------------------------------------------------
    // Serialization
    // ---------------------------------------------------------------
    std::array<uint8_t, N_STATES> serialize() const {
        return mapping;
    }

    void deserialize(const std::array<uint8_t, N_STATES>& data) {
        mapping = data;
    }

    std::array<uint8_t, N_STATES> serializeLocks() const {
        return lockMask;
    }

    void deserializeLocks(const std::array<uint8_t, N_STATES>& data) {
        lockMask = data;
    }
};

// TruthTable: 4-channel version with preset support (backward compatible)
struct TruthTable : TruthTableT<4> {
    using TruthTableT<4>::TruthTableT;

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
