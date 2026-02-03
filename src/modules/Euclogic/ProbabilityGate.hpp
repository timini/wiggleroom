#pragma once
/******************************************************************************
 * PROBABILITY GATE
 * Probability-based gate with seeded RNG for deterministic testing
 * No VCV Rack dependencies - fully testable standalone
 ******************************************************************************/

#include <random>
#include <cstdint>

namespace WiggleRoom {

struct ProbabilityGate {
    // Probability threshold (0.0 = always block, 1.0 = always pass)
    float probability = 1.0f;

    // RNG state
    std::mt19937 rng;
    std::uniform_real_distribution<float> dist{0.0f, 1.0f};
    uint32_t seed = 0;

    ProbabilityGate() {
        // Seed with random device by default
        std::random_device rd;
        seed = rd();
        rng.seed(seed);
    }

    // Set RNG seed for deterministic testing
    void setSeed(uint32_t s) {
        seed = s;
        rng.seed(seed);
    }

    // Reset RNG to initial seed state
    void reset() {
        rng.seed(seed);
    }

    // Test if gate passes given current probability
    // Consumes RNG state
    bool test() {
        if (probability <= 0.0f) return false;
        if (probability >= 1.0f) return true;
        return dist(rng) < probability;
    }

    // Process: if input is true, apply probability; if input is false, pass through false
    bool process(bool input) {
        if (!input) return false;
        return test();
    }

    // Process with explicit probability value (useful for CV modulation)
    bool process(bool input, float prob) {
        if (!input) return false;
        if (prob <= 0.0f) return false;
        if (prob >= 1.0f) return true;
        return dist(rng) < prob;
    }

    // Set probability (clamped to 0-1)
    void setProbability(float p) {
        probability = std::max(0.0f, std::min(1.0f, p));
    }
};

} // namespace WiggleRoom
