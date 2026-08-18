#pragma once
/******************************************************************************
 * ScaleDetect - key estimation from detected pitches for Stems
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable.
 *
 * Krumhansl and Schmuckler's key-finding algorithm, as published in Krumhansl,
 * "Cognitive Foundations of Musical Pitch" (1990). Pitches go into a twelve bin
 * pitch-class histogram; the histogram is correlated against a major and a minor
 * profile at all twelve rotations; the best of the twenty-four is the key.
 *
 * TWO THINGS THE SOURCE CONCEPT DID NOT ADDRESS, both required by the spec:
 *
 *  1. Scale detection is meaningless on an unpitched stem. Run it on a drum
 *     layer and it returns a key, confidently and at random. Two gates stop
 *     that. Pitches arrive already carrying YIN's confidence and anything below
 *     the voicing threshold is never counted, so a percussive layer contributes
 *     almost nothing; and a minimum accumulated weight is required before any
 *     answer is offered at all, so a handful of stray detections cannot decide
 *     a key.
 *
 *  2. Below threshold the module holds the last confident result rather than
 *     emitting garbage. On a fresh module there is no last result to hold, so
 *     this SEEDS from the manual root and scale defaults, which covers the
 *     empty buffer, an unpitched first recording, and the window while
 *     separation is still running.
 *
 * The histogram decays, so a key change in the source material is eventually
 * followed rather than being outvoted forever by whatever was recorded first.
 *
 * Real-time contract: everything here is fixed-size arithmetic over twelve
 * bins. It allocates nothing at any point.
 ******************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace WiggleRoom {
namespace stems {

class ScaleDetect {
public:
    static constexpr int kPitchClasses = 12;

    enum class Mode { Major = 0, Minor = 1 };

    struct Result {
        /** 0 = C, 1 = C sharp, and so on. */
        int root = 0;
        Mode mode = Mode::Major;
        /** Correlation of the winning key, 0 to 1. */
        float confidence = 0.f;
        /** False when this is the held or seeded value rather than a detection. */
        bool detected = false;
    };

    ScaleDetect() { reset(); }

    /**
     * Clear the histogram and return to the seed.
     *
     * The seed is deliberately NOT cleared: it is the manual root and scale,
     * which is what the module falls back to and which the user may have set.
     */
    void reset() {
        for (int i = 0; i < kPitchClasses; i++) histogram_[i] = 0.0;
        totalWeight_ = 0.0;
        held_ = Result{seedRoot_, seedMode_, 0.f, false};
    }

    /** Manual root and scale, used until the first confident detection. */
    void setSeed(int root, Mode mode) {
        seedRoot_ = wrap(root);
        seedMode_ = mode;
        if (!held_.detected) {
            held_.root = seedRoot_;
            held_.mode = seedMode_;
        }
    }

    /** Confidence a pitch must carry to be counted. Matches YIN's voicing gate. */
    void setPitchConfidenceThreshold(float threshold) {
        if (!std::isfinite(threshold)) return;
        pitchThreshold_ = std::min(std::max(threshold, 0.f), 1.f);
    }

    /** Correlation the winning key must reach before it replaces the held one. */
    void setKeyConfidenceThreshold(float threshold) {
        if (!std::isfinite(threshold)) return;
        keyThreshold_ = std::min(std::max(threshold, 0.f), 1.f);
    }

    /**
     * Accumulated weight required before any key is offered.
     *
     * This is the gate that a handful of stray detections on a drum layer
     * cannot get past. Correlation alone will not do it: a histogram with two
     * bins filled correlates with something, and correlates well.
     */
    void setMinimumWeight(float weight) {
        if (!std::isfinite(weight)) return;
        minimumWeight_ = std::max(0.f, weight);
    }

    /**
     * Per-update multiplier applied to the histogram, 0 to 1.
     *
     * 1 accumulates forever, which means the first thing recorded outvotes
     * everything after it. Anything less lets the estimate follow the material.
     */
    void setDecay(float decay) {
        if (!std::isfinite(decay)) return;
        decay_ = std::min(std::max(decay, 0.f), 1.f);
    }

    /**
     * Offer one pitch estimate.
     *
     * @param hz          Frequency. Ignored if not finite or out of hearing range.
     * @param confidence  YIN's confidence. Below the threshold this is ignored.
     */
    void addPitch(float hz, float confidence) {
        if (!std::isfinite(hz) || !std::isfinite(confidence)) return;
        if (confidence < pitchThreshold_) return;
        if (hz < 20.f || hz > 8000.f) return;

        // Clamp rather than trust. YIN produces confidence in 0 to 1 by
        // construction, but this is a public entry point, and a single caller
        // passing something larger would swamp every other bin in the histogram
        // and pin the key to whatever that one pitch was.
        const double weight = std::min(1.0, static_cast<double>(confidence));

        if (decay_ < 1.f) {
            for (int i = 0; i < kPitchClasses; i++) histogram_[i] *= decay_;
            totalWeight_ *= decay_;
        }

        // A4 = 440 Hz is MIDI 69, and 69 mod 12 is 9, so pitch class 0 lands on
        // C as intended.
        const double midi = 69.0 + 12.0 * std::log2(static_cast<double>(hz) / 440.0);
        const int pitchClass = wrap(static_cast<int>(std::lround(midi)));

        // Weighted by confidence, so a marginal detection counts for less than a
        // clear one rather than equally.
        histogram_[pitchClass] += weight;
        totalWeight_ += weight;
    }

    /**
     * Best key for the material so far.
     *
     * Returns the held result unchanged when there is too little evidence or the
     * best correlation is too weak, which is the behaviour the spec requires on
     * a percussive stem.
     */
    Result detect() {
        if (totalWeight_ < minimumWeight_) return held_;

        int bestRoot = 0;
        Mode bestMode = Mode::Major;
        double best = -2.0;

        for (int mode = 0; mode < 2; mode++) {
            const double* profile = (mode == 0) ? kMajorProfile : kMinorProfile;
            for (int root = 0; root < kPitchClasses; root++) {
                const double r = correlate(profile, root);
                if (r > best) {
                    best = r;
                    bestRoot = root;
                    bestMode = (mode == 0) ? Mode::Major : Mode::Minor;
                }
            }
        }

        const float confidence = static_cast<float>(std::min(1.0, std::max(0.0, best)));
        if (confidence < keyThreshold_) {
            // Report how weak the evidence is, but do NOT move the key.
            Result out = held_;
            out.confidence = confidence;
            return out;
        }

        held_ = Result{bestRoot, bestMode, confidence, true};
        return held_;
    }

    /** The last result, without re-running the correlation. */
    const Result& held() const { return held_; }

    /** Histogram bin, for tests and for the UI display. */
    double bin(int pitchClass) const { return histogram_[wrap(pitchClass)]; }

    double totalWeight() const { return totalWeight_; }

    /** Note name for a pitch class, sharps only. */
    static const char* noteName(int pitchClass) {
        static const char* kNames[kPitchClasses] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                    "F#", "G",  "G#", "A",  "A#", "B"};
        return kNames[wrap(pitchClass)];
    }

