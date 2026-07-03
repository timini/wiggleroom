#pragma once
/******************************************************************************
 * EUCLIDEAN ENGINE
 * Pure Euclidean rhythm generator using Bjorklund's algorithm
 * No VCV Rack dependencies - fully testable standalone
 ******************************************************************************/

#include <vector>
#include <cstdint>
#include <algorithm>

namespace WiggleRoom {

struct EuclideanEngine {
    // Configuration
    int steps = 16;      // Total steps in pattern (1-64)
    int hits = 8;        // Number of hits to distribute (0-steps)
    int rotation = 0;    // Pattern rotation offset (0 to steps-1)

    // State
    int currentStep = 0; // Current position in sequence (0 to steps-1)

    // Pre-computed pattern
    std::vector<bool> pattern;

    // Generate Euclidean rhythm pattern using Bjorklund's algorithm
    void generate() {
        pattern.clear();
        pattern.resize(steps, false);

        if (steps <= 0) return;
        if (hits <= 0) return;
        if (hits >= steps) {
            pattern.assign(steps, true);
            return;
        }

        std::vector<std::vector<bool>> first, second;

        for (int i = 0; i < hits; i++) {
            first.push_back({true});
        }
        for (int i = 0; i < steps - hits; i++) {
            second.push_back({false});
        }

        while (second.size() > 1) {
            size_t minSize = std::min(first.size(), second.size());

            for (size_t i = 0; i < minSize; i++) {
                first[i].insert(first[i].end(), second[i].begin(), second[i].end());
            }

            std::vector<std::vector<bool>> newSecond;
            if (second.size() > first.size()) {
                for (size_t i = first.size(); i < second.size(); i++) {
                    newSecond.push_back(std::move(second[i]));
                }
            } else if (first.size() > second.size()) {
                for (size_t i = second.size(); i < first.size(); i++) {
                    newSecond.push_back(std::move(first[i]));
                }
                first.resize(second.size());
            }
            second = std::move(newSecond);
        }

        std::vector<bool> result;
        result.reserve(steps);
        for (const auto& seq : first) {
            result.insert(result.end(), seq.begin(), seq.end());
        }
        for (const auto& seq : second) {
            result.insert(result.end(), seq.begin(), seq.end());
        }

        pattern.resize(steps);
        for (int i = 0; i < steps; i++) {
            int srcIdx = (i + rotation) % steps;
            pattern[i] = result[srcIdx];
        }
    }

    void configure(int numSteps, int numHits, int rot = 0) {
        int newSteps = std::max(1, std::min(64, numSteps));
        int newHits = std::max(0, std::min(newSteps, numHits));
        int newRotation = std::max(0, std::min(newSteps - 1, rot));

        if (newSteps != steps || newHits != hits || newRotation != rotation) {
            bool stepsChanged = (newSteps != steps);
            steps = newSteps;
            hits = newHits;
            rotation = newRotation;
            generate();

            if (stepsChanged) {
                currentStep = 0;
            } else if (currentStep >= steps) {
                currentStep = 0;
            }
        }
    }

    bool tick() {
        if (steps <= 0 || pattern.empty()) return false;

        bool isHit = pattern[currentStep];
        currentStep = (currentStep + 1) % steps;
        return isHit;
    }

    void reset() {
        currentStep = 0;
    }

    bool getHit(int step) const {
        if (step < 0 || step >= (int)pattern.size()) return false;
        return pattern[step];
    }

    int getCurrentStep() const {
        return currentStep;
    }
};

} // namespace WiggleRoom
