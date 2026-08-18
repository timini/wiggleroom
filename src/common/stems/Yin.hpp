#pragma once
/******************************************************************************
 * Yin - fundamental frequency estimation for Stems
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable.
 *
 * de Cheveigne and Kawahara, "YIN, a fundamental frequency estimator for speech
 * and music", JASA 111(4), 2002. Four of the paper's six steps are implemented
 * here: the squared difference function, the cumulative mean normalised
 * difference function, the absolute threshold, and parabolic interpolation.
 *
 * aubio implements the same algorithm but is GPL and far more library than this
 * needs; YIN itself is short enough to write directly.
 *
 * WHY CONFIDENCE MATTERS. The estimator always returns something. On a drum
 * loop that something is noise, and feeding it to scale detection produces
 * random keys. The spec requires the module to hold the last confident result
 * rather than emit garbage, so this returns aperiodicity alongside the
 * frequency and every caller is expected to gate on it.
 *
 * WHY THE FIRST MINIMUM BELOW THRESHOLD, not the global one. The global minimum
 * of the CMNDF often sits at an integer multiple of the true period, because a
 * signal that repeats every T also repeats every 2T. Taking the global minimum
 * therefore reports the octave below. Taking the FIRST dip under the threshold
 * picks the shortest period that explains the signal, which is the fundamental.
 * This is step 4 of the paper and it is the whole defence against octave errors.
 *
 * Cost is O(window * maxTau), which for the default 2048 sample window and a
 * 55 Hz floor is roughly two million operations per call. That is why this runs
 * on the worker thread. It allocates only in the constructor.
 ******************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace WiggleRoom {
namespace stems {

class Yin {
public:
    struct Result {
        /** Estimated fundamental in Hz, or 0 when nothing was found. */
        float frequency = 0.f;
        /**
         * 0 to 1. Roughly "how periodic is this", computed as 1 minus the
         * CMNDF at the chosen lag. A pure tone lands near 1, white noise near 0.
         */
        float confidence = 0.f;
        /** True when a lag actually passed the absolute threshold. */
        bool voiced = false;
    };

    /**
     * @param maxWindow  Largest analysis window, in samples. Everything is
     *                   allocated once from this, so analyse() never does.
     */
    explicit Yin(std::size_t maxWindow = 2048) {
        maxWindow_ = std::max<std::size_t>(64, maxWindow);
        // The difference function needs two full periods in the window, so the
        // longest measurable lag is half the window.
        difference_.resize(maxWindow_ / 2 + 1, 0.f);
        cmndf_.resize(maxWindow_ / 2 + 1, 0.f);
    }

    void setSampleRate(int sampleRate) {
        sampleRate_ = (sampleRate > 0) ? sampleRate : 48000;
    }

    /**
     * Aperiodicity below which a lag is accepted. The paper uses 0.1; values up
     * to about 0.15 trade a few more false positives for fewer missed notes.
     */
    void setThreshold(float threshold) {
        threshold_ = std::min(std::max(threshold, 0.01f), 0.9f);
    }

    /** Search range. Narrowing it is the cheapest way to cut the cost. */
    void setFrequencyRange(float lowHz, float highHz) {
        lowHz_ = std::max(1.f, lowHz);
        highHz_ = std::max(lowHz_ + 1.f, highHz);
    }

    float threshold() const { return threshold_; }

    /**
     * Estimate the fundamental of one window.
     *
     * @param samples  Window start. At least @p length samples must be readable.
     * @param length   Window length. Clamped to the constructed maximum.
     */
    Result analyse(const float* samples, std::size_t length) {
        Result result;
        if (!samples || length < 8) return result;
        const std::size_t window = std::min(length, maxWindow_);

        const std::size_t halfWindow = window / 2;
        std::size_t minTau = static_cast<std::size_t>(std::floor(sampleRate_ / highHz_));
        std::size_t maxTau = static_cast<std::size_t>(std::ceil(sampleRate_ / lowHz_));
        minTau = std::max<std::size_t>(2, minTau);
        maxTau = std::min(maxTau, halfWindow);
        if (maxTau <= minTau + 1) return result;

        // Silence, or something close enough to it that the difference function
        // is all rounding noise. Without this the search still returns a lag,
        // and an empty buffer reports a confident-looking frequency out of
        // nothing. That is the state the module is in every time a patch loads.
        double energy = 0.0;
        for (std::size_t i = 0; i < window; i++) {
            const double x = samples[i];
            if (!std::isfinite(x)) return result;  // NaN would poison every lag
            energy += x * x;
        }
        if (energy < 1e-12 * static_cast<double>(window)) return result;

        computeDifference(samples, window, maxTau);
        computeCmndf(maxTau);

        const std::size_t tau = pickLag(minTau, maxTau, result.voiced);
        if (tau == 0) return result;

        double refined = interpolate(tau, maxTau);
        if (refined <= 0.0) return result;

        // Parabolic interpolation over integer lags runs out of resolution at
        // the top of the range. At 2 kHz and 48 kHz the period is 24 samples, so
        // one cent is 0.014 of a sample, and fitting three integer-spaced points
        // leaves about 1.5 cents of error. Refining directly on the difference
        // function at fractional lags removes it.
        refined = refineLag(samples, window - maxTau, refined,
                            static_cast<double>(minTau), static_cast<double>(maxTau));
        if (refined <= 0.0) return result;

        result.frequency = static_cast<float>(sampleRate_ / refined);
        // Clamp rather than trust: parabolic interpolation on a noisy CMNDF can
        // push the value slightly outside [0, 1].
        result.confidence = std::min(1.f, std::max(0.f, 1.f - cmndf_[tau]));
        return result;
    }

    /**
     * Minimise the difference function over fractional lags.
     *
     * Ternary search rather than another parabolic fit: the function is smooth
     * and unimodal in the immediate neighbourhood of a true period, and a search
     * converges to the actual minimum instead of to wherever a parabola through
     * three points happens to sit. Forty iterations narrow a two-sample bracket
     * to well under a thousandth of a sample, and the whole refinement costs a
     * fraction of the main pass.
     */
    double refineLag(const float* s, std::size_t compare, double tau, double minTau,
                     double maxTau) const {
        double lo = std::max(minTau, tau - 1.0);
        double hi = std::min(maxTau, tau + 1.0);
        if (!(hi > lo)) return tau;

        for (int iteration = 0; iteration < 40 && (hi - lo) > 1e-4; iteration++) {
            const double third = (hi - lo) / 3.0;
            const double a = lo + third;
            const double b = hi - third;
            if (differenceAt(s, compare, a) < differenceAt(s, compare, b)) {
                hi = b;
            } else {
                lo = a;
            }
        }
        return 0.5 * (lo + hi);
    }

    /**
     * Squared difference at a fractional lag.
     *
     * Catmull-Rom interpolation of the delayed copy. Linear interpolation was
     * measured against it and is equivalent on everything tested: pure sines
     * across the range, squares, saws, a weak-fundamental harmonic stack, and
     * saw plus noise all agree to within a small fraction of the error either
     * one leaves. The cubic is kept because its error does not depend on the
     * frequency content the way linear interpolation's attenuation does, not
     * because a difference was observed here.
     */
    double differenceAt(const float* s, std::size_t compare, double tau) const {
        const std::size_t base = static_cast<std::size_t>(tau);
        const double frac = tau - static_cast<double>(base);
        double acc = 0.0;
        for (std::size_t i = 1; i + 2 < compare; i++) {
            const std::size_t j = i + base;
            const double p0 = s[j - 1], p1 = s[j], p2 = s[j + 1], p3 = s[j + 2];
            const double delayed =
                p1 + 0.5 * frac *
                         (p2 - p0 +
                          frac * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 +
                                  frac * (3.0 * (p1 - p2) + p3 - p0)));
            const double delta = static_cast<double>(s[i]) - delayed;
            acc += delta * delta;
        }
        return acc;
    }

    /** The CMNDF from the last analyse(), for tests and diagnostics. */
    const std::vector<float>& debugCmndf() const { return cmndf_; }

