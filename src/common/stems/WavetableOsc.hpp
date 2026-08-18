#pragma once
/******************************************************************************
 * WavetableOsc - plays the extracted frames for Stems
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable.
 *
 * Reads the frame WavetableExtract publishes, crossfades to each new one at a
 * rate set by wt_morph, and tracks 1 V/octave from the quantiser plus the
 * coarse and fine controls.
 *
 * WHY MIP MAPS. A frame holds up to 1024 harmonics. Playing it at 440 Hz on a
 * 48 kHz system leaves room for 54 before Nyquist, so everything above that
 * folds back, and it folds back into fixed inharmonic partials because the
 * frame repeats exactly. That is the worst kind of aliasing to listen to: it
 * does not move with the note, so it reads as a metallic ring sitting under
 * every pitch rather than as noise.
 *
 * The frame is therefore reduced to a chain of half-bandwidth copies once, when
 * it arrives, and the one whose content fits below Nyquist is selected per
 * sample from the current pitch. Building the chain costs about two frames'
 * worth of work in total, so it is amortised the same way the extractor
 * amortises building the frame in the first place.
 *
 * WHY THE OSCILLATOR OWNS ITS BUFFERS. The extractor publishes by swapping
 * between two buffers, so the frame being faded FROM is the one the next build
 * writes into. Holding its pointer across a crossfade is a race with nothing
 * but timing to prevent it.
 *
 * Real-time contract: process() allocates nothing and takes no locks.
 ******************************************************************************/

#include "WavetableExtract.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace WiggleRoom {
namespace stems {

class WavetableOsc {
public:
    static constexpr std::size_t kFrameSize = WavetableExtract::kFrameSize;

    /**
     * Half-bandwidth copies, including the full-rate frame itself.
     *
     * The shortest is 16 samples, which carries 8 harmonics. Below about 16
     * harmonics a wavetable stops sounding like the source anyway, and the
     * pitch that would need fewer is above the top of the module's range.
     */
    static constexpr int kNumMips = 8;

    WavetableOsc() {
        for (int side = 0; side < 2; side++) {
            for (std::size_t i = 0; i < kMipStorage; i++) mips_[side][i] = 0.f;
            ready_[side] = false;
        }
    }

    void setSampleRate(int sampleRate) {
        sampleRate_ = (sampleRate > 0) ? sampleRate : 48000;
        updateMorph();
    }

    /**
     * Frame interpolation rate. 0 smears, 1 snaps.
     *
     * Mapped exponentially, because the useful range is bunched at the fast
     * end: linear mapping put everything from a barely audible smear to a hard
     * switch in the top tenth of the control.
     */
    void setMorph(float morph) {
        if (!std::isfinite(morph)) return;
        morph_ = std::min(std::max(morph, 0.f), 1.f);
        updateMorph();
    }

    void setCoarse(float semitones) {
        if (!std::isfinite(semitones)) return;
        coarse_ = std::min(std::max(semitones, -24.f), 24.f);
    }

    void setFine(float semitones) {
        if (!std::isfinite(semitones)) return;
        fine_ = std::min(std::max(semitones, -1.f), 1.f);
    }

    void setLevel(float level) {
        if (!std::isfinite(level)) return;
        level_ = std::min(std::max(level, 0.f), 1.f);
    }

    /** Work units per offerFrame() call while a mip chain is being built. */
    void setBudgetPerCall(int units) { budget_ = std::max(1, units); }

    /**
     * Offer the extractor's current frame.
     *
     * Call once per process() with whatever the extractor currently holds. A
     * change in @p frameCount starts a new chain; the frame is copied here
     * rather than referenced, for the reason in the header.
     */
    void offerFrame(const float* frame, uint64_t frameCount) {
        if (!frame) return;
        if (frameCount != offeredCount_) {
            offeredCount_ = frameCount;
            buildSide_ = 1 - activeSide_;
            buildCursor_ = 0;
            buildLevel_ = 0;
            buildBase_ = 0;
            building_ = true;
            ready_[buildSide_] = false;
        }
        if (building_) advanceBuild(frame);
    }

    /**
     * One sample.
     *
     * @param volts  1 V/octave, normally from the quantiser. 0 V is C4.
     */
    float process(float volts) {
        if (!ready_[activeSide_] && !ready_[1 - activeSide_]) return 0.f;

        const double frequency = frequencyFor(volts);
        // Phase is kept in cycles, not samples, so it is independent of which
        // mip is selected. Advancing an index into a mip would have to be
        // rescaled every time the level changed, and the rescaling lands in the
        // middle of the waveform.
        const double increment = frequency / sampleRate_;
        phase_ += increment;
        phase_ -= std::floor(phase_);

        const int level = mipFor(frequency);
        float out = readMip(activeSide_, level, phase_);

        if (crossfade_ < 1.f && ready_[1 - activeSide_]) {
            const float incoming = readMip(1 - activeSide_, level, phase_);
            out = out + (incoming - out) * crossfade_;
            crossfade_ = std::min(1.f, crossfade_ + morphStep_);
            if (crossfade_ >= 1.f) activeSide_ = 1 - activeSide_;
        }
        return out * level_;
    }

    /** Frame currently sounding, for tests. */
    int debugActiveSide() const { return activeSide_; }

    /** Mip level the last process() call read. Diagnostics. */
    int debugLastMip() const { return lastMip_; }

    /** Work units spent by the last offerFrame() call. Diagnostics. */
    std::size_t debugWorkLastCall() const { return workLastCall_; }

    void reset() {
        phase_ = 0.0;
        crossfade_ = 1.f;
        building_ = false;
        buildCursor_ = 0;
    }

private:
    /** Total floats across the whole chain: 2048 + 1024 + ... + 16. */
    static constexpr std::size_t kMipStorage = 4080;

