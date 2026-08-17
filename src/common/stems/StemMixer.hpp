#pragma once
/******************************************************************************
 * StemMixer - four-channel stem playback for Stems
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable.
 *
 * Takes the published StemSet, a playhead position from Transport, and the
 * unseparated recording as a fallback, and produces the stereo loop bus plus
 * the single-stem tap that feeds the analyser and the wavetable extractor.
 *
 * Three things here are not obvious and each one comes from the spec:
 *
 *  1. STRAIGHT SUM, not equal power. The HPSS masks are disjoint by
 *     construction, so the four layers already add up to the source. An
 *     equal-power law would apply a 3 dB lift per pair and break scenario 14,
 *     which requires four unity faders to reconstruct the source within 0.5 dB.
 *     Equal power is the right choice for crossfading between alternatives; it
 *     is the wrong choice for recombining parts of a whole.
 *
 *  2. WHILE SEPARATING, only channel 1 carries the unseparated buffer. Routing
 *     it to all four at their default unity gain sums four identical copies for
 *     a 12 dB lift, and then drops abruptly when the real stems arrive.
 *
 *  3. THE TRANSITION IS CROSSFADED, and it is a crossfade between the fallback
 *     path and the stems path rather than between two stem sets. The previous
 *     set is reclaimed by the worker as soon as the audio thread lets go of it,
 *     so it cannot be held across a fade; the unseparated recording, by
 *     contrast, is owned by the module and always available.
 *
 * Real-time contract: process() allocates nothing and takes no locks.
 ******************************************************************************/

#include "SeparationWorker.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace WiggleRoom {
namespace stems {

class StemMixer {
public:
    static constexpr int kNumChannels = StemSet::kNumLayers;

    struct Frame {
        float left = 0.f;
        float right = 0.f;
    };

    explicit StemMixer(int sampleRate) { setSampleRate(sampleRate); }

    /**
     * Slew rates are held in seconds, so a sample-rate change does not alter
     * how long a fade takes. Storing them as per-sample increments would make
     * every fade twice as fast at 96 kHz.
     */
    void setSampleRate(int sampleRate) {
        sampleRate_ = (sampleRate > 0) ? sampleRate : 48000;
        gainStep_   = 1.0f / (kGainFadeSeconds * sampleRate_);
        sourceStep_ = 1.0f / (kSourceFadeSeconds * sampleRate_);
        tapStep_    = 1.0f / (kTapFadeSeconds * sampleRate_);
    }

    /** Fader for one channel, 0 to 1. */
    void setLevel(int channel, float level) {
        if (channel < 0 || channel >= kNumChannels) return;
        level_[channel] = std::min(std::max(level, 0.f), 1.f);
    }

    void setMute(int channel, bool muted) {
        if (channel < 0 || channel >= kNumChannels) return;
        mute_[channel] = muted;
    }

    /** Which stem feeds the analyser and the wavetable. */
    void setStemSelect(int channel) {
        const int clamped = std::min(std::max(channel, 0), kNumChannels - 1);
        if (clamped == tapTarget_) return;
        // Start a crossfade rather than switching outright. The tap is a live
        // audio signal, so a hard switch between two unrelated stems is a step
        // discontinuity, audible as a click through the wavetable voice.
        //
        // The fade runs from the VALUE last emitted, not from the index of the
        // outgoing stem. Fading between two indices reads better on paper and is
        // wrong the moment a second switch arrives mid-fade: the outgoing index
        // still names the original stem, so the fade restarts from that stem's
        // current sample rather than from the half-mixed value actually being
        // emitted, and the tap steps by up to the full difference between them.
        // Starting from the emitted value is continuous however often the
        // selection changes.
        tapFrom_ = tap_;
        tapTarget_ = clamped;
        tapFade_ = 0.f;
    }

    int stemSelect() const { return tapTarget_; }

