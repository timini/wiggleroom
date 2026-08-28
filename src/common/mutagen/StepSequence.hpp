#pragma once
/******************************************************************************
 * STEP SEQUENCE
 * Values for one Mutagen lane, plus the mutation engine that drifts them.
 *
 * No VCV Rack dependencies - fully testable standalone.
 *
 * Values are normalised to [0, 1]. Scaling to a voltage happens at output
 * time in the module, so stored values stay range-independent (same choice
 * EucSeq makes for its per-step CV).
 ******************************************************************************/

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace WiggleRoom {

struct StepSequence {
    static constexpr int MAX_STEPS = 64;
    static constexpr int MIN_STEPS = 1;
    static constexpr int DEFAULT_STEPS = 8;

    float values[MAX_STEPS] = {};
    int steps = DEFAULT_STEPS;

    StepSequence() {
        std::random_device rd;
        rng.seed(rd());
    }

    // Tests seed explicitly so mutation statistics are reproducible.
    void setSeed(uint32_t seed) { rng.seed(seed); }

    void setSteps(int n) { steps = clampInt(n, MIN_STEPS, MAX_STEPS); }

    float valueAt(int index) const {
        if (steps <= 0) return 0.f;
        // Wrap rather than clamp so a shrunken length still reads in range.
        int i = index % steps;
        if (i < 0) i += steps;
        return values[i];
    }

    void setValue(int index, float v) {
        if (index < 0 || index >= MAX_STEPS) return;
        pushUndo();
        values[index] = clampFloat(v, 0.f, 1.f);
    }

    // Fill every active step. Steps beyond the active length keep whatever
    // they held, so shortening and lengthening again does not wipe them.
    void randomize() {
        pushUndo();
        std::uniform_real_distribution<float> dist(0.f, 1.f);
        for (int s = 0; s < steps; s++)
            values[s] = dist(rng);
    }

    // The whole mutation feature: on a given clock step, with probability
    // `rate`, one uniformly chosen active step takes a fresh random value.
    // Returns the step it changed, or -1 if nothing mutated.
    int mutateOnce(float rate) {
        if (rate <= 0.f || steps <= 0) return -1;
        std::uniform_real_distribution<float> chance(0.f, 1.f);
        if (chance(rng) >= rate) return -1;

        std::uniform_int_distribution<int> pick(0, steps - 1);
        std::uniform_real_distribution<float> dist(0.f, 1.f);
        int target = pick(rng);
        // Deliberately no pushUndo() here: mutation fires on clock steps and
        // would otherwise grow history without bound and bury the user's own
        // edits. Undo covers manual edits, randomise and bank recall.
        values[target] = dist(rng);
        return target;
    }

    // ---- undo/redo, following TruthTable's push-based history ----

    void pushUndo() {
        undoHistory.push_back(snapshot());
        redoHistory.clear();
        if ((int)undoHistory.size() > MAX_HISTORY)
            undoHistory.erase(undoHistory.begin());
    }

    bool undo() {
        if (undoHistory.empty()) return false;
        redoHistory.push_back(snapshot());
        restore(undoHistory.back());
        undoHistory.pop_back();
        return true;
    }

    bool redo() {
        if (redoHistory.empty()) return false;
        undoHistory.push_back(snapshot());
        restore(redoHistory.back());
        redoHistory.pop_back();
        return true;
    }

    void clearHistory() {
        undoHistory.clear();
        redoHistory.clear();
    }

private:
    static constexpr int MAX_HISTORY = 64;

    struct Snapshot {
        float values[MAX_STEPS];
        int steps;
    };

    std::mt19937 rng;
    std::vector<Snapshot> undoHistory;
    std::vector<Snapshot> redoHistory;

    Snapshot snapshot() const {
        Snapshot s;
        std::copy(values, values + MAX_STEPS, s.values);
        s.steps = steps;
        return s;
    }

    void restore(const Snapshot& s) {
        std::copy(s.values, s.values + MAX_STEPS, values);
        steps = s.steps;
    }

    static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float clampFloat(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
};

} // namespace WiggleRoom
