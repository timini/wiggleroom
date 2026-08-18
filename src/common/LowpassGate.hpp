#pragma once
/******************************************************************************
 * LowpassGate - vactrol-modelled lowpass gate
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable, and it lives
 * outside src/common/stems because two modules use it. specs/Stems.md and
 * specs/TheLantern.md both call for one, and the epic is explicit that they
 * share an implementation rather than each growing their own.
 *
 * Parker and D'Angelo, "A Digital Model of the Buchla Lowpass-Gate", DAFx-13.
 * Background and the circuit analysis are in docs/specs/buchla-259-292.md.
 *
 * WHAT MAKES IT A LOWPASS GATE RATHER THAN A VCA. The vactrol, not the filter,
 * carries the character. Its conductance rises in about 12 ms and falls over
 * hundreds, and that roughly 20:1 asymmetry is the plucked quality. The fall is
 * also level dependent: the cell responds faster while it is still bright and
 * drags as it darkens, which is what gives the long tail rather than a clean
 * exponential.
 *
 * The other half is that brightness and level fall TOGETHER. A VCA closing
 * leaves the timbre alone, so a decaying note keeps its harmonics all the way
 * down and sounds synthetic. Here the same conductance sets both the gain and
 * the cutoff, so the note dulls as it fades, which is what a struck object does.
 *
 * Real-time contract: process() allocates nothing and takes no locks.
 ******************************************************************************/

#include <algorithm>
#include <cmath>

namespace WiggleRoom {

class LowpassGate {
public:
    explicit LowpassGate(int sampleRate = 48000) { setSampleRate(sampleRate); }

    void setSampleRate(int sampleRate) {
        sampleRate_ = (sampleRate > 0) ? sampleRate : 48000;
        updateCoefficients();
    }

    /**
     * Scales both vactrol time constants together, 0.25 to 4.
     *
     * TheLantern's `response` control moves the whole cell rather than just its
     * decay, so an attack fixed at 12 ms leaves half of that control with
     * nothing to do. At the extremes of its documented 0.5 to 2 range this puts
     * the attack between about 6 and 24 ms.
     */
    void setResponseScale(float scale) {
        if (!std::isfinite(scale)) return;
        response_ = std::min(std::max(scale, 0.25f), 4.f);
        updateCoefficients();
    }

    float responseScale() const { return response_; }

    /** Fall time. The vactrol's own is about 250 ms; the control widens it. */
    void setDecaySeconds(float seconds) {
        if (!std::isfinite(seconds)) return;
        decaySeconds_ = std::min(std::max(seconds, 0.02f), 4.f);
        updateCoefficients();
    }

    /**
     * 0 is VCA, 1 is lowpass, and anything between is the Both position.
     *
     * Continuous rather than a three-way switch, because the spec asks for a
     * continuum. The circuit switches two components to move between modes; the
     * audible consequence is how much of the conductance goes to the gain and
     * how much to the cutoff, which is what is interpolated here.
     */
    void setColour(float colour) {
        if (!std::isfinite(colour)) return;
        colour_ = std::min(std::max(colour, 0.f), 1.f);
    }

    /** Floor the gate never closes below, so it can be left partly open. */
    void setRestingLevel(float level) {
        if (!std::isfinite(level)) return;
        resting_ = std::min(std::max(level, 0.f), 1.f);
    }

    /** Ping the gate fully open, then let it decay on its own. */
    void trigger() {
        target_ = 1.f;
        pinged_ = true;
    }

    /** Hold the gate at @p level until released. */
    void setGate(float level) {
        if (!std::isfinite(level)) return;
        target_ = std::min(std::max(level, 0.f), 1.f);
        // A held gate is NOT a ping, even at exactly 1. Treating every full
        // scale target as a ping meant setGate(1) began decaying the moment the
        // attack arrived, so a sustained gate fell to 0.076 within a second.
        // Full scale gate signals commonly clamp to exactly 1, so that is the
        // ordinary case rather than an edge one.
        pinged_ = false;
    }

    void release() {
        target_ = 0.f;
        pinged_ = false;
    }