    static std::size_t mipLength(int level) { return kFrameSize >> level; }

    static std::size_t mipOffset(int level) {
        std::size_t offset = 0;
        for (int i = 0; i < level; i++) offset += mipLength(i);
        return offset;
    }

    double frequencyFor(float volts) const {
        if (!std::isfinite(volts)) volts = 0.f;
        // Clamped before the exponential, not after. exp2 of a large finite
        // value is an infinity, and an infinite increment leaves the phase NaN
        // for the rest of the patch.
        const double semitones = std::min(std::max((double)volts * 12.0, -240.0), 240.0) +
                                 coarse_ + fine_;
        const double hz = 261.6255653005986 * std::exp2(semitones / 12.0);
        return std::min(std::max(hz, 0.01), sampleRate_ * 0.5);
    }

    /**
     * Coarsest chain entry whose content still fits below Nyquist.
     *
     * Level n carries kFrameSize / 2^(n+1) harmonics, and the note has room for
     * sampleRate / (2 * frequency) of them.
     */
    int mipFor(double frequency) const {
        const double allowed = 0.5 * sampleRate_ / std::max(frequency, 1e-9);
        int level = 0;
        double carried = 0.5 * (double)kFrameSize;
        while (level < kNumMips - 1 && carried > allowed) {
            carried *= 0.5;
            level++;
        }
        lastMip_ = level;
        return level;
    }

    /** Catmull-Rom read of one chain entry, wrapping at both ends. */
    float readMip(int side, int level, double phase) const {
        const std::size_t n = mipLength(level);
        const float* table = mips_[side] + mipOffset(level);
        const double position = phase * (double)n;
        const std::size_t i1 = (std::size_t)position % n;
        const double frac = position - std::floor(position);
        // Wrapped, not clamped. The table IS a cycle, so the sample before the
        // first is the last one; clamping would flatten the waveform at the
        // wrap point and put a discontinuity exactly where the edge fade was
        // applied to remove one.
        const std::size_t i0 = (i1 + n - 1) % n;
        const std::size_t i2 = (i1 + 1) % n;
        const std::size_t i3 = (i1 + 2) % n;

        const double p0 = table[i0], p1 = table[i1], p2 = table[i2], p3 = table[i3];
        return (float)(p1 + 0.5 * frac *
                                (p2 - p0 +
                                 frac * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 +
                                         frac * (3.0 * (p1 - p2) + p3 - p0))));
    }

    /**
     * Build part of a chain.
     *
     * Level 0 is the frame itself; each level after it is the previous one
     * filtered and halved. The filter is a three point Hann kernel rather than a
     * plain pair average: a box filter of two leaves a first sidelobe only 13 dB
     * down, which is audible as the ringing this whole arrangement exists to
     * remove.
     */
    void advanceBuild(const float* frame) {
        std::size_t work = 0;
        float* dest = mips_[buildSide_];

        while (buildCursor_ < kMipStorage && work < (std::size_t)budget_) {
            // The level and its base are carried between calls rather than
            // rescanned. Searching the chain for every sample would multiply the
            // real cost of a unit by the number of levels, so the budget would
            // bound something eight times smaller than the work actually done.
            if (buildLevel_ < kNumMips - 1 &&
                buildCursor_ >= buildBase_ + mipLength(buildLevel_)) {
                buildBase_ += mipLength(buildLevel_);
                buildLevel_++;
            }
            const int level = buildLevel_;
            const std::size_t index = buildCursor_ - buildBase_;

            if (level == 0) {
                dest[buildCursor_] = frame[index];
            } else {
                const std::size_t sourceLength = mipLength(level - 1);
                const float* source = dest + mipOffset(level - 1);
                const std::size_t centre = index * 2;
                const std::size_t before = (centre + sourceLength - 1) % sourceLength;
                const std::size_t after = (centre + 1) % sourceLength;
                dest[buildCursor_] = 0.25f * source[before] + 0.5f * source[centre] +
                                     0.25f * source[after];
            }
            buildCursor_++;
            work++;
        }
        workLastCall_ = work;

        if (buildCursor_ >= kMipStorage) {
            building_ = false;
            ready_[buildSide_] = true;
            // Start the crossfade only once the incoming chain is complete.
            // Fading into a half-built table plays whatever the previous frame
            // left in the unwritten part.
            crossfade_ = ready_[activeSide_] ? 0.f : 1.f;
            if (!ready_[activeSide_]) activeSide_ = buildSide_;
        }
    }

    void updateMorph() {
        // 0 gives a one second smear, 1 gives a single sample. Exponential
        // because the useful range is bunched at the fast end.
        const double seconds = 1.0 * std::exp2(-16.0 * (double)morph_);
        const double samples = std::max(1.0, seconds * sampleRate_);
        morphStep_ = (float)(1.0 / samples);
    }

    int sampleRate_ = 48000;
    float morph_ = 0.5f;
    float morphStep_ = 0.f;
    float coarse_ = 0.f;
    float fine_ = 0.f;
    float level_ = 0.7f;
    int budget_ = 512;

    double phase_ = 0.0;
    float crossfade_ = 1.f;
    int activeSide_ = 0;
    int buildSide_ = 1;
    bool building_ = false;
    bool ready_[2] = {false, false};
    std::size_t buildCursor_ = 0;
    int buildLevel_ = 0;
    std::size_t buildBase_ = 0;
    uint64_t offeredCount_ = 0;
    std::size_t workLastCall_ = 0;
    mutable int lastMip_ = 0;

    float mips_[2][kMipStorage];
};

}  // namespace stems
}  // namespace WiggleRoom
