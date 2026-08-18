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
 *     and costs nothing when the ratio is one or below. The span is rounded UP:
 *     nearly every knob position gives a fractional ratio, and truncating a
 *     step of 1.9995 to one sample turns the filter off exactly where it is
 *     still needed.
 *
 *  5. NORMALISATION USES THE SOURCE WINDOW'S PEAK, NOT THE FRAME'S. Scaling the
 *     frame to full scale would undo the averaging above: a tone the filter
 *     rejected down to five per cent gets multiplied by twenty and published at
 *     full scale, so the anti-aliasing exists only in a diagnostic. Measuring
 *     the level before decimation means whatever the filter rejected stays
 *     rejected, while a quiet source is still brought up.
 *
 *  6. NOTHING TRUSTS THE STEM DATA. A single non-finite sample anywhere in the
 *     window would spread through the mean and the normalisation into every
 *     value of every frame published afterwards, and RingBuffer::write stores
 *     what it is given.
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
            buffers_[0][i] = 0.f;
            buffers_[1][i] = 0.f;
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
     * Work units per process() call, where a unit is one source read or one
     * output write.
     *
     * Counting OUTPUT SAMPLES instead was wrong. At a window of 8192 each
     * output sample costs four source reads, so a call in the reading phase did
     * four times the work of one in the finalising phase while both reported
     * the same number, and the cost still jumped at the boundary between them.
     *
     * A frame costs kFrameSize * (span + 1) units: the reads, then one write
     * each. At the default window that is 4096, so the default budget of 256
     * publishes in sixteen calls, which at a 256 sample block is about 85 ms.
     * A longer window genuinely costs more and therefore takes longer, which is
     * the point of a fixed budget; at the maximum window it is forty calls.
     */
    void setBudgetPerCall(int units) {
        budget_ = std::max(1, units);
    }

    /**
     * The per-call bound actually in force.
     *
     * A slot's source reads are averaged together and so cannot be split across
     * calls, which makes one slot the floor. Asking for less than that raises
     * the bound rather than breaking it, and this reports what is really
     * promised.
     */
    std::size_t effectiveBudget() const {
        // Both the span in force and the one the CONFIGURED window implies.
        // span_ is only recalculated when a build begins, so reading it alone
        // reported the old window's bound to anyone who inspected this straight
        // after changing the setting, which is exactly when a caller looks.
        return std::max<std::size_t>((std::size_t)budget_,
                                     std::max(span_, spanFor(windowSamples_)));
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
        if (!std::isfinite(playhead)) return false;
        const auto& source = set->layer[layer].channel[0];

        // A stem set too short to interpolate cannot produce a frame, but it
        // still has to REPLACE one. Returning early on the size left frame()
        // showing the previous recording indefinitely once a short take was
        // published, and Hpss::separate sizes every layer to the input length
        // and accepts sub-frame input, so such a set really does reach here.
        const bool degenerate = source.size() < 2;
        const bool alreadyHandled = degenerate && silenceIssuedFor(set->generation, layer);

        // A new stem set, a different layer or a changed window means whatever
        // is being built, or was last published, no longer describes the
        // material. Start again rather than finish a frame that is half one
        // thing and half another.
        // A changed WINDOW is deliberately not stale. Everything the build
        // depends on is snapshotted, so finishing with the old size cannot mix
        // two windows together, and the new size is picked up by the next
        // build. Restarting on it meant that automating wt_window, or simply
        // turning it slowly enough to cross an integer on each call, discarded
        // the progress every time and no frame could ever finish: the
        // oscillator kept the previous wavetable until the control stopped
        // moving, which is the opposite of what a moving control should do.
        const bool stale = !alreadyHandled &&
                           ((phase_ == Phase::Idle) ||
                            set->generation != buildGeneration_ ||
                            layer != buildLayer_);

        if (stale) {
            if (degenerate) {
                beginSilentFinalise(set->generation, layer);
            } else {
                // Starting a build snapshots WHERE and WHAT to read, once.
                // Re-reading the playhead every call would smear a single frame
                // across however far the transport moved while it was being
                // built, so the frame would never correspond to any actual
                // moment in the material.
                beginBuild(set, layer, source.size(), playhead);
            }
        }

        // Degenerate and already dealt with: nothing left to do.
        if (phase_ == Phase::Idle) return false;

        if (phase_ == Phase::Reading) {
            // Charged by the SPAN, so a wide window costs more calls rather
            // than more work per call.
            // A slot is the smallest indivisible unit: its source reads are
            // averaged together, so it cannot be split across calls. A budget
            // below the span is therefore raised to one slot rather than
            // silently exceeded, and the bound this class advertises is the
            // larger of the two.
            const std::size_t limit = std::max<std::size_t>((std::size_t)budget_, span_);
            std::size_t work = 0;
            std::size_t produced = 0;
            while (cursor_ < kFrameSize && work + span_ <= limit) {
                const float slot = readWindowSlot(*set, layer, cursor_);
                build_[cursor_] = slot;
                // Accumulated here so the mean the TAPER introduces can be
                // computed without another whole pass. See beginFinalise().
                taperedSum_ += static_cast<double>(slot) * edgeWindow_[cursor_];
                taperWeightSum_ += edgeWindow_[cursor_];
                cursor_++;
                produced++;
                work += span_;
            }
            samplesLastCall_ = produced;
            workLastCall_ = work;
            if (cursor_ < kFrameSize) return false;
            beginFinalise();
            return false;
        }

        // Finalising is amortised too. Doing the mean, the peak and the copy in
        // the call that happens to complete the read added three whole extra
        // passes to one call in sixteen, which is exactly the periodic spike
        // this class exists to avoid, and debugSamplesLastCall() did not count
        // them so the amortisation test could not see it.
        const std::size_t count =
            std::min<std::size_t>(kFrameSize - cursor_, (std::size_t)budget_);
        float* back = buffers_[1 - frontIndex_];
        for (std::size_t i = 0; i < count; i++) {
            const std::size_t at = cursor_ + i;
            // DC comes off BEFORE the taper. Tapering first multiplies the
            // offset by the fade, so a constant source becomes an edge-shaped
            // waveform which then normalises to full scale instead of silence.
            back[at] = static_cast<float>(
                ((build_[at] - mean_) * edgeWindow_[at] - taperMean_) * gain_);
        }
        cursor_ += count;
        samplesLastCall_ = count;
        workLastCall_ = count;
        if (cursor_ < kFrameSize) return false;

        // Publication is a pointer swap, so it costs nothing whatever the frame
        // size is.
        frontIndex_ = 1 - frontIndex_;
        phase_ = Phase::Idle;
        frameCount_++;
        return true;
    }

    /** The most recently completed frame. Always kFrameSize samples. */
    const float* frame() const { return buffers_[frontIndex_]; }
    static constexpr std::size_t frameSize() { return kFrameSize; }

    /** Increments each time a frame completes. */
    uint64_t frameCount() const { return frameCount_; }

    /**
     * Peak of the source window, after DC removal and before decimation.
     *
     * This is what the frame is normalised against, so it is worth being able
     * to read it.
     */
    double debugSourcePeak() const { return sourcePeak_; }

    /** Peak of the published frame. Diagnostics. */
    double debugFramePeak() const {
        double peak = 0.0;
        for (std::size_t i = 0; i < kFrameSize; i++) {
            peak = std::max(peak, std::fabs((double)buffers_[frontIndex_][i]));
        }
        return peak;
    }

    /** Output samples produced by the last process() call. Diagnostics. */
    std::size_t debugSamplesLastCall() const { return samplesLastCall_; }

    /**
     * Work units spent by the last process() call, a source read or an output
     * write being one each.
     *
     * This, not the sample count, is what the budget bounds and what a test of
     * the amortisation has to read: at a wide window one output sample costs
     * several source reads, so equal sample counts hide unequal work.
     */
    std::size_t debugWorkLastCall() const { return workLastCall_; }

    /** Discard any part-built frame. For patch load and sample rate changes. */
    void reset() {
        phase_ = Phase::Idle;
        cursor_ = 0;
        // Re-arm the degenerate-take handling. Leaving this set while dropping
        // the phase meant a reset partway through replacing a frame satisfied
        // the already-handled test forever afterwards, so finalisation never
        // restarted and the previous audible frame stayed visible. Patch load
        // and sample rate changes both call this.
        silenceIssued_ = false;
    }