    /**
     * One sample.
     *
     * @param in  Audio to pass.
     * @return    Gated and filtered audio.
     */
    float process(float in) {
        if (!std::isfinite(in)) in = 0.f;
        // Clamped, not merely checked for finiteness. Two consecutive legal
        // finite samples of opposite extreme magnitude, FLT_MAX then -FLT_MAX,
        // overflow the subtraction in the filter below, and once a stage holds
        // an infinity every ordinary sample after it returns NaN for good.
        // Ten is far outside anything an audio signal carries in Rack.
        in = std::min(std::max(in, -kMaxInput), kMaxInput);

        const float aim = std::max(target_, resting_);

        if (aim > conductance_) {
            conductance_ += (aim - conductance_) * riseCoefficient_;
        } else {
            // Level dependent, which is the whole point. A fixed coefficient
            // gives a clean exponential; the cell actually drags as it darkens,
            // and that lengthening tail is what a lowpass gate sounds like.
            const float slowdown = kTailFloor + (1.f - kTailFloor) * conductance_;
            conductance_ += (aim - conductance_) * fallCoefficient_ * slowdown;
        }
        conductance_ = std::min(std::max(conductance_, 0.f), 1.f);

        // A trigger is a ping, not a hold: once the rise has essentially
        // arrived, stop aiming high so the fall can begin on its own. Only a
        // trigger, though; a gate held open stays open until released.
        if (pinged_ && conductance_ > 0.99f) {
            target_ = 0.f;
            pinged_ = false;
        }

        // Three positions on a continuum, not a blend of two.
        //
        //   0.0  VCA      gain follows the cell, the filter stays open
        //   0.5  Both     gain AND cutoff follow it, which is the classic sound
        //   1.0  Lowpass  cutoff follows it, the gain stays up
        //
        // Interpolating straight from VCA to Lowpass would make the midpoint
        // half of each, which is quieter and duller than either end and is not
        // what the Both position does.
        const float open = openness();
        const float gain = vcaGain();

        const float cutoff = cutoffFor(open);
        stage1_ += (in - stage1_) * onePoleCoefficient(cutoff);
        // The second pole sits an octave up, so the pair is closer to 6 dB per
        // octave than 12, as the circuit's non-coincident poles are.
        stage2_ += (stage1_ - stage2_) *
                   onePoleCoefficient(std::min(cutoff * 2.f, sampleRate_ * 0.45f));

        return stage2_ * gain;
    }

    /** Vactrol conductance, 0 to 1. Diagnostics and display. */
    float envelope() const { return conductance_; }

    /** Current filter cutoff in Hz. Diagnostics. */
    float cutoffHz() const { return cutoffFor(openness()); }

    /** Current VCA gain. Diagnostics. */
    float gain() const { return vcaGain(); }

    void reset() {
        conductance_ = 0.f;
        target_ = 0.f;
        stage1_ = 0.f;
        stage2_ = 0.f;
    }

private:
    /** How open the filter is, 0 to 1. See the colour note in process(). */
    float openness() const {
        if (colour_ <= 0.5f) {
            const float t = colour_ * 2.f;          // 0 at VCA, 1 at Both
            return 1.f + (conductance_ - 1.f) * t;
        }
        return conductance_;                         // Both through Lowpass
    }

    /** VCA gain, 0 to 1. See the colour note in process(). */
    float vcaGain() const {
        if (colour_ <= 0.5f) return conductance_;    // VCA through Both
        const float t = (colour_ - 0.5f) * 2.f;      // 0 at Both, 1 at Lowpass
        return conductance_ + (1.f - conductance_) * t;
    }

    float cutoffFor(float open) const {
        // Exponential in the conductance, because pitch and brightness are both
        // heard logarithmically. A linear sweep spends nearly all of its travel
        // in the top octave and sounds like nothing is happening until the very
        // end of the decay.
        const float low = 30.f;
        const float high = std::min(18000.f, sampleRate_ * 0.45f);
        const float t = std::min(std::max(open, 0.f), 1.f);
        return low * std::pow(high / low, t);
    }

    float onePoleCoefficient(float hz) const {
        const float x = std::exp(-2.f * kPi * hz / sampleRate_);
        return 1.f - x;
    }

    void updateCoefficients() {
        // Both constants are calibrated to the MEASURED response, not used as
        // raw time constants, because that is what the controls claim.
        //
        // A single pole takes 2.2 time constants to go from 10 to 90 per cent,
        // so a 12 ms rise needs a 5.45 ms constant; using 12 directly gave
        // 26.4 ms. The fall is stretched by the level-dependent slowdown above,
        // so its constant is scaled back to land on the requested time.
        const float riseTau = kRiseSeconds * response_ / 2.2f;
        riseCoefficient_ = 1.f - std::exp(-1.f / (riseTau * sampleRate_));
        const float fallTau = decaySeconds_ * response_ * kFallCalibration;
        fallCoefficient_ = 1.f - std::exp(-1.f / (fallTau * sampleRate_));
    }

    static constexpr float kPi = 3.14159265358979323846f;
    /** VTL5C3/2 rise. The 20:1 asymmetry against the fall is the character. */
    static constexpr float kRiseSeconds = 0.012f;
    /** How far the fall slows as the cell darkens. */
    static constexpr float kTailFloor = 0.25f;
    /** Compensates the tail slowdown so decaySeconds means what it says. */
    static constexpr float kFallCalibration = 0.70f;
    /** Far outside anything an audio signal carries in Rack. */
    static constexpr float kMaxInput = 10.f;

    int sampleRate_ = 48000;
    float decaySeconds_ = 0.25f;
    float response_ = 1.f;
    float colour_ = 0.5f;
    float resting_ = 0.f;

    float conductance_ = 0.f;
    float target_ = 0.f;
    bool pinged_ = false;
    float riseCoefficient_ = 0.f;
    float fallCoefficient_ = 0.f;
    float stage1_ = 0.f;
    float stage2_ = 0.f;
};

}  // namespace WiggleRoom
