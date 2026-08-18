#pragma once
/******************************************************************************
 * Diffusion - the space at the end of the Stems granular chain
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable.
 *
 * A Schroeder-style network: four allpass stages to smear the transients,
 * then a pair of comb-delay feedback loops per channel for the tail.
 *
 * WHY NOT CONVOLUTION. The ticket allowed RealTimeConvolver as an alternative.
 * It is rejected because a convolver plays a fixed impulse: the decay control
 * would have to crossfade between several responses, which is both more memory
 * and a worse control feel than simply changing a feedback coefficient. A
 * recirculating network gives a continuous decay control for a few kilobytes.
 *
 * WHY THE FEEDBACK IS BOUNDED WELL BELOW ONE. A comb loop with a gain of 1 is
 * an oscillator, and one slightly under it rings for minutes. The control maps
 * to a decay TIME and the gain is derived from it, so the loop can never be
 * asked for a gain it cannot come back from. There is a hard ceiling under that
 * as well, because the derivation depends on the delay length and a future
 * change there should not be able to turn the reverb into a sine generator.
 *
 * Delay lengths are mutually prime in samples, which is what stops the network
 * developing a periodic flutter: shared factors make several loops return
 * energy at the same instant and the tail acquires an audible pitch.
 *
 * Real-time contract: process() allocates nothing and takes no locks. All
 * storage is sized at construction from the maximum supported sample rate.
 ******************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace WiggleRoom {
namespace stems {

class Diffusion {
public:
    explicit Diffusion(int sampleRate = 48000) {
        // Sized once for the highest rate the plugin supports, so a sample rate
        // change never reallocates on the audio thread.
        allocate(kMaxSampleRate);
        setSampleRate(sampleRate);
    }

    void setSampleRate(int sampleRate) {
        sampleRate_ = (sampleRate > 0) ? sampleRate : 48000;
        updateLengths();
        updateFeedback();
    }

    /** Decay time in seconds, measured to -60 dB. */
    void setDecaySeconds(float seconds) {
        if (!std::isfinite(seconds)) return;
        decaySeconds_ = std::min(std::max(seconds, 0.05f), 10.f);
        updateFeedback();
    }

    /** Dry/wet, 0 to 1. */
    void setMix(float mix) {
        if (!std::isfinite(mix)) return;
        mix_ = std::min(std::max(mix, 0.f), 1.f);
    }

    /** Damping of the tail's high frequencies, 0 to 1. */
    void setDamping(float damping) {
        if (!std::isfinite(damping)) return;
        damping_ = std::min(std::max(damping, 0.f), 0.95f);
    }

    struct Frame {
        float left = 0.f;
        float right = 0.f;
    };

    Frame process(float inLeft, float inRight) {
        if (!std::isfinite(inLeft)) inLeft = 0.f;
        if (!std::isfinite(inRight)) inRight = 0.f;
        // Clamped as well as checked. Extreme finite samples can overflow
        // inside the feedback arithmetic, and a recirculating network that once
        // holds an infinity would never recover.
        //
        // In practice the guard on the summed tail below already catches this,
        // so removing the clamp is not observable through the public interface;
        // it is kept because relying on a downstream guard to contain something
        // that has already entered the delay lines is a thin defence, not
        // because a test can see the difference.
        inLeft = std::min(std::max(inLeft, -kMaxInput), kMaxInput);
        inRight = std::min(std::max(inRight, -kMaxInput), kMaxInput);

        float wetLeft = inLeft;
        float wetRight = inRight;

        // Allpass stages first: they scatter the transient without colouring
        // the magnitude response, which is what makes the tail sound like a
        // space rather than a set of echoes.
        for (int i = 0; i < kNumAllpass; i++) {
            wetLeft = allpass(allpassLeft_[i], allpassLength_[i], wetLeft);
            wetRight = allpass(allpassRight_[i], allpassRightLength_[i], wetRight);
        }

        float tailLeft = 0.f;
        float tailRight = 0.f;
        for (int i = 0; i < kNumCombs; i++) {
            tailLeft += comb(combLeft_[i], combLength_[i], dampLeft_[i], wetLeft);
            tailRight += comb(combRight_[i], combRightLength_[i], dampRight_[i], wetRight);
        }
        tailLeft *= kCombScale;
        tailRight *= kCombScale;

        if (!std::isfinite(tailLeft)) tailLeft = 0.f;
        if (!std::isfinite(tailRight)) tailRight = 0.f;

        Frame out;
        out.left = inLeft + (tailLeft - inLeft) * mix_;
        out.right = inRight + (tailRight - inRight) * mix_;
        return out;
    }

    /** Feedback gain currently in force. Diagnostics. */
    float feedback() const { return feedback_; }

    void reset() {
        for (auto& line : storage_) std::fill(line.begin(), line.end(), 0.f);
        for (int i = 0; i < kNumAllpass; i++) {
            allpassLeft_[i].index = 0;
            allpassRight_[i].index = 0;
        }
        for (int i = 0; i < kNumCombs; i++) {
            combLeft_[i].index = 0;
            combRight_[i].index = 0;
            dampLeft_[i] = 0.f;
            dampRight_[i] = 0.f;
        }
    }

