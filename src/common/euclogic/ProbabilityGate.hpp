#pragma once
/******************************************************************************
 * PROBABILITY GATE
 * Probability-based gate with seeded RNG for deterministic testing
 * No VCV Rack dependencies - fully testable standalone
 ******************************************************************************/

#include <random>
#include <cstdint>
#include <algorithm>

namespace WiggleRoom {

struct ProbabilityGate {
    float probability = 1.0f;

    std::mt19937 rng;
    std::uniform_real_distribution<float> dist{0.0f, 1.0f};
    uint32_t seed = 0;

    ProbabilityGate() {
        std::random_device rd;
        seed = rd();
        rng.seed(seed);
    }

    void setSeed(uint32_t s) {
        seed = s;
        rng.seed(seed);
    }

    void reset() {
        rng.seed(seed);
    }

    bool test() {
        if (probability <= 0.0f) return false;
        if (probability >= 1.0f) return true;
        return dist(rng) < probability;
    }

    bool process(bool input) {
        if (!input) return false;
        return test();
    }

    bool process(bool input, float prob) {
        if (!input) return false;
        if (prob <= 0.0f) return false;
        if (prob >= 1.0f) return true;
        return dist(rng) < prob;
    }

    void setProbability(float p) {
        probability = std::max(0.0f, std::min(1.0f, p));
    }
};

} // namespace WiggleRoom
