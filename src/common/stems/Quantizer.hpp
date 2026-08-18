#pragma once
/******************************************************************************
 * Quantizer - scale-snapping CV output for Stems
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable. That rules out
 * rack::dsp::ExponentialSlewLimiter, so the portamento is written out here; it
 * is a few lines and keeping the core framework-free is worth more than reusing
 * them.
 *
 * Snaps an incoming 1 V/octave signal to the nearest degree of the scale, in
 * whatever key the detector reports or the user has set by hand, and applies
 * portamento on the way.
 *
 * THREE THINGS THAT ARE EASY TO GET WRONG HERE:
 *
 *  1. The tie-break has to be consistent or the staircase is not monotonic.
 *     A semitone exactly between two scale degrees, which happens at every
 *     non-scale note in a seven-note scale, is equidistant from the degree
 *     below and the one above. Choosing "whichever was nearest in the last
 *     block" or rounding to even makes a slowly rising input step BACKWARDS at
 *     some boundaries, which is audible as a wrong note. This always takes the
 *     lower degree.
 *
 *  2. The arithmetic is done in semitones as integers and divided by twelve
 *     once, at the end, in double. That keeps the glide state clean over long
 *     runs. It is worth being clear that it does NOT widen the output
 *     precision: process() returns float, and float rounding at these
 *     magnitudes is about 1.2e-7 V, which swamps the 2.98e-8 V that float
 *     accumulation would have cost. The integer degrees are exactly twelve
 *     semitones apart; the cast to float is what loses the last digits, and
 *     nothing here can prevent that.
 *
 *  3. Glide has to be exactly zero at zero, and has to ARRIVE when it is not.
 *     A bare exponential does reach the target eventually, once the increment
 *     falls below an ULP, but "eventually" is far longer than the time the
 *     control is labelled with: at twice a one second glide it is still 4.5e-5
 *     short. The output is snapped once the remainder is below a hundredth of a
 *     cent, so the glide finishes when it says it does.
 *
 * Real-time contract: process() allocates nothing and takes no locks.
 ******************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace WiggleRoom {
namespace stems {

class Quantizer {
public:
    /**
     * Scales offered by the manual override.
     *
     * The detector only distinguishes major from minor, so auto mode uses the
     * first two. The rest exist because the manual override would be pointless
     * if it could only say what the detector already says.
     */
    enum class Scale {
        Chromatic = 0,
        Major,
        NaturalMinor,
        HarmonicMinor,
        MelodicMinor,
        Dorian,
        Phrygian,
        Lydian,
        Mixolydian,
        Locrian,
        PentatonicMajor,
        PentatonicMinor,
        Blues,
        WholeTone,
        Count
    };

    static constexpr int kNumScales = static_cast<int>(Scale::Count);

    explicit Quantizer(int sampleRate = 48000) { setSampleRate(sampleRate); }

    void setSampleRate(int sampleRate) {
        sampleRate_ = (sampleRate > 0) ? sampleRate : 48000;
        updateGlideCoefficient();
    }

    /** Portamento time in seconds. Zero is instantaneous. */
    void setGlideSeconds(float seconds) {
        if (!std::isfinite(seconds)) return;
        glideSeconds_ = std::min(std::max(seconds, 0.f), 10.f);
        updateGlideCoefficient();
    }

    /**
     * Use the manual root and scale instead of the detected key.
     *
     * Manual takes precedence unconditionally. A user who has reached for the
     * override has said what they want, and letting a detection quietly
     * override it makes the control feel broken.
     */
    void setManualOverride(bool enabled) { manual_ = enabled; }

    void setManualKey(int root, Scale scale) {
        manualRoot_ = wrap(root);
        const int s = static_cast<int>(scale);
        manualScale_ = (s >= 0 && s < kNumScales) ? scale : Scale::Chromatic;
    }

    /** The key the detector currently reports. Ignored while overridden. */
    void setDetectedKey(int root, Scale scale) {
        detectedRoot_ = wrap(root);
        const int s = static_cast<int>(scale);
        detectedScale_ = (s >= 0 && s < kNumScales) ? scale : Scale::Chromatic;
    }

    bool manualOverride() const { return manual_; }
    int activeRoot() const { return manual_ ? manualRoot_ : detectedRoot_; }
    Scale activeScale() const { return manual_ ? manualScale_ : detectedScale_; }

    /**
     * Quantise one sample and advance the glide.
     *
     * @param volts  1 V/octave input, 0 V being C4.
     * @return       The glided output, also 1 V/octave.
     */
    float process(float volts) {
        if (std::isfinite(volts)) target_ = quantise(volts);

        if (glideCoefficient_ <= 0.0) {
            // Zero glide means zero glide. See note 3 in the header.
            current_ = target_;
            return static_cast<float>(current_);
        }

        current_ += (target_ - current_) * glideCoefficient_;
        // Snap once the remaining distance is far below a cent, so the output
        // settles exactly on the degree rather than approaching it forever.
        if (std::fabs(target_ - current_) < kSettleVolts) current_ = target_;
        return static_cast<float>(current_);
    }

    /** The quantised destination, before glide. */
    float target() const { return static_cast<float>(target_); }

    /** Jump to the target. For patch load, where gliding in would be wrong. */
    void snapToTarget() { current_ = target_; }

    void reset() {
        target_ = 0.0;
        current_ = 0.0;
    }

    /** Semitone offsets of @p scale from its root, as a twelve bit mask. */
    static uint16_t scaleMask(Scale scale) {
        switch (scale) {
            case Scale::Major:           return mask({0, 2, 4, 5, 7, 9, 11});
            case Scale::NaturalMinor:    return mask({0, 2, 3, 5, 7, 8, 10});
            case Scale::HarmonicMinor:   return mask({0, 2, 3, 5, 7, 8, 11});
            case Scale::MelodicMinor:    return mask({0, 2, 3, 5, 7, 9, 11});
            case Scale::Dorian:          return mask({0, 2, 3, 5, 7, 9, 10});
            case Scale::Phrygian:        return mask({0, 1, 3, 5, 7, 8, 10});
            case Scale::Lydian:          return mask({0, 2, 4, 6, 7, 9, 11});
            case Scale::Mixolydian:      return mask({0, 2, 4, 5, 7, 9, 10});
            case Scale::Locrian:         return mask({0, 1, 3, 5, 6, 8, 10});
            case Scale::PentatonicMajor: return mask({0, 2, 4, 7, 9});
            case Scale::PentatonicMinor: return mask({0, 3, 5, 7, 10});
            case Scale::Blues:           return mask({0, 3, 5, 6, 7, 10});
            case Scale::WholeTone:       return mask({0, 2, 4, 6, 8, 10});
            case Scale::Chromatic:
            default:                     return 0x0FFF;
        }
    }

    static const char* scaleName(Scale scale) {
        switch (scale) {
            case Scale::Major:           return "Major";
            case Scale::NaturalMinor:    return "Natural Minor";
            case Scale::HarmonicMinor:   return "Harmonic Minor";
            case Scale::MelodicMinor:    return "Melodic Minor";
            case Scale::Dorian:          return "Dorian";
            case Scale::Phrygian:        return "Phrygian";
            case Scale::Lydian:          return "Lydian";
            case Scale::Mixolydian:      return "Mixolydian";
            case Scale::Locrian:         return "Locrian";
            case Scale::PentatonicMajor: return "Pentatonic Major";
            case Scale::PentatonicMinor: return "Pentatonic Minor";
            case Scale::Blues:           return "Blues";
            case Scale::WholeTone:       return "Whole Tone";
            case Scale::Chromatic:
            default:                     return "Chromatic";
        }
    }

