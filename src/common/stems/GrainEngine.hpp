#pragma once
/******************************************************************************
 * GrainEngine - granular texturiser for Stems
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable.
 *
 * Schedules short overlapping reads of a source buffer, each with its own
 * position, rate, envelope and pan, and sums them into a stereo pair.
 *
 * THE POOL IS FIXED AND PREALLOCATED, which is the whole design constraint. A
 * granulator's cost is the number of grains alive at once, and that follows
 * density multiplied by size: at the top of both controls, 100 Hz against half
 * a second, the naive answer is fifty concurrent grains and the CPU cost rises
 * with the square of the controls. A fixed pool caps the worst case at
 * something that can be measured once and relied on, and a new grain that finds
 * no free slot is simply not started. Dropping a grain is inaudible in a cloud;
 * an audio thread that misses its deadline is not.
 *
 * Envelopes are raised cosine and always complete. A grain cut off before its
 * envelope closes is a click, and at a hundred a second that is a buzz at the
 * density rate rather than an occasional tick.
 *
 * Real-time contract: process() allocates nothing and takes no locks.
 ******************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace WiggleRoom {
namespace stems {

class GrainEngine {
public:
    /**
     * Concurrent grains.
     *
     * Sized from the worst case the controls allow: 100 Hz of density against
     * 500 ms grains is fifty overlapping, and the jitter can stretch a grain
     * beyond its nominal size, so there is headroom above that.
     */
    static constexpr int kMaxGrains = 64;

    struct Frame {
        float left = 0.f;
        float right = 0.f;
    };

    explicit GrainEngine(int sampleRate = 48000) {
        setSampleRate(sampleRate);
        rng_ = 0x9E3779B9u;
    }

    void setSampleRate(int sampleRate) {
        const int next = (sampleRate > 0) ? sampleRate : 48000;
        if (next != sampleRate_) {
            // Live grains carry an increment computed from the OLD rate, so
            // leaving them alone makes a 48 to 96 kHz change finish every
            // remaining envelope in half the seconds it was given, and the
            // reverse doubles them and holds pool slots for twice as long.
            // Durations are configured in seconds, so they have to survive.
            const double ratio = (double)sampleRate_ / (double)next;
            for (int i = 0; i < kMaxGrains; i++) {
                if (!grains_[i].active) continue;
                grains_[i].phaseIncrement *= ratio;
                grains_[i].fade *= ratio;
            }
        }
        sampleRate_ = next;
    }

    /** Grain duration in seconds. */
    void setSizeSeconds(float seconds) {
        if (!std::isfinite(seconds)) return;
        sizeSeconds_ = std::min(std::max(seconds, 0.001f), 0.5f);
    }

    /** Grains started per second. */
    void setDensityHz(float hz) {
        if (!std::isfinite(hz)) return;
        densityHz_ = std::min(std::max(hz, 0.1f), 100.f);
    }

    /** Transposition in semitones. */
    void setPitchSemitones(float semitones) {
        if (!std::isfinite(semitones)) return;
        pitch_ = std::min(std::max(semitones, -24.f), 24.f);
    }

    /** Window shape and randomisation, 0 to 1. */
    void setTexture(float texture) {
        if (!std::isfinite(texture)) return;
        texture_ = std::min(std::max(texture, 0.f), 1.f);
    }

    /** Stereo dispersal, 0 to 1. */
    void setSpread(float spread) {
        if (!std::isfinite(spread)) return;
        spread_ = std::min(std::max(spread, 0.f), 1.f);
    }

    /** Where in the source new grains are taken from, in frames. */
    void setReadPosition(double frames) {
        if (!std::isfinite(frames)) return;
        readPosition_ = frames;
    }

    /**
     * One sample.
     *
     * @param source  Buffer grains read from. Must outlive the call.
     * @param length  Length of @p source in frames.
     */
    Frame process(const float* source, std::size_t length) {
        Frame out;
        if (!source || length < 2) return out;

        // Schedule. The accumulator is in grains rather than samples so a
        // density change takes effect immediately instead of after whatever
        // countdown was already running.
        scheduleAccumulator_ += densityHz_ / sampleRate_;
        while (scheduleAccumulator_ >= 1.0) {
            scheduleAccumulator_ -= 1.0;
            startGrain(length);
        }

        for (int i = 0; i < kMaxGrains; i++) {
            Grain& g = grains_[i];
            if (!g.active) continue;

            // Wrapped every sample, not only at the start. A grain that begins
            // near the end of the buffer, or reaches it quickly at positive
            // pitch, otherwise falls off the end and reads zero for the rest of
            // its life: a step at full envelope gain followed by silence, which
            // is exactly the click the completion guarantee is supposed to
            // prevent.
            const double span = (double)(length - 1);
            g.position = std::fmod(g.position, span);
            if (g.position < 0.0) g.position += span;

            const double position = g.position;
            float sample = 0.f;
            if (position >= 0.0 && position < span) {
                const std::size_t i0 = (std::size_t)position;
                const double frac = position - (double)i0;
                const float a = source[i0];
                const float b = source[i0 + 1];
                sample = (float)(a + (b - a) * frac);
            }
            if (!std::isfinite(sample)) sample = 0.f;

            const float envelope = envelopeAt(g.phase, g.shape, g.fade);
            const float value = sample * envelope * g.amplitude;
            out.left += value * g.gainLeft;
            out.right += value * g.gainRight;

            g.position += g.rate;
            g.phase += g.phaseIncrement;
            // The envelope ALWAYS completes. Ending a grain on anything other
            // than its own envelope reaching zero is a click, and at a hundred
            // grains a second that is a buzz at the density rate.
            if (g.phase >= 1.0) {
                g.active = false;
                activeCount_--;
            }
        }

        out.left = softLimit(out.left);
        out.right = softLimit(out.right);
        if (!std::isfinite(out.left)) out.left = 0.f;
        if (!std::isfinite(out.right)) out.right = 0.f;
        return out;
    }

    /** Grains currently sounding. */
    int activeGrains() const { return activeCount_; }

    /** Grains that could not start because the pool was full. Diagnostics. */
    uint64_t debugDropped() const { return dropped_; }

    /** Grains started. Diagnostics. */
    uint64_t debugStarted() const { return started_; }

    void reset() {
        for (int i = 0; i < kMaxGrains; i++) grains_[i].active = false;
        activeCount_ = 0;
        scheduleAccumulator_ = 0.0;
    }