private:
    /** Step 1 and 2: squared difference over the window for each lag. */
    void computeDifference(const float* s, std::size_t window, std::size_t maxTau) {
        // Only the first half of the window is compared, so that s[i + tau] is
        // always inside the window for every lag up to maxTau. Reading past it
        // would compare against whatever follows the buffer.
        const std::size_t compare = window - maxTau;
        difference_[0] = 0.f;
        for (std::size_t tau = 1; tau <= maxTau; tau++) {
            double acc = 0.0;
            for (std::size_t i = 0; i < compare; i++) {
                const double delta = static_cast<double>(s[i]) - static_cast<double>(s[i + tau]);
                acc += delta * delta;
            }
            difference_[tau] = static_cast<float>(acc);
        }
    }

    /** Step 3: cumulative mean normalisation. */
    void computeCmndf(std::size_t maxTau) {
        // d'(0) = 1 by definition in the paper. Nothing here reads it, because
        // the search starts at minTau, which is at least 2; it is set so the
        // array matches the published definition rather than as a guard.
        cmndf_[0] = 1.f;
        double runningSum = 0.0;
        for (std::size_t tau = 1; tau <= maxTau; tau++) {
            runningSum += difference_[tau];
            cmndf_[tau] = (runningSum > 1e-20)
                              ? static_cast<float>(difference_[tau] * tau / runningSum)
                              : 1.f;
        }
    }

    /** Step 4: first local minimum below the absolute threshold. */
    std::size_t pickLag(std::size_t minTau, std::size_t maxTau, bool& voiced) const {
        voiced = false;
        for (std::size_t tau = minTau; tau <= maxTau; tau++) {
            if (cmndf_[tau] >= threshold_) continue;
            // Descend to the bottom of this dip. Stopping at the first sample
            // under the threshold would bias the estimate towards a shorter
            // period, since the threshold is crossed on the way down.
            std::size_t best = tau;
            while (best + 1 <= maxTau && cmndf_[best + 1] < cmndf_[best]) best++;
            voiced = true;
            return best;
        }

        // Nothing passed. Fall back to the global minimum so a caller that
        // ignores `voiced` still gets the best available guess, and let the
        // confidence say how little it is worth.
        std::size_t best = minTau;
        for (std::size_t tau = minTau; tau <= maxTau; tau++) {
            if (cmndf_[tau] < cmndf_[best]) best = tau;
        }
        return best;
    }

    /** Step 5: parabolic interpolation for sub-sample period resolution. */
    double interpolate(std::size_t tau, std::size_t maxTau) const {
        if (tau == 0 || tau >= maxTau) return static_cast<double>(tau);
        const double a = cmndf_[tau - 1];
        const double b = cmndf_[tau];
        const double c = cmndf_[tau + 1];
        const double denom = 2.0 * (2.0 * b - a - c);
        if (std::fabs(denom) < 1e-12) return static_cast<double>(tau);
        const double shift = (c - a) / denom;
        // A well-formed minimum shifts by less than half a sample. Anything
        // larger means the three points are not bracketing a minimum, and
        // trusting it would move the estimate to a lag that was never measured.
        if (!(shift > -0.5 && shift < 0.5)) return static_cast<double>(tau);
        return static_cast<double>(tau) + shift;
    }

    std::size_t maxWindow_ = 2048;
    int sampleRate_ = 48000;
    float threshold_ = 0.12f;
    float lowHz_ = 50.f;
    float highHz_ = 2200.f;

    std::vector<float> difference_;
    std::vector<float> cmndf_;
};

}  // namespace stems
}  // namespace WiggleRoom