private:
    /** Nearest scale degree to @p volts, in volts. */
    double quantise(double volts) const {
        const uint16_t bits = scaleMask(activeScale());
        // An empty mask would leave the search with no candidate at all. Not
        // reachable through the public API, but the fallback is one line.
        if (bits == 0) return volts;

        const double semitones = volts * 12.0;
        const int root = activeRoot();

        // Search the octave the input falls in and one either side, which is
        // more than enough: the largest gap in any scale here is three
        // semitones. Working in absolute semitones rather than pitch classes is
        // what keeps the result monotonic across an octave boundary.
        const long long centre = static_cast<long long>(std::floor(semitones / 12.0));
        long long best = 0;
        double bestDistance = 1e18;
        bool found = false;

        for (long long octave = centre - 1; octave <= centre + 1; octave++) {
            for (int degree = 0; degree < 12; degree++) {
                if (!(bits & (1u << degree))) continue;
                const long long candidate = octave * 12 + root + degree;
                const double distance = std::fabs(static_cast<double>(candidate) - semitones);
                // Strictly less than, so an exact tie keeps the candidate found
                // first. Candidates are generated in ascending order, so that is
                // always the LOWER of the two, which is what makes the staircase
                // monotonic. See note 1 in the header.
                if (!found || distance < bestDistance - 1e-12) {
                    bestDistance = distance;
                    best = candidate;
                    found = true;
                }
            }
        }
        // Divide by twelve exactly once. See note 2 in the header.
        return static_cast<double>(best) / 12.0;
    }

    void updateGlideCoefficient() {
        if (glideSeconds_ <= 0.f) {
            glideCoefficient_ = 0.0;
            return;
        }
        // One-pole approach. The glide time is taken as the time to arrive
        // rather than as a time constant, which is what a player expects from a
        // control labelled in seconds, so the constant is a fifth of it and the
        // output is within a percent of the target when the time is up.
        const double tau = static_cast<double>(glideSeconds_) / 5.0;
        glideCoefficient_ = 1.0 - std::exp(-1.0 / (tau * sampleRate_));
    }

    static uint16_t mask(std::initializer_list<int> degrees) {
        uint16_t bits = 0;
        for (int d : degrees) bits |= static_cast<uint16_t>(1u << d);
        return bits;
    }

    static int wrap(int i) {
        const int m = i % 12;
        return (m < 0) ? (m + 12) : m;
    }

    // A hundredth of a cent, far below anything audible or measurable.
    static constexpr double kSettleVolts = 1.0 / 12.0 / 100.0 / 100.0;

    int sampleRate_ = 48000;
    float glideSeconds_ = 0.f;
    double glideCoefficient_ = 0.0;

    bool manual_ = false;
    int manualRoot_ = 0;
    Scale manualScale_ = Scale::Major;
    int detectedRoot_ = 0;
    Scale detectedScale_ = Scale::Major;

    double target_ = 0.0;
    double current_ = 0.0;
};

}  // namespace stems
}  // namespace WiggleRoom