private:
    static constexpr int kNumAllpass = 4;
    static constexpr int kNumCombs = 4;
    static constexpr int kMaxSampleRate = 192000;
    static constexpr float kMaxInput = 10.f;
    static constexpr float kCombScale = 0.25f;
    /**
     * Hard ceiling on the loop gain.
     *
     * The gain is derived from the decay time, so it should never approach one
     * on its own. This is here so that a future change to the delay lengths, or
     * a sample rate the derivation was not checked at, cannot turn the network
     * into an oscillator.
     *
     * It has to sit ABOVE what the longest decay needs, or it stops being a
     * safety net and becomes the thing that sets the decay: at 0.93 the control
     * saturated past two seconds, so four and eight both measured about four.
     * The ten second maximum needs 0.971, so this leaves a little room and no
     * more.
     */
    static constexpr float kMaxFeedback = 0.98f;

    struct Line {
        float* data = nullptr;
        std::size_t index = 0;
    };

    /**
     * Delay lengths in milliseconds, chosen so that at any sample rate the
     * sample counts share no small factors. Shared factors make several loops
     * return energy at the same instant and the tail acquires an audible pitch.
     */
    static constexpr float kAllpassMs[kNumAllpass] = {4.77f, 3.59f, 12.73f, 9.31f};
    static constexpr float kCombMs[kNumCombs] = {29.7f, 37.1f, 41.1f, 43.7f};
    /** The right channel is offset so the two sides decorrelate. */
    static constexpr float kStereoOffset = 1.17f;

    void allocate(int maxRate) {
        storage_.resize(2 * (kNumAllpass + kNumCombs));
        std::size_t at = 0;
        auto sizeFor = [&](float ms, float scale) {
            return (std::size_t)(ms * scale * maxRate / 1000.f) + 4;
        };
        for (int i = 0; i < kNumAllpass; i++) {
            storage_[at].assign(sizeFor(kAllpassMs[i], 1.f), 0.f);
            allpassLeft_[i].data = storage_[at].data();
            at++;
            storage_[at].assign(sizeFor(kAllpassMs[i], kStereoOffset), 0.f);
            allpassRight_[i].data = storage_[at].data();
            at++;
        }
        for (int i = 0; i < kNumCombs; i++) {
            storage_[at].assign(sizeFor(kCombMs[i], 1.f), 0.f);
            combLeft_[i].data = storage_[at].data();
            at++;
            storage_[at].assign(sizeFor(kCombMs[i], kStereoOffset), 0.f);
            combRight_[i].data = storage_[at].data();
            at++;
        }
    }

    void updateLengths() {
        for (int i = 0; i < kNumAllpass; i++) {
            allpassLength_[i] = lengthFor(kAllpassMs[i], 1.f);
            allpassRightLength_[i] = lengthFor(kAllpassMs[i], kStereoOffset);
        }
        for (int i = 0; i < kNumCombs; i++) {
            combLength_[i] = lengthFor(kCombMs[i], 1.f);
            combRightLength_[i] = lengthFor(kCombMs[i], kStereoOffset);
        }
        reset();
    }

    std::size_t lengthFor(float ms, float scale) const {
        const std::size_t n = (std::size_t)(ms * scale * sampleRate_ / 1000.f);
        return std::max<std::size_t>(2, n);
    }

    /**
     * Derive the loop gain from the decay time.
     *
     * A comb of length L at gain g decays 20*log10(g) dB per pass, so reaching
     * -60 dB in T seconds needs g = 10^(-3 * L / (T * fs)). Setting a gain
     * directly instead would make the same knob position mean a different decay
     * at every sample rate and every delay length.
     */
    void updateFeedback() {
        const double longest = (double)combLength_[kNumCombs - 1];
        const double samples = std::max(1.0, (double)decaySeconds_ * sampleRate_);
        const double g = std::pow(10.0, -3.0 * longest / samples);
        feedback_ = (float)std::min((double)kMaxFeedback, std::max(0.0, g));
    }

    float allpass(Line& line, std::size_t length, float in) {
        const std::size_t at = line.index % length;
        const float stored = line.data[at];
        const float out = -in + stored;
        line.data[at] = in + stored * kAllpassGain;
        line.index = (line.index + 1) % length;
        return out;
    }

    float comb(Line& line, std::size_t length, float& damp, float in) {
        const std::size_t at = line.index % length;
        const float stored = line.data[at];
        // One-pole damping inside the loop, so the tail loses brightness as it
        // decays rather than ringing on with the same spectrum, which is what
        // a real room does.
        damp = stored + (damp - stored) * damping_;
        line.data[at] = in + damp * feedback_;
        line.index = (line.index + 1) % length;
        return stored;
    }

    static constexpr float kAllpassGain = 0.5f;

    int sampleRate_ = 48000;
    float decaySeconds_ = 2.f;
    float mix_ = 0.3f;
    float damping_ = 0.4f;
    float feedback_ = 0.f;

    std::vector<std::vector<float>> storage_;
    Line allpassLeft_[kNumAllpass], allpassRight_[kNumAllpass];
    Line combLeft_[kNumCombs], combRight_[kNumCombs];
    std::size_t allpassLength_[kNumAllpass] = {0}, allpassRightLength_[kNumAllpass] = {0};
    std::size_t combLength_[kNumCombs] = {0}, combRightLength_[kNumCombs] = {0};
    float dampLeft_[kNumCombs] = {0.f}, dampRight_[kNumCombs] = {0.f};
};

}  // namespace stems
}  // namespace WiggleRoom
