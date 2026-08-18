#pragma once
/******************************************************************************
 * WavetableExtract - builds oscillator frames from a stem for Stems
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable.
 *
 * Takes wt_window samples of the selected stem, centred on the playhead plus
 * wt_offset, and turns them into a fixed-size oscillator frame.
 *
 * FOUR THINGS THAT MATTER HERE, three of them straight from the spec:
 *
 *  1. THE FRAME IS ALWAYS THE SAME LENGTH. That is what makes wt_window change
 *     how much source material is captured rather than the oscillator's pitch.
 *     The oscillator's fundamental is set by how fast it reads a frame, so a
 *     frame whose length moved with the window would retune the voice every
 *     time the control was touched.
 *
 *  2. EXTRACTION READS THE UNSTRETCHED STEM. This class is handed the stem and
 *     a position in it, never a stretched signal. A stretch artefact in a loop
 *     goes by once; the same artefact in a wavetable is played cyclically at
 *     audio rate and becomes a stable timbral feature.
 *
 *  3. THE WORK IS AMORTISED. Building a whole frame in the block where the
 *     playhead crosses a boundary is a spike, and the spike lands on the audio
 *     thread. A fixed budget of output samples per call spreads it flat.
 *
 *  4. DECIMATION IS AVERAGED, NOT POINT-SAMPLED. At the top of the range the
 *     window is four times the frame, so taking every fourth sample folds
 *     everything above a quarter of the frame's Nyquist back down. Averaging
 *     each output sample over the source samples it spans is the cheapest fix
 *     and costs nothing when the ratio is one or below.
 *
 * Real-time contract: process() allocates nothing and takes no locks.
 ******************************************************************************/

#include "SeparationWorker.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace WiggleRoom {
namespace stems {

class WavetableExtract {
public:
    /** Oscillator frame length. Power of two, fixed. See note 1. */
    static constexpr std::size_t kFrameSize = 2048;

    static constexpr int kMinWindow = 256;
    static constexpr int kMaxWindow = 8192;

    WavetableExtract() {
        for (std::size_t i = 0; i < kFrameSize; i++) {
            front_[i] = 0.f;
            build_[i] = 0.f;
        }
        buildEdgeWindow();
    }

    /** Source samples captured per frame. See note 1. */
    void setWindowSamples(int samples) {
        windowSamples_ = std::min(std::max(samples, kMinWindow), kMaxWindow);
    }

    /** Window centre relative to the playhead, as a fraction of the window. */
    void setOffset(float offset) {
        if (!std::isfinite(offset)) return;
        offset_ = std::min(std::max(offset, -1.f), 1.f);
    }

    /**
     * Output samples produced per process() call.
     *
     * The default spreads a frame over sixteen calls, which at a 256 sample
     * block is about a twelfth of a second: fast enough that the wavetable
     * tracks the playhead, flat enough that no single block pays for a frame.
     */
    void setBudgetPerCall(int samples) {
        budget_ = std::max(1, samples);
    }

    int windowSamples() const { return windowSamples_; }
    float offset() const { return offset_; }

    /**
     * Advance the build by at most one budget's worth.
     *
     * @param set       Published stems, or nullptr while separating.
     * @param layer     Which stem to read.
     * @param playhead  Position in frames, from Transport.
     * @return          True on the call that completes a frame.
     */
    bool process(const StemSet* set, int layer, double playhead) {
        samplesLastCall_ = 0;
        if (!set) return false;
        if (layer < 0 || layer >= StemSet::kNumLayers) return false;
        const auto& source = set->layer[layer].channel[0];
        if (source.size() < 2) return false;
        if (!std::isfinite(playhead)) return false;

        // Starting a build snapshots WHERE and WHAT to read, once. Re-reading
        // the playhead every call would smear a single frame across however far
        // the transport moved while it was being built, so the frame would
        // never correspond to any actual moment in the material.
        if (!building_) beginBuild(set, layer, source.size(), playhead);

        // A new stem set, a different layer or a changed window mid-build means
        // the snapshot is stale. Start again rather than finish a frame that is
        // half one thing and half another.
        if (set->generation != buildGeneration_ || layer != buildLayer_ ||
            windowSamples_ != buildWindow_) {
            beginBuild(set, layer, source.size(), playhead);
        }

        const std::size_t remaining = kFrameSize - buildIndex_;
        const std::size_t count = std::min<std::size_t>(remaining, (std::size_t)budget_);

        for (std::size_t i = 0; i < count; i++) {
            const std::size_t out = buildIndex_ + i;
            build_[out] = sampleWindow(source, out);
        }
        buildIndex_ += count;
        samplesLastCall_ = count;

        if (buildIndex_ < kFrameSize) return false;

        finishBuild();
        return true;
    }

    /** The most recently completed frame. Always kFrameSize samples. */
    const float* frame() const { return front_; }
    static constexpr std::size_t frameSize() { return kFrameSize; }

    /** Increments each time a frame completes. */
    uint64_t frameCount() const { return frameCount_; }

    /** Peak of the last frame BEFORE normalisation. Diagnostics. */
    double debugRawPeak() const { return rawPeak_; }

    /** Output samples produced by the last process() call. Diagnostics. */
    std::size_t debugSamplesLastCall() const { return samplesLastCall_; }

