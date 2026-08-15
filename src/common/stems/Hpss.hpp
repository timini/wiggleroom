#pragma once
/******************************************************************************
 * Hpss - Tier 0 separation for Stems
 *
 * Harmonic/percussive separation by median filtering on the magnitude
 * spectrogram (Fitzgerald, DAFx-10; margin extension Driedger, Muller and
 * Disch, 2014), combined with a low band split, producing four layers.
 *
 * Why this rather than a neural model: it is cheap, deterministic, needs no
 * model files and adds no dependency. The instrument needs material that
 * differs from itself, not correctly labelled instruments, so four musically
 * distinct layers are sufficient for the MVP. That removes ML deployment from
 * the critical path entirely; Tier 1 can be added later behind the same
 * interface.
 *
 * Parameters follow librosa's defaults (kernel 31, power 2.0, margin 1.0, soft
 * Wiener-style masks) so results can be cross-checked against a reference
 * implementation.
 *
 * The four layers are DISJOINT. Masks are applied in order, each excluding what
 * earlier layers claimed, so summing all four at unity reconstructs the source.
 * Defining Harmonic without excluding the Low band would make them overlap and
 * the default all-faders-at-unity state would double-count bass.
 *
 * Runs on the worker thread over a whole recorded buffer. Not RT-safe.
 ******************************************************************************/

#include "FftBackend.hpp"
#include "Stft.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace WiggleRoom {
namespace stems {

enum class StemLayer { Low = 0, Percussive = 1, Harmonic = 2, Residual = 3 };

class Hpss {
public:
    static constexpr int kNumLayers = 4;

    struct Result {
        std::vector<float> layer[kNumLayers];
    };

    explicit Hpss(FftBackend& fft) : fft_(fft), stft_(fft) {}

    /** Median filter length in frames/bins. librosa's default is 31. */
    void setKernelSize(int kernel) { kernel_ = std::max(3, kernel | 1); }

    /** Mask exponent. librosa's default is 2.0. */
    void setPower(float power) { power_ = std::max(0.f, power); }

    /** Split frequency below which content is routed to the Low layer. */
    void setLowSplitHz(float hz) { lowSplitHz_ = std::max(0.f, hz); }

    /**
     * Separate @p input into four disjoint layers.
     * Each output layer is resized to @p length.
     */
    void separate(const float* input, std::size_t length, int sampleRate, Result& out) {
        for (int L = 0; L < kNumLayers; L++) out.layer[L].assign(length, 0.f);
        if (!input || length == 0) return;

        analyse(input, length);
        if (frames_ == 0) {
            // Too short for a single frame. Route everything to Residual so the
            // layers still sum to the source rather than silently dropping it.
            std::copy(input, input + length, out.layer[(int)StemLayer::Residual].begin());
            return;
        }

        buildMasks(sampleRate);

        for (int L = 0; L < kNumLayers; L++) {
            synthesise(input, length, L, out.layer[L]);
        }
    }

    // --- Introspection for cross-validation against a reference implementation.
    // Exposed so tests can median-filter the SAME magnitudes with scipy and
    // compare, which isolates the median filter from STFT differences.
    std::size_t debugFrames() const { return frames_; }
    std::size_t debugBins() const { return fft_.numBins(); }
    const std::vector<float>& debugMagnitude() const { return magnitude_; }
    const std::vector<float>& debugHarmonicMedian() const { return harmMedian_; }
    const std::vector<float>& debugPercussiveMedian() const { return percMedian_; }
    int debugKernel() const { return kernel_; }

private:
    /** Collect the magnitude spectrogram so masks can be computed across frames. */
    void analyse(const float* input, std::size_t length) {
        const std::size_t bins = fft_.numBins();
        magnitude_.clear();
        frames_ = 0;

        std::vector<float> scratchOut(length, 0.f);
        stft_.process(input, scratchOut.data(), length,
                      [&](float* spectrum, std::size_t) {
                          magnitude_.resize((frames_ + 1) * bins);
                          for (std::size_t b = 0; b < bins; b++) {
                              const float re = spectrum[2 * b];
                              const float im = spectrum[2 * b + 1];
                              magnitude_[frames_ * bins + b] = std::sqrt(re * re + im * im);
                          }
                          frames_++;
                      });
    }