private:
    struct Grain {
        bool active = false;
        double position = 0.0;
        double rate = 1.0;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        float amplitude = 1.f;
        float gainLeft = 0.707f;
        float gainRight = 0.707f;
        float shape = 0.f;
        double fade = 0.0;
    };

    void startGrain(std::size_t length) {
        int slot = -1;
        for (int i = 0; i < kMaxGrains; i++) {
            if (!grains_[i].active) { slot = i; break; }
        }
        if (slot < 0) {
            // No free slot, so this grain simply does not happen. Stealing the
            // oldest would cut its envelope short, which is a click; in a cloud
            // a missing grain is inaudible.
            dropped_++;
            return;
        }

        Grain& g = grains_[slot];
        g.active = true;
        started_++;
        activeCount_++;

        // Texture is jitter: position, size and pan all scatter with it.
        const double jitterRange = (double)texture_;
        const double sizeJitter = 1.0 + jitterRange * (nextBipolar() * 0.5);
        const double seconds = std::max(0.0005, (double)sizeSeconds_ * sizeJitter);
        const double samples = seconds * sampleRate_;
        g.phase = 0.0;
        g.phaseIncrement = 1.0 / std::max(1.0, samples);
        // At least this many samples of fade at each end, whatever the texture.
        g.fade = std::min(0.5, kMinFadeSamples * g.phaseIncrement);

        const double positionJitter = jitterRange * nextBipolar() * (double)sampleRate_ * 0.05;
        double start = readPosition_ + positionJitter;
        // Wrapped into the buffer rather than clamped, so jitter near either end
        // scatters into the material instead of piling every stray grain onto
        // the first or last sample.
        const double span = (double)(length - 1);
        start = std::fmod(start, span);
        if (start < 0.0) start += span;
        g.position = start;

        g.rate = std::pow(2.0, (double)pitch_ / 12.0);

        // Texture also opens the window from a raised cosine toward a flatter
        // shape, which is what makes a cloud go from smooth to grainy.
        g.shape = texture_;

        const float pan = 0.5f + spread_ * (float)nextBipolar() * 0.5f;
        const float clamped = std::min(std::max(pan, 0.f), 1.f);
        // Constant power, so spread changes the image without changing the
        // level. A linear pan law dips 3 dB in the centre, which reads as the
        // cloud getting quieter as it widens.
        g.gainLeft = std::cos(clamped * kHalfPi);
        g.gainRight = std::sin(clamped * kHalfPi);

        // Density compensation. Grains sum, so without it the output rises with
        // the overlap: measured peaks of 0.90, 2.98 and 4.90 across the range of
        // the two controls, which is 17 dB of level change from controls that
        // are supposed to change texture.
        //
        // Incoherent sources sum as the square root of their number, which is
        // the ordinary case: any texture above zero scatters the read positions
        // far enough to decorrelate them.
        //
        // The exception is texture exactly 0, where every grain starts at the
        // same position and on sustained or periodic material they are
        // near-identical and sum LINEARLY: the peak climbed from 0.71 at 1 Hz
        // to 2.5 at 100 Hz. That is handled by the soft limit in process()
        // rather than by a stronger exponent here. Making the exponent depend
        // on texture was tried and is worse: it over-attenuates the ordinary
        // incoherent case, where the square root is already correct, and it
        // would be fitting the curve to whichever test signal happened to be
        // used.
        const double overlap = std::max(1.0, (double)densityHz_ * (double)sizeSeconds_);
        g.amplitude = (float)(1.0 / std::sqrt(overlap));
    }

    /**
     * Raised cosine, flattening toward a plateau as texture rises.
     *
     * Always zero at both ends whatever the shape, because that is what stops
     * the grain boundary being a click.
     */
    static float envelopeAt(double phase, float shape, double fade) {
        const double p = std::min(std::max(phase, 0.0), 1.0);
        const double hann = 0.5 * (1.0 - std::cos(2 * kPi * p));
        if (shape <= 0.f) return (float)hann;
        // A plateau with cosine ends: the fraction spent fading shrinks as the
        // shape opens, but never below `fade`, which is a minimum expressed in
        // SAMPLES rather than as a fraction of the grain.
        //
        // A fraction alone is not enough at the short end. Jitter can cut a
        // grain to half a millisecond, which is 24 samples, and the last phase
        // rendered is one increment short of 1; with the flattened texture-1
        // window that last sample still had 0.37 of full gain, so the grain
        // ended on a step rather than on silence.
        const double edge = std::max(fade, 0.5 * (1.0 - 0.8 * (double)shape));
        double gain;
        if (p < edge) {
            gain = 0.5 * (1.0 - std::cos(kPi * p / edge));
        } else if (p > 1.0 - edge) {
            gain = 0.5 * (1.0 - std::cos(kPi * (1.0 - p) / edge));
        } else {
            gain = 1.0;
        }
        return (float)(hann + (gain - hann) * (double)shape);
    }

    /**
     * Transparent below the threshold, saturating above it.
     *
     * Catches the coherent worst case: at texture 0 every grain reads the same
     * position, so on periodic material they sum linearly rather than as the
     * square root and the peak reaches 2.5, which clips a full-scale input.
     * Below the threshold this is exactly unity, so the ordinary incoherent
     * case passes through untouched and the compensation above is not fighting
     * a limiter that is always engaged.
     */
    static float softLimit(float x) {
        const float threshold = 0.8f;
        const float magnitude = std::fabs(x);
        if (magnitude <= threshold) return x;
        const float over = magnitude - threshold;
        const float limited = threshold + (1.f - threshold) * std::tanh(over / (1.f - threshold));
        return (x < 0.f) ? -limited : limited;
    }

    /** Cheap deterministic noise, so tests repeat exactly. */
    double nextBipolar() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return ((double)(rng_ >> 8) / (double)(1u << 24)) * 2.0 - 1.0;
    }

    static constexpr double kPi = 3.14159265358979323846;
    /** Minimum fade at each end of a grain, in samples. */
    static constexpr double kMinFadeSamples = 32.0;
    static constexpr float kHalfPi = 1.57079632679489661923f;

    int sampleRate_ = 48000;
    float sizeSeconds_ = 0.08f;
    float densityHz_ = 10.f;
    float pitch_ = 0.f;
    float texture_ = 0.3f;
    float spread_ = 0.5f;
    double readPosition_ = 0.0;

    double scheduleAccumulator_ = 0.0;
    int activeCount_ = 0;
    uint64_t dropped_ = 0;
    uint64_t started_ = 0;
    uint32_t rng_ = 1;

    Grain grains_[kMaxGrains];
};

}  // namespace stems
}  // namespace WiggleRoom
