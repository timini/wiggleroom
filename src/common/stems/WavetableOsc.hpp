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
     * Ten levels, down to a four sample table carrying two harmonics. Eight
     * levels was not enough: the shortest was then sixteen samples with eight
     * harmonics, valid only to about 3 kHz, while an ordinary 1 V/octave input
     * reaches that by 3.5 V and keeps going. Above the end of the chain the
     * selection saturates and the content aliases, which is precisely what the
     * chain exists to prevent, so it has to cover the whole reachable range
     * rather than most of it.
     */
    static constexpr int kNumMips = 10;

    WavetableOsc() {
        buildDecimationKernel();
        // Without this the coefficient stays zero until a setter is called, so
        // at the documented defaults the crossfade never advances and the
        // oscillator sits on its first frame forever.
        updateMorph();
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

        // While a crossfade is running, the incoming side is being SOUNDED.
        // Rebuilding it because a newer frame arrived cancels the fade before
        // the sides can swap, and with a morph slower than the extractor's
        // publish interval that happens on every publish, so the oscillator
        // never leaves its first frame. A slow morph is meant to smear past
        // intermediate frames; it is not meant to freeze.
        if (crossfade_ < 1.f) return;

        if (building_) {
            // The frame under us changed. Level 0 is copied from it directly,
            // so carrying on would splice two different frames together.
            if (frameCount != targetCount_) startBuild(frameCount);
            advanceBuild(frame);
            return;
        }

        if (frameCount != targetCount_ || !ready_[activeSide_]) {
            startBuild(frameCount);
            advanceBuild(frame);
        }
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
        // Re-arm. Clearing the build while keeping the frame it was for meant a
        // later offer of that same frame saw neither a changed count nor a
        // running build, so it could never become playable until the extractor
        // happened to publish again.
        targetCount_ = 0;
    }

private:
    /** Total floats across the whole chain: 2048 + 1024 + ... + 4. */
    static constexpr std::size_t kMipStorage = 4092;

    static constexpr double kPi = 3.14159265358979323846;
    /** Half-band decimation filter length. Fifteen taps reach the stop band. */
    static constexpr int kKernelTaps = 15;
    static constexpr int kKernelHalf = kKernelTaps / 2;

    void startBuild(uint64_t frameCount) {
        targetCount_ = frameCount;
        buildSide_ = ready_[activeSide_] ? (1 - activeSide_) : activeSide_;
        buildCursor_ = 0;
        buildLevel_ = 0;
        buildBase_ = 0;
        building_ = true;
        ready_[buildSide_] = false;
    }

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
        // Finiteness is already guaranteed by the semitone clamp above, so this
        // is a bound on absurdity rather than a guard: it is not observable
        // through this interface, since a wildly out of range pitch produces
        // aliased nonsense either way. It stays because an increment of
        // thousands of cycles per sample is worth not handing to the phase
        // accumulator, not because a test can see the difference.
        return std::min(std::max(hz, 0.01), sampleRate_ * 0.5);
    }

    /**
     * Coarsest chain entry whose content still fits below Nyquist.
     *
     * Level n carries kFrameSize / 2^(n+1) harmonics, and the note has room for
     * sampleRate / (2 * frequency) of them.
     */
    int mipFor(double frequency) const {
        // The 0.5 is the Nyquist criterion and is not a tuning knob: biasing it
        // to 1.0, so a longer table is chosen, was measured and makes the floor
        // steadily worse with pitch, reaching -14.8 dB at 5 V because the extra
        // harmonics genuinely do not fit.
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
        // Wrapped rather than clamped, because the table IS a cycle and the
        // sample before the first is the last one. Worth being clear that this
        // is correctness rather than a measured improvement: clamping the two
        // outer taps instead was tried against a frame whose ends are a full
        // scale apart and produced an identical maximum step and peak to six
        // decimal places, because it only affects one table position per cycle.
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
     * filtered and halved.
     *
     * The filter is a windowed sinc, not the three point kernel this started
     * with. Three taps give only about 6 dB at the new Nyquist, so content just
     * above it survives at roughly a third of its amplitude and folds into a
     * table that is supposed to be band limited, and the error compounds across
     * nine successive decimations.
     *
     * Worth being straight about what this bought: the measured end-to-end
     * alias floor did NOT change, staying at about -31 dB across the range with
     * either kernel. That floor is set by the Catmull-Rom read in readMip(),
     * not by the decimation, so a better decimation filter cannot lower it.
     * This is here because the tables really should be band limited when the
     * class claims they are, and the cost is paid once per frame at build time.
     * Lowering the floor further would mean a longer interpolator.
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
                double acc = 0.0;
                for (int t = 0; t < kKernelTaps; t++) {
                    const long long offset = (long long)t - kKernelHalf;
                    // Wrapped, because the table is a cycle. Clamping here would
                    // build the filter's own discontinuity into every level.
                    const std::size_t at =
                        (std::size_t)(((long long)centre + offset +
                                       (long long)sourceLength * 2) %
                                      (long long)sourceLength);
                    acc += kernel_[t] * source[at];
                }
                dest[buildCursor_] = (float)acc;
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
            //
            // The test is which SIDE was built, not whether the active side is
            // ready. Reading readiness got this wrong for the very first frame,
            // which builds into the active side because there is nothing to
            // fade from: readiness had just been set on that same side, so the
            // fade was armed with no incoming table. It then never advanced,
            // and since offerFrame refuses to disturb an in-flight fade, no
            // further frame was ever built. One frame in, silent forever after.
            if (buildSide_ == activeSide_) {
                crossfade_ = 1.f;
            } else {
                crossfade_ = 0.f;
            }
        }
    }

    /**
     * Half-band lowpass for 2:1 decimation: sinc at a quarter of the rate,
     * Blackman windowed, normalised to unity gain at DC.
     */
    void buildDecimationKernel() {
        double sum = 0.0;
        for (int t = 0; t < kKernelTaps; t++) {
            const double x = (double)(t - kKernelHalf);
            const double sinc = (std::fabs(x) < 1e-12)
                                    ? 0.5
                                    : std::sin(kPi * 0.5 * x) / (kPi * x);
            const double w = 0.42 - 0.5 * std::cos(2 * kPi * t / (kKernelTaps - 1)) +
                             0.08 * std::cos(4 * kPi * t / (kKernelTaps - 1));
            kernel_[t] = sinc * w;
            sum += kernel_[t];
        }
        for (int t = 0; t < kKernelTaps; t++) kernel_[t] /= sum;
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
    uint64_t targetCount_ = 0;
    std::size_t workLastCall_ = 0;
    mutable int lastMip_ = 0;

    double kernel_[kKernelTaps] = {0.0};
    float mips_[2][kMipStorage];
};

}  // namespace stems
}  // namespace WiggleRoom