private:
    /**
     * Pearson correlation between the histogram rotated to @p root and a profile.
     *
     * Pearson rather than a plain dot product: the profiles and the histogram
     * have completely different scales and offsets, and a dot product would
     * simply favour whichever rotation put the most weight under the largest
     * profile values. Subtracting both means is what makes the twenty-four
     * candidates comparable.
     */
    double correlate(const double* profile, int root) const {
        double sumX = 0.0, sumY = 0.0;
        for (int i = 0; i < kPitchClasses; i++) {
            sumX += histogram_[wrap(i + root)];
            sumY += profile[i];
        }
        const double meanX = sumX / kPitchClasses;
        const double meanY = sumY / kPitchClasses;

        double num = 0.0, denX = 0.0, denY = 0.0;
        for (int i = 0; i < kPitchClasses; i++) {
            const double dx = histogram_[wrap(i + root)] - meanX;
            const double dy = profile[i] - meanY;
            num += dx * dy;
            denX += dx * dx;
            denY += dy * dy;
        }
        const double den = std::sqrt(denX * denY);
        // A flat histogram has zero variance and no key. The numerator is zero
        // whenever the denominator is, so dividing would give NaN rather than an
        // infinity, and NaN loses every comparison, so the outcome is the same
        // either way. This is written out so the non-answer is explicit instead
        // of resting on that.
        return (den > 1e-12) ? (num / den) : 0.0;
    }

    static int wrap(int i) {
        const int m = i % kPitchClasses;
        return (m < 0) ? (m + kPitchClasses) : m;
    }

    // Krumhansl and Kessler probe-tone ratings, the profiles the algorithm is
    // defined against. Left as published rather than normalised, since Pearson
    // correlation is invariant to both scale and offset anyway.
    static constexpr double kMajorProfile[kPitchClasses] = {
        6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static constexpr double kMinorProfile[kPitchClasses] = {
        6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

    double histogram_[kPitchClasses] = {0.0};
    double totalWeight_ = 0.0;

    float pitchThreshold_ = 0.5f;
    float keyThreshold_ = 0.5f;
    float minimumWeight_ = 8.f;
    float decay_ = 1.f;

    int seedRoot_ = 0;
    Mode seedMode_ = Mode::Major;
    Result held_;
};

}  // namespace stems
}  // namespace WiggleRoom