    /**
     * Produce one frame of the loop bus.
     *
     * @param set          Published stems, or nullptr while separating.
     * @param position     Playhead in frames, from Transport.
     * @param fallbackLeft Unseparated recording at the same position.
     */
    Frame process(const StemSet* set, double position, float fallbackLeft,
                  float fallbackRight) {
        // Advance the fallback/stems crossfade. Its target is simply whether
        // stems exist, so the fade covers the arrival of the first set and any
        // return to the fallback after a failed separation.
        const float sourceTarget = (set != nullptr) ? 1.f : 0.f;
        sourceTargetSeen_ = sourceTarget;
        sourceMix_ = approach(sourceMix_, sourceTarget, sourceStep_);
        const float fallbackMix = 1.f - sourceMix_;

        float outLeft = 0.f, outRight = 0.f;

        for (int c = 0; c < kNumChannels; c++) {
            float stemLeft = stemLeft_[c], stemRight = stemRight_[c];
            if (set != nullptr) {
                readStem(*set, c, position, stemLeft, stemRight);
                stemLeft_[c] = stemLeft;
                stemRight_[c] = stemRight;
            }
            // When the set goes away the last samples are HELD for the duration
            // of the fade out. Zeroing them instead would drop the stems path in
            // one sample while the fallback was still fading in, so the output
            // would dip almost to silence at exactly the moment the module is
            // meant to degrade gracefully.

            // The unseparated recording goes to channel 1 alone. See note 2 in
            // the header. This is the ONLY place that routing is expressed, so
            // widening it here is what the fallback tests detect.
            const float fbLeft  = (c == 0) ? fallbackLeft : 0.f;
            const float fbRight = (c == 0) ? fallbackRight : 0.f;

            const float left  = stemLeft * sourceMix_ + fbLeft * fallbackMix;
            const float right = stemRight * sourceMix_ + fbRight * fallbackMix;

            // Level and mute share one slewed gain. Applying mute as a hard
            // multiply would click, and the fade has to survive a level change
            // made while muted, so the target is computed from both.
            const float target = mute_[c] ? 0.f : level_[c];
            gain_[c] = approach(gain_[c], target, gainStep_);

            outLeft  += left * gain_[c];
            outRight += right * gain_[c];
        }

        // The tap crossfades between the outgoing and incoming stem, and
        // separately between the stems and the fallback. While separation is
        // running it carries the unseparated recording: selecting any channel
        // but the first would otherwise starve the analyser and the wavetable
        // until stems arrived.
        tapFade_ = std::min(1.f, tapFade_ + tapStep_);
        const float incoming = stemLeft_[tapTarget_] * sourceMix_ + fallbackLeft * fallbackMix;
        tap_ = tapFrom_ + (incoming - tapFrom_) * tapFade_;
        if (tapFade_ >= 1.f) tapFrom_ = tap_;

        Frame out;
        out.left = outLeft;
        out.right = outRight;
        return out;
    }

    /** The selected stem, for the analyser and the wavetable extractor. */
    float tap() const { return tap_; }

    /** True once every slewed gain has reached its target. Diagnostics. */
    bool settled() const {
        if (std::fabs(sourceMix_ - sourceTargetSeen_) > 1e-6f) return false;
        for (int c = 0; c < kNumChannels; c++) {
            const float target = mute_[c] ? 0.f : level_[c];
            if (std::fabs(gain_[c] - target) > 1e-6f) return false;
        }
        return true;
    }

    /**
     * Jump every slewed value to its target.
     *
     * For patch load and sample-rate changes, where fading in from silence
     * would be wrong: the user has not changed anything, so there is nothing to
     * smooth over.
     */
    void snapToTargets(bool haveStems) {
        sourceMix_ = haveStems ? 1.f : 0.f;
        sourceTargetSeen_ = sourceMix_;
        for (int c = 0; c < kNumChannels; c++) {
            gain_[c] = mute_[c] ? 0.f : level_[c];
        }
        tapFade_ = 1.f;
        tapFrom_ = tap_;
    }

private:
    static float approach(float current, float target, float step) {
        if (current < target) return std::min(target, current + step);
        if (current > target) return std::max(target, current - step);
        return current;
    }

    /** Linear interpolation into one layer, clamped at the ends. */
    static void readStem(const StemSet& set, int layer, double position, float& left,
                         float& right) {
        left = right = 0.f;
        const auto& l = set.layer[layer].channel[0];
        const std::size_t n = l.size();
        // Validate while the value is still floating point. Casting a NaN or an
        // infinity to size_t is undefined, and NaN slips past a bare `< 0`
        // comparison, so the checks have to come first. The RingBuffer had
        // exactly this defect.
        if (n == 0 || !std::isfinite(position) || position < 0.0 ||
            position >= static_cast<double>(n)) {
            return;
        }
        const std::size_t i0 = static_cast<std::size_t>(position);
        const std::size_t i1 = std::min(i0 + 1, n - 1);
        const float frac = static_cast<float>(position - static_cast<double>(i0));

        const auto& r = (set.channels > 1) ? set.layer[layer].channel[1] : l;
        // A stereo set with a short or empty right channel would otherwise index
        // out of range; fall back to the left rather than trusting the flag.
        const bool rightUsable = (r.size() == n);

        left = l[i0] + (l[i1] - l[i0]) * frac;
        right = rightUsable ? (r[i0] + (r[i1] - r[i0]) * frac) : left;
    }

    // Long enough to remove a step, short enough not to smear a deliberate mute.
    static constexpr float kGainFadeSeconds = 0.010f;
    // The source transition covers a change of material, so it gets longer.
    static constexpr float kSourceFadeSeconds = 0.050f;
    static constexpr float kTapFadeSeconds = 0.020f;

    int sampleRate_ = 48000;
    float gainStep_ = 0.f;
    float sourceStep_ = 0.f;
    float tapStep_ = 0.f;

    float level_[kNumChannels] = {1.f, 1.f, 1.f, 1.f};
    bool mute_[kNumChannels] = {false, false, false, false};
    float gain_[kNumChannels] = {1.f, 1.f, 1.f, 1.f};

    float stemLeft_[kNumChannels] = {0.f, 0.f, 0.f, 0.f};
    float stemRight_[kNumChannels] = {0.f, 0.f, 0.f, 0.f};

    float sourceMix_ = 0.f;
    float sourceTargetSeen_ = 0.f;

    int tapTarget_ = kNumChannels - 1;
    float tapFrom_ = 0.f;
    float tapFade_ = 1.f;
    float tap_ = 0.f;
};

}  // namespace stems
}  // namespace WiggleRoom