    /** Discard any part-built frame. For patch load and sample rate changes. */
    void reset() {
        building_ = false;
        buildIndex_ = 0;
    }

private:
    void beginBuild(const StemSet* set, int layer, std::size_t sourceLength,
                    double playhead) {
        building_ = true;
        buildIndex_ = 0;
        buildGeneration_ = set->generation;
        buildLayer_ = layer;
        buildWindow_ = windowSamples_;

        const double window = static_cast<double>(buildWindow_);
        // The window is CENTRED on the playhead, and the offset moves it by up
        // to a whole window either way.
        const double centre = playhead + static_cast<double>(offset_) * window;
        buildStart_ = centre - window * 0.5;
        buildStep_ = window / static_cast<double>(kFrameSize);
        buildLength_ = sourceLength;
    }

    /**
     * One output sample, averaged over the source samples it spans.
     *
     * Point sampling would be enough while the window is no longer than the
     * frame. At the top of the range it is four times longer, and every fourth
     * sample folds everything above a quarter of the frame's Nyquist straight
     * back down into the audible part of the waveform. See note 4.
     */
    float sampleWindow(const std::vector<float>& source, std::size_t outputIndex) const {
        const double from = buildStart_ + buildStep_ * static_cast<double>(outputIndex);
        const double to = from + buildStep_;

        double accumulator = 0.0;
        int taken = 0;
        // One sample per source position when decimating, and exactly one
        // interpolated read when the step is at or below one.
        const int span = std::max(1, static_cast<int>(buildStep_));
        for (int i = 0; i < span; i++) {
            const double at = from + (to - from) * (static_cast<double>(i) + 0.5) /
                                         static_cast<double>(span);
            accumulator += readSource(source, at);
            taken++;
        }
        const float raw = static_cast<float>(accumulator / std::max(1, taken));
        return raw * edgeWindow_[outputIndex];
    }

    /** Linear read, clamped at the ends. Positions outside the stem read zero. */
    double readSource(const std::vector<float>& source, double position) const {
        const std::size_t n = buildLength_;
        // Validate fully while still floating point. Casting a NaN or an
        // infinity to size_t is undefined, and NaN slips past a bare `< 0`.
        if (n == 0 || !std::isfinite(position) || position < 0.0 ||
            position >= static_cast<double>(n)) {
            return 0.0;
        }
        const std::size_t i0 = static_cast<std::size_t>(position);
        const std::size_t i1 = std::min(i0 + 1, n - 1);
        const double frac = position - static_cast<double>(i0);
        return source[i0] + (source[i1] - source[i0]) * frac;
    }

    void finishBuild() {
        building_ = false;

        // Remove DC before normalising. An offset survives the edge fade as a
        // step at the wrap point, and normalising with it still present spends
        // headroom on the offset rather than on the waveform.
        double mean = 0.0;
        for (std::size_t i = 0; i < kFrameSize; i++) mean += build_[i];
        mean /= static_cast<double>(kFrameSize);

        double peak = 0.0;
        for (std::size_t i = 0; i < kFrameSize; i++) {
            build_[i] = static_cast<float>(build_[i] - mean);
            peak = std::max(peak, std::fabs(static_cast<double>(build_[i])));
        }
        // Kept because normalisation hides it, and it is the only direct
        // evidence that decimation is being averaged rather than point sampled:
        // content above the frame's Nyquist should arrive attenuated, and after
        // normalisation an attenuated frame and a full-scale alias look alike.
        rawPeak_ = peak;

        // Silence stays silent. Normalising it would amplify whatever rounding
        // noise the window left behind into a full-scale frame.
        const double gain = (peak > 1e-7) ? (1.0 / peak) : 0.0;
        for (std::size_t i = 0; i < kFrameSize; i++) {
            front_[i] = static_cast<float>(build_[i] * gain);
        }
        frameCount_++;
    }

    /**
     * Raised-cosine fade over the first and last few per cent.
     *
     * The frame is read cyclically, so whatever is at the end runs straight
     * into whatever is at the start. Source audio has no reason to join up
     * there, and the step is a click at the oscillator's own frequency, which
     * reads as a buzz sitting under every note. A short fade at each end costs
     * a little of the waveform and removes it. A full-length window would
     * remove the waveform along with it.
     */
    void buildEdgeWindow() {
        const std::size_t fade = kFrameSize / 20;  // five per cent each end
        for (std::size_t i = 0; i < kFrameSize; i++) {
            float gain = 1.f;
            if (i < fade) {
                gain = 0.5f * (1.f - std::cos(kPi * static_cast<float>(i) /
                                              static_cast<float>(fade)));
            } else if (i >= kFrameSize - fade) {
                const std::size_t j = kFrameSize - 1 - i;
                gain = 0.5f * (1.f - std::cos(kPi * static_cast<float>(j) /
                                              static_cast<float>(fade)));
            }
            edgeWindow_[i] = gain;
        }
    }

    static constexpr float kPi = 3.14159265358979323846f;

    int windowSamples_ = 2048;
    float offset_ = 0.f;
    int budget_ = static_cast<int>(kFrameSize) / 16;

    bool building_ = false;
    std::size_t buildIndex_ = 0;
    uint64_t buildGeneration_ = 0;
    int buildLayer_ = 0;
    int buildWindow_ = 2048;
    double buildStart_ = 0.0;
    double buildStep_ = 1.0;
    std::size_t buildLength_ = 0;

    std::size_t samplesLastCall_ = 0;
    double rawPeak_ = 0.0;
    uint64_t frameCount_ = 0;

    float front_[kFrameSize];
    float build_[kFrameSize];
    float edgeWindow_[kFrameSize];
};

}  // namespace stems
}  // namespace WiggleRoom
