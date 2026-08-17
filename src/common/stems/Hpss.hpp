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
#include <atomic>
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

    /**
     * Separation margin. At 1.0 the harmonic and percussive shares sum to
     * exactly 1 and nothing is left over, which is librosa's behaviour but
     * leaves our fourth layer permanently silent. Above 1.0 each share is
     * attenuated where the two are comparable, and the unclaimed remainder
     * becomes Residual. This is the Driedger/Muller/Disch margin idea in soft
     * form.
     */
    void setMargin(float margin) { margin_ = std::max(1.f, margin); }

    /** Split frequency below which content is routed to the Low layer. */
    void setLowSplitHz(float hz) { lowSplitHz_ = std::max(0.f, hz); }

    /**
     * Optional cancellation flag, polled at frame granularity.
     *
     * Without this, stop() has to wait for a whole separation to finish. At the
     * 32 second buffer limit that can freeze module removal or host shutdown
     * for many seconds, because clearing a running_ flag cannot interrupt a
     * separate() already in progress.
     */
    void setAbortFlag(const std::atomic<bool>* flag) { abort_ = flag; }

    /** True when the last separate() returned early because of the abort flag. */
    bool wasAborted() const { return aborted_; }

    /**
     * Separate @p input into four disjoint layers.
     * Each output layer is resized to @p length.
     */
    void separate(const float* input, std::size_t length, int sampleRate, Result& out) {
        aborted_ = false;
        for (int L = 0; L < kNumLayers; L++) out.layer[L].assign(length, 0.f);
        if (!input || length == 0) return;
        if (aborting()) { aborted_ = true; return; }

        // Detect sub-frame input BEFORE analysis. Stft pads a whole frame on
        // both sides, so analyse() always yields frames and a frames_ == 0
        // check here would be unreachable; short recordings would be smeared
        // spectrally across all four layers instead of passing through.
        if (length < fft_.size()) {
            std::copy(input, input + length, out.layer[(int)StemLayer::Residual].begin());
            return;
        }

        analyse(input, length);
        if (frames_ == 0) {
            std::copy(input, input + length, out.layer[(int)StemLayer::Residual].begin());
            return;
        }

        if (aborting()) { aborted_ = true; return; }
        buildMasks(sampleRate);

        for (int L = 0; L < kNumLayers; L++) {
            if (aborting()) { aborted_ = true; return; }
            synthesise(input, length, L, out.layer[L]);
        }
    }

    /** Expose the share computation so its algebra can be tested directly. */
    void debugShares(float h, float p, float& harm, float& perc, float& res) const {
        harm = shareOf(h, p);
        perc = shareOf(p, h);
        res  = std::max(0.f, 1.f - harm - perc);
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
            if (aborting()) return;
            for (std::size_t f = 0; f < frames_; f++) {
                harmMedian_[f * bins + b] = medianAlongTime(f, b, bins, window);
            }
        }
        // Median across FREQUENCY enhances transient content (percussive).
        for (std::size_t f = 0; f < frames_; f++) {
            if (aborting()) return;
            for (std::size_t b = 0; b < bins; b++) {
                percMedian_[f * bins + b] = medianAlongFreq(f, b, bins, window);
            }
        }

        // Bin index of the low split.
        // Ceiling, not rounding. lowSplitBin_ is the first bin AT OR ABOVE the
        // split, so `b < lowSplitBin_` keeps every bin whose centre is below it.
        // Rounding excluded such bins: at 44.1 kHz with a 2048-point FFT and a
        // 200 Hz split it gave bin 9, whose centre is about 193.8 Hz.
        const double binHz = static_cast<double>(sampleRate) / static_cast<double>(fft_.size());
        lowSplitBin_ = static_cast<std::size_t>(std::ceil(lowSplitHz_ / binHz));
        lowSplitBin_ = std::min(lowSplitBin_, bins);
    }

    /**
     * scipy.ndimage 'reflect' index mapping: (d c b a | a b c d | d c b a).
     *
     * Dropping out-of-range neighbours instead would leave edge windows shorter
     * than the kernel, and sometimes even-length, so the first and last
     * kernel/2 frames would be filtered differently from the interior. With the
     * default kernel that is 15 frames at each end, which is exactly where loop
     * boundaries live.
     */
    static std::size_t reflectIndex(long long i, long long n) {
        if (n <= 1) return 0;
        const long long period = 2 * n;
        long long m = ((i % period) + period) % period;   // wrap into [0, 2n)
        if (m >= n) m = period - 1 - m;                   // fold the upper half
        return static_cast<std::size_t>(m);
    }

    float medianAlongTime(std::size_t f, std::size_t b, std::size_t bins,
                          std::vector<float>& scratch) const {
        const int half = kernel_ / 2;
        int count = 0;
        for (int k = -half; k <= half; k++) {
            const std::size_t idx =
                reflectIndex(static_cast<long long>(f) + k, static_cast<long long>(frames_));
            scratch[count++] = magnitude_[idx * bins + b];
        }
        return medianOf(scratch, count);
    }

    float medianAlongFreq(std::size_t f, std::size_t b, std::size_t bins,
                          std::vector<float>& scratch) const {
        const int half = kernel_ / 2;
        int count = 0;
        for (int k = -half; k <= half; k++) {
            const std::size_t idx =
                reflectIndex(static_cast<long long>(b) + k, static_cast<long long>(bins));
            scratch[count++] = magnitude_[f * bins + idx];
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
        const float denom = std::pow(h, power_) + std::pow(p, power_);

        if (!(denom > 1e-20f)) {
            // No energy to attribute. Give it to Residual so the masks still
            // sum to one and the layers remain disjoint.
            return (layer == (int)StemLayer::Residual) ? 1.f : 0.f;
        }

        // Margin-weighted soft masks. The margin scales the COMPETING median
        // before the exponent, matching the reference: h^p / (h^p + (m*p)^p).
        // Applying it afterwards gives a weaker effective margin than
        // configured, and correspondingly less residual.
        const float harmShare = shareOf(h, p);
        const float percShare = shareOf(p, h);

        switch (static_cast<StemLayer>(layer)) {
            case StemLayer::Percussive: return percShare;
            case StemLayer::Harmonic:   return harmShare;
            case StemLayer::Residual:
                // Whatever the two shares leave unclaimed. Clamped because
                // rounding can push the sum a hair over 1.
                return std::max(0.f, 1.f - harmShare - percShare);
            default:                    return 0.f;
        }
    }

    bool aborting() const {
        return abort_ && abort_->load(std::memory_order_relaxed);
    }

    /** mine^power / (mine^power + (margin * other)^power). */
    float shareOf(float mine, float other) const {
        const float a = std::pow(mine, power_);
        const float b = std::pow(margin_ * other, power_);
        const float denom = a + b;
        return (denom > 1e-20f) ? (a / denom) : 0.f;
    }

    FftBackend& fft_;
    Stft stft_;

    int kernel_ = 31;
    float power_ = 2.f;
    float lowSplitHz_ = 200.f;
    // Above 1 so Residual carries the ambiguous energy rather than being silent.
    float margin_ = 2.f;
    std::size_t lowSplitBin_ = 0;

    const std::atomic<bool>* abort_ = nullptr;
    bool aborted_ = false;

    std::size_t frames_ = 0;
    std::vector<float> magnitude_;
    std::vector<float> harmMedian_;
    std::vector<float> percMedian_;
};

}  // namespace stems
}  // namespace WiggleRoom