private:
    enum class Phase { Idle, Reading, Finalising };

    /** Source reads per output sample for a given window. */
    static std::size_t spanFor(int window) {
        const double step = static_cast<double>(window) / static_cast<double>(kFrameSize);
        return static_cast<std::size_t>(
            std::max(1, static_cast<int>(std::ceil(step - 1e-9))));
    }

    void beginBuild(const StemSet* set, int layer, std::size_t sourceLength,
                    double playhead) {
        phase_ = Phase::Reading;
        cursor_ = 0;
        buildGeneration_ = set->generation;
        buildLayer_ = layer;
        buildWindow_ = windowSamples_;
        silenceIssued_ = false;

        const double window = static_cast<double>(buildWindow_);
        // The window is CENTRED on the playhead, and the offset moves it by up
        // to a whole window either way.
        const double centre = playhead + static_cast<double>(offset_) * window;
        buildStart_ = centre - window * 0.5;
        buildStep_ = window / static_cast<double>(kFrameSize);
        // Rounded UP, not truncated. A window of 4095 gives a step just under
        // two, and truncating that to one sample turns the filter off for
        // nearly every knob position that is not an exact multiple of the frame.
        span_ = spanFor(buildWindow_);
        buildLength_ = sourceLength;

        sourceSum_ = 0.0;
        taperedSum_ = 0.0;
        taperWeightSum_ = 0.0;
        taperMean_ = 0.0;
        sourceCount_ = 0;
        sourceMin_ = 0.0;
        sourceMax_ = 0.0;
        haveSourceExtent_ = false;
    }

    bool silenceIssuedFor(uint64_t generation, int layer) const {
        // The window is not part of this. A degenerate take publishes silence
        // whatever the window is, so re-arming on a window change would only
        // republish the same silence.
        return silenceIssued_ && buildGeneration_ == generation && buildLayer_ == layer;
    }

    /**
     * Replace the frame with silence, through the normal amortised path.
     *
     * Writing all 2048 entries in the call that noticed the short take would be
     * the same frame-boundary spike the reading and finalising paths exist to
     * avoid. A gain of zero makes the ordinary finalise loop emit silence, so
     * this needs no separate code path and no separate budget.
     */
    void beginSilentFinalise(uint64_t generation, int layer) {
        phase_ = Phase::Finalising;
        cursor_ = 0;
        mean_ = 0.0;
        gain_ = 0.0;
        taperMean_ = 0.0;
        sourcePeak_ = 0.0;
        span_ = 1;
        buildGeneration_ = generation;
        buildLayer_ = layer;
        buildWindow_ = windowSamples_;
        silenceIssued_ = true;
    }

    void beginFinalise() {
        phase_ = Phase::Finalising;
        cursor_ = 0;

        mean_ = (sourceCount_ > 0) ? (sourceSum_ / (double)sourceCount_) : 0.0;
        sourcePeak_ = haveSourceExtent_
                          ? std::max(std::fabs(sourceMax_ - mean_), std::fabs(sourceMin_ - mean_))
                          : 0.0;

        // Normalise against the SOURCE level, not the decimated frame's. See
        // note 5: scaling the frame to full scale would multiply whatever the
        // anti-alias average rejected straight back up, so a tone brought down
        // to five per cent would be published at unity and the filter would
        // exist only in a diagnostic.
        //
        // Silence stays silent for the same reason it always did: there is
        // nothing to bring up, and dividing by the residue would make a
        // full-scale frame out of rounding noise.
        // The taper puts DC back. Removing the source mean makes the UNTAPERED
        // window zero-mean, but multiplying by a fade that is not symmetric
        // about the content shifts it again: a window whose edges lean positive
        // against a negative interior published a frame sitting about five per
        // cent of full scale off centre, which eats headroom in the oscillator
        // and the lowpass gate and can click.
        //
        // sum((x - mean) * w) = sum(x * w) - mean * sum(w), and both sums are
        // accumulated during the read, so this costs no extra pass.
        const double tapered = taperedSum_ - mean_ * taperWeightSum_;
        taperMean_ = tapered / static_cast<double>(kFrameSize);

        // The gain has to account for that correction, or it stops being a
        // normalisation. Removing taperMean_ AFTER scaling shifts every sample,
        // so a window whose faded edge leans one way against an interior peak
        // leaning the other pushes that peak past full scale: about 1.025 in the
        // worst case, which downstream stages then clip.
        //
        // The bound is analytic rather than measured. The taper never exceeds
        // one, so |(x - mean) * w| <= sourcePeak_, and the shifted result is
        // therefore within sourcePeak_ + |taperMean_|. That is O(1), needs no
        // extra pass over the frame, and is strictly smaller than 1 /
        // sourcePeak_, so it cannot boost back what the decimation average
        // rejected.
        const double bound = sourcePeak_ + std::fabs(taperMean_);
        gain_ = (bound > 1e-7) ? (1.0 / bound) : 0.0;
    }

    /**
     * One output sample: the average of the source over the slot it covers.
     *
     * Point sampling would be enough while the window is no longer than the
     * frame. At the top of the range it is four times longer, and every fourth
     * sample folds everything above a quarter of the frame's Nyquist straight
     * back down into the audible part of the waveform. See note 4.
     */
    float readWindowSlot(const StemSet& set, int layer, std::size_t outputIndex) {
        // span_ is fixed for the whole build, so the cost of every slot is
        // known in advance and can be charged against the budget.
        // The slot is CENTRED on the position this output sample maps to, not
        // started there. Starting it there puts every read half a step late,
        // and at a unit ratio, which is the default window, that means reading
        // halfway between two source samples and averaging them when no
        // resampling is called for at all. A stem alternating between +1 and -1
        // came out completely silent, and ordinary high-frequency content was
        // being attenuated for no reason.
        const double centre = buildStart_ + buildStep_ * static_cast<double>(outputIndex);
        const double half = buildStep_ * 0.5;
        const double from = centre - half;
        const double to = centre + half;

        const std::size_t span = span_;

        double accumulator = 0.0;
        for (std::size_t i = 0; i < span; i++) {
            const double at = from + (to - from) * (static_cast<double>(i) + 0.5) /
                                         static_cast<double>(span);
            const double value = readSource(set, layer, at);
            accumulator += value;

            // The level BEFORE decimation, which is what the frame is
            // normalised against.
            sourceSum_ += value;
            sourceCount_++;
            if (!haveSourceExtent_) {
                sourceMin_ = sourceMax_ = value;
                haveSourceExtent_ = true;
            } else {
                sourceMin_ = std::min(sourceMin_, value);
                sourceMax_ = std::max(sourceMax_, value);
            }
        }
        return static_cast<float>(accumulator / static_cast<double>(span));
    }

    /**
     * Linear read, holding the end samples outside the stem.
     *
     * HELD, not zero-padded. The window is centred on the playhead, so at the
     * loop start half of it lies before the beginning of the material. Reading
     * zero there puts artificial silence into the mean and the peak, and after
     * DC removal the padded half and the real half become equal and opposite:
     * a constant 1.0 stem at playhead 0 came out as a full-scale square rather
     * than silence. Holding the edge sample means padding contributes no shape
     * of its own.
     *
     * Non-finite positions are still refused outright. There is no nearest
     * endpoint to a NaN, and casting one to size_t is undefined.
     */
    double readSource(const StemSet& set, int layer, double position) const {
        const std::size_t n = buildLength_;
        if (n == 0 || !std::isfinite(position)) return 0.0;
        position = std::min(std::max(position, 0.0), static_cast<double>(n) - 1.0);
        const std::size_t i0 = static_cast<std::size_t>(position);
        const std::size_t i1 = std::min(i0 + 1, n - 1);
        const double frac = position - static_cast<double>(i0);

        const auto& left = set.layer[layer].channel[0];
        double value = left[i0] + (left[i1] - left[i0]) * frac;

        // Both channels. The voice is mono and there is no channel selector, so
        // reading only the left one publishes a silent wavetable for a stem
        // panned hard right, and loses half the timbre of every other stereo
        // recording. The worker separates the two sides independently, so the
        // right channel really does carry different material.
        const auto& right = set.layer[layer].channel[1];
        if (set.channels > 1 && right.size() == left.size()) {
            const double r = right[i0] + (right[i1] - right[i0]) * frac;
            value = 0.5 * (value + r);
        }

        // Never trust the stem. RingBuffer::write stores whatever it is given,
        // so one non-finite sample in a recording would otherwise spread through
        // the mean and the gain into every value of every frame published after
        // it. See note 6.
        return std::isfinite(value) ? value : 0.0;
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
    // kFrameSize * (span + 1) units per frame, so 256 publishes the default
    // window in sixteen calls.
    int budget_ = 2 * static_cast<int>(kFrameSize) / 16;

    Phase phase_ = Phase::Idle;
    std::size_t cursor_ = 0;
    uint64_t buildGeneration_ = 0;
    int buildLayer_ = 0;
    int buildWindow_ = 2048;
    double buildStart_ = 0.0;
    double buildStep_ = 1.0;
    std::size_t buildLength_ = 0;
    std::size_t span_ = 1;

    double sourceSum_ = 0.0;
    double taperedSum_ = 0.0;
    double taperWeightSum_ = 0.0;
    double taperMean_ = 0.0;
    std::size_t sourceCount_ = 0;
    double sourceMin_ = 0.0;
    double sourceMax_ = 0.0;
    bool haveSourceExtent_ = false;
    double mean_ = 0.0;
    double gain_ = 0.0;
    double sourcePeak_ = 0.0;

    bool silenceIssued_ = false;
    std::size_t samplesLastCall_ = 0;
    std::size_t workLastCall_ = 0;
    uint64_t frameCount_ = 0;

    int frontIndex_ = 0;
    float buffers_[2][kFrameSize];
    float build_[kFrameSize];
    float edgeWindow_[kFrameSize];
};

}  // namespace stems
}  // namespace WiggleRoom