    void buildMasks(int sampleRate) {
        const std::size_t bins = fft_.numBins();
        harmMedian_.assign(frames_ * bins, 0.f);
        percMedian_.assign(frames_ * bins, 0.f);

        // Median across TIME enhances sustained content (harmonic).
        std::vector<float> window(kernel_);
        for (std::size_t b = 0; b < bins; b++) {
            for (std::size_t f = 0; f < frames_; f++) {
                harmMedian_[f * bins + b] = medianAlongTime(f, b, bins, window);
            }
        }
        // Median across FREQUENCY enhances transient content (percussive).
        for (std::size_t f = 0; f < frames_; f++) {
            for (std::size_t b = 0; b < bins; b++) {
                percMedian_[f * bins + b] = medianAlongFreq(f, b, bins, window);
            }
        }

        // Bin index of the low split.
        const double binHz = static_cast<double>(sampleRate) / static_cast<double>(fft_.size());
        lowSplitBin_ = static_cast<std::size_t>(std::lround(lowSplitHz_ / binHz));
        lowSplitBin_ = std::min(lowSplitBin_, bins);
    }

    float medianAlongTime(std::size_t f, std::size_t b, std::size_t bins,
                          std::vector<float>& scratch) const {
        const int half = kernel_ / 2;
        int count = 0;
        for (int k = -half; k <= half; k++) {
            const long long idx = static_cast<long long>(f) + k;
            if (idx < 0 || idx >= static_cast<long long>(frames_)) continue;
            scratch[count++] = magnitude_[static_cast<std::size_t>(idx) * bins + b];
        }
        return medianOf(scratch, count);
    }

    float medianAlongFreq(std::size_t f, std::size_t b, std::size_t bins,
                          std::vector<float>& scratch) const {
        const int half = kernel_ / 2;
        int count = 0;
        for (int k = -half; k <= half; k++) {
            const long long idx = static_cast<long long>(b) + k;
            if (idx < 0 || idx >= static_cast<long long>(bins)) continue;
            scratch[count++] = magnitude_[f * bins + static_cast<std::size_t>(idx)];
        }
        return medianOf(scratch, count);
    }

    static float medianOf(std::vector<float>& v, int count) {
        if (count == 0) return 0.f;
        std::nth_element(v.begin(), v.begin() + count / 2, v.begin() + count);
        return v[count / 2];
    }

    /**
     * Re-run the STFT applying this layer's mask.
     *
     * Masks are computed here rather than cached so each layer's weights are
     * derived from the same medians, which is what keeps them summing to one.
     */
    void synthesise(const float* input, std::size_t length, int layer,
                    std::vector<float>& out) {
        const std::size_t bins = fft_.numBins();
        std::size_t frame = 0;

        stft_.process(input, out.data(), length, [&](float* spectrum, std::size_t) {
            if (frame >= frames_) return;
            for (std::size_t b = 0; b < bins; b++) {
                const float gain = maskFor(layer, frame, b, bins);
                spectrum[2 * b]     *= gain;
                spectrum[2 * b + 1] *= gain;
            }
            frame++;
        });
    }

    /**
     * Disjoint masks summing to 1 per bin.
     *
     *   Low         : everything below the split
     *   Percussive  : soft percussive share of what remains
     *   Harmonic    : soft harmonic share of what remains
     *   Residual    : whatever neither claims
     */
    float maskFor(int layer, std::size_t f, std::size_t b, std::size_t bins) const {
        const bool isLow = b < lowSplitBin_;
        if (isLow) {
            return (layer == (int)StemLayer::Low) ? 1.f : 0.f;
        }
        if (layer == (int)StemLayer::Low) return 0.f;

        const float h = harmMedian_[f * bins + b];
        const float p = percMedian_[f * bins + b];
        const float hp = std::pow(h, power_);
        const float pp = std::pow(p, power_);
        const float denom = hp + pp;

        if (!(denom > 1e-20f)) {
            // No energy to attribute. Give it to Residual so the masks still
            // sum to one and the layers remain disjoint.
            return (layer == (int)StemLayer::Residual) ? 1.f : 0.f;
        }

        const float harmShare = hp / denom;
        const float percShare = pp / denom;

        switch (static_cast<StemLayer>(layer)) {
            case StemLayer::Percussive: return percShare;
            case StemLayer::Harmonic:   return harmShare;
            case StemLayer::Residual:   return 0.f;  // shares already total 1
            default:                    return 0.f;
        }
    }

    FftBackend& fft_;
    Stft stft_;

    int kernel_ = 31;
    float power_ = 2.f;
    float lowSplitHz_ = 200.f;
    std::size_t lowSplitBin_ = 0;

    std::size_t frames_ = 0;
    std::vector<float> magnitude_;
    std::vector<float> harmMedian_;
    std::vector<float> percMedian_;
};

}  // namespace stems
}  // namespace WiggleRoom
