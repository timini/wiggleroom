#pragma once

#include <cmath>
#include <algorithm>
#include <array>

namespace WiggleRoom {

/**
 * ExpressionEngine - Generates slide and accent signals based on melodic context
 *
 * VISCOSITY (Slide Logic):
 *   - Bipolar control (-1 to +1)
 *   - CCW (Liquid, negative): Slide when intervals are SMALL (< 2 semitones)
 *   - CW (Elastic, positive): Slide when intervals are LARGE (> 7 semitones)
 *   - Center (0): Slides disabled
 *
 * FORCE (Accent Logic):
 *   - Three modes:
 *     - GRAVITY: Accent when pitch drops (downbeat weight)
 *     - APEX: Accent the highest note in 4-step window (emphasize peaks)
 *     - INFLECTION: Accent when pitch direction reverses (corners)
 */
class ExpressionEngine {
public:
    enum class ForceMode {
        GRAVITY,     // Accent on pitch drops
        APEX,        // Accent on highest in window
        INFLECTION   // Accent on direction changes
    };

    static constexpr int PITCH_HISTORY_SIZE = 4;

    ExpressionEngine()
        : viscosity_(0.0f)
        , forceMode_(ForceMode::GRAVITY)
        , forceDepth_(0.5f)
        , slide_(false)
        , accent_(false)
        , historyIndex_(0)
    {
        pitchHistory_.fill(12);  // Initialize to middle C
    }

    // Update with new pitch value - call once per clock
    void update(int currentPitch, int previousPitch) {
        // Store in history
        pitchHistory_[historyIndex_] = currentPitch;
        historyIndex_ = (historyIndex_ + 1) % PITCH_HISTORY_SIZE;

        // Calculate interval
        int interval = std::abs(currentPitch - previousPitch);

        // Calculate slide based on viscosity
        slide_ = calculateSlide(interval);

        // Calculate accent based on force mode
        accent_ = calculateAccent(currentPitch, previousPitch);
    }

    // Set viscosity (-1.0 to +1.0)
    // Negative = Liquid (slide on small intervals)
    // Positive = Elastic (slide on large intervals)
    void setViscosity(float v) {
        viscosity_ = std::min(std::max(v, -1.0f), 1.0f);
    }

    // Set force mode
    void setForceMode(ForceMode mode) {
        forceMode_ = mode;
    }

    void setForceModeIndex(int index) {
        switch (index) {
            case 0: forceMode_ = ForceMode::GRAVITY; break;
            case 1: forceMode_ = ForceMode::APEX; break;
            case 2: forceMode_ = ForceMode::INFLECTION; break;
            default: forceMode_ = ForceMode::GRAVITY; break;
        }
    }

    // Set force depth (0.0 to 1.0)
    void setForceDepth(float d) {
        forceDepth_ = std::min(std::max(d, 0.0f), 1.0f);
    }

    // Get current slide state
    bool getSlide() const {
        return slide_;
    }

    // Get current accent state
    bool getAccent() const {
        return accent_;
    }

    // Get force depth for accent intensity
    float getForceDepth() const {
        return forceDepth_;
    }

    // Reset history
    void reset() {
        pitchHistory_.fill(12);
        historyIndex_ = 0;
        slide_ = false;
        accent_ = false;
    }

private:
    float viscosity_;
    ForceMode forceMode_;
    float forceDepth_;

    bool slide_;
    bool accent_;

    std::array<int, PITCH_HISTORY_SIZE> pitchHistory_;
    int historyIndex_;

    bool calculateSlide(int interval) {
        // Dead zone around center (no slides when viscosity near zero)
        if (std::abs(viscosity_) < 0.1f) {
            return false;
        }

        if (viscosity_ < 0) {
            // Liquid mode: slide when intervals are SMALL
            // More negative = higher threshold for "small"
            int threshold = static_cast<int>(2 + std::abs(viscosity_) * 3);  // 2-5 semitones
            return interval > 0 && interval <= threshold;
        } else {
            // Elastic mode: slide when intervals are LARGE
            // More positive = lower threshold for "large"
            int threshold = static_cast<int>(7 - viscosity_ * 4);  // 7-3 semitones
            return interval >= threshold;
        }
    }

    bool calculateAccent(int currentPitch, int previousPitch) {
        // Apply depth as probability/threshold
        // forceDepth_ = 0: very selective accents
        // forceDepth_ = 1: many accents

        switch (forceMode_) {
            case ForceMode::GRAVITY:
                return calculateGravityAccent(currentPitch, previousPitch);

            case ForceMode::APEX:
                return calculateApexAccent(currentPitch);

            case ForceMode::INFLECTION:
                return calculateInflectionAccent(currentPitch, previousPitch);

            default:
                return false;
        }
    }

    bool calculateGravityAccent(int currentPitch, int previousPitch) {
        // Accent when pitch drops
        int drop = previousPitch - currentPitch;

        if (drop <= 0) return false;  // No drop, no accent

        // Larger drops more likely to accent
        // forceDepth modulates minimum drop required
        int minDrop = static_cast<int>((1.0f - forceDepth_) * 7);  // 0-7 semitones
        return drop >= minDrop;
    }

    bool calculateApexAccent(int currentPitch) {
        // Accent the highest note in the history window
        int maxPitch = currentPitch;
        for (int i = 0; i < PITCH_HISTORY_SIZE; i++) {
            if (pitchHistory_[i] > maxPitch) {
                maxPitch = pitchHistory_[i];
            }
        }

        // Is current pitch the apex?
        bool isApex = (currentPitch >= maxPitch);

        // forceDepth modulates how much higher than recent average
        if (isApex && forceDepth_ < 1.0f) {
            int sum = 0;
            for (int i = 0; i < PITCH_HISTORY_SIZE; i++) {
                sum += pitchHistory_[i];
            }
            float avg = sum / static_cast<float>(PITCH_HISTORY_SIZE);
            float threshold = (1.0f - forceDepth_) * 5;  // 0-5 semitones above average
            isApex = (currentPitch >= avg + threshold);
        }

        return isApex;
    }

    bool calculateInflectionAccent(int currentPitch, int previousPitch) {
        // Get the pitch from two steps ago
        int twoAgo = pitchHistory_[(historyIndex_ + PITCH_HISTORY_SIZE - 2) % PITCH_HISTORY_SIZE];

        // Calculate directions
        int prevDirection = previousPitch - twoAgo;  // Was going up (+) or down (-)
        int currDirection = currentPitch - previousPitch;  // Now going up (+) or down (-)

        // Inflection = direction reversal
        bool inflection = (prevDirection > 0 && currDirection < 0) ||
                          (prevDirection < 0 && currDirection > 0);

        if (!inflection) return false;

        // forceDepth modulates minimum angle of change
        int totalChange = std::abs(prevDirection) + std::abs(currDirection);
        int minChange = static_cast<int>((1.0f - forceDepth_) * 6);  // 0-6 total semitones
        return totalChange >= minChange;
    }
};

} // namespace WiggleRoom
