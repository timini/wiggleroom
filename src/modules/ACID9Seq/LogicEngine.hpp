#pragma once

#include <cmath>
#include <array>

namespace WiggleRoom {

/**
 * LogicEngine - Evaluates musical logic conditions for Gate/Slide/Accent
 *
 * Each output can be assigned any logic mode from the master list.
 * Modes evaluate based on pitch intervals, direction, and gear relationships.
 */
class LogicEngine {
public:
    // Master list of logic modes
    enum class Mode {
        ALWAYS = 0,   // Every tick
        NEVER,        // Never
        CHANGE,       // Pitch changed from previous
        SAME,         // Pitch same as previous
        RISE,         // Going up (any amount)
        DROP,         // Going down (any amount)
        LEAP,         // Interval > threshold
        STEP,         // Interval <= threshold
        PEAK,         // Higher than both neighbors
        VALLEY,       // Lower than both neighbors
        THIRD,        // Interval is 3 or 4 semitones
        FIFTH,        // Interval is 7 semitones
        OCTAVE,       // Interval is 12 semitones
        B_POS,        // Gear B offset > 0
        B_NEG,        // Gear B offset < 0
        B_ZERO,       // Gear B offset = 0
        AGREE,        // A and B moving same direction
        CLASH,        // A and B moving opposite directions
        NUM_MODES
    };

    static constexpr int NUM_MODES_INT = static_cast<int>(Mode::NUM_MODES);

    static const char* getModeName(Mode mode) {
        switch (mode) {
            case Mode::ALWAYS: return "Always";
            case Mode::NEVER:  return "Never";
            case Mode::CHANGE: return "Change";
            case Mode::SAME:   return "Same";
            case Mode::RISE:   return "Rise";
            case Mode::DROP:   return "Drop";
            case Mode::LEAP:   return "Leap";
            case Mode::STEP:   return "Step";
            case Mode::PEAK:   return "Peak";
            case Mode::VALLEY: return "Valley";
            case Mode::THIRD:  return "3rd";
            case Mode::FIFTH:  return "5th";
            case Mode::OCTAVE: return "Oct";
            case Mode::B_POS:  return "B+";
            case Mode::B_NEG:  return "B-";
            case Mode::B_ZERO: return "B=0";
            case Mode::AGREE:  return "Agree";
            case Mode::CLASH:  return "Clash";
            default: return "?";
        }
    }

    static const char* getModeShortName(int index) {
        return getModeName(static_cast<Mode>(index));
    }

    LogicEngine() {
        reset();
    }

    // Call each clock tick with current state
    void update(int currentPitch, int prevPitch, int prevPrevPitch,
                int gearAValue, int prevGearAValue,
                int gearBOffset, int prevGearBOffset) {
        currentPitch_ = currentPitch;
        prevPitch_ = prevPitch;
        prevPrevPitch_ = prevPrevPitch;
        gearAValue_ = gearAValue;
        prevGearAValue_ = prevGearAValue;
        gearBOffset_ = gearBOffset;
        prevGearBOffset_ = prevGearBOffset;

        // Pre-calculate common values
        interval_ = currentPitch_ - prevPitch_;
        absInterval_ = std::abs(interval_);
        gearADelta_ = gearAValue_ - prevGearAValue_;
        gearBDelta_ = gearBOffset_ - prevGearBOffset_;
    }

    // Evaluate a logic mode
    bool evaluate(Mode mode, int threshold) const {
        switch (mode) {
            case Mode::ALWAYS:
                return true;

            case Mode::NEVER:
                return false;

            case Mode::CHANGE:
                return currentPitch_ != prevPitch_;

            case Mode::SAME:
                return currentPitch_ == prevPitch_;

            case Mode::RISE:
                return interval_ > 0;

            case Mode::DROP:
                return interval_ < 0;

            case Mode::LEAP:
                return absInterval_ > threshold;

            case Mode::STEP:
                return absInterval_ <= threshold && absInterval_ > 0;

            case Mode::PEAK:
                // Current is higher than previous AND previous was rising
                return (currentPitch_ > prevPitch_) && (prevPitch_ > prevPrevPitch_) ? false :
                       (currentPitch_ < prevPitch_) && (prevPitch_ > prevPrevPitch_);

            case Mode::VALLEY:
                // Current is higher than previous AND previous was falling
                return (currentPitch_ > prevPitch_) && (prevPitch_ < prevPrevPitch_);

            case Mode::THIRD:
                return absInterval_ == 3 || absInterval_ == 4;

            case Mode::FIFTH:
                return absInterval_ == 7;

            case Mode::OCTAVE:
                return absInterval_ == 12;

            case Mode::B_POS:
                return gearBOffset_ > 0;

            case Mode::B_NEG:
                return gearBOffset_ < 0;

            case Mode::B_ZERO:
                return gearBOffset_ == 0;

            case Mode::AGREE:
                // Both A and B moving in same direction (or both static)
                return (gearADelta_ > 0 && gearBDelta_ > 0) ||
                       (gearADelta_ < 0 && gearBDelta_ < 0) ||
                       (gearADelta_ == 0 && gearBDelta_ == 0);

            case Mode::CLASH:
                // A and B moving in opposite directions
                return (gearADelta_ > 0 && gearBDelta_ < 0) ||
                       (gearADelta_ < 0 && gearBDelta_ > 0);

            default:
                return false;
        }
    }

    bool evaluate(int modeIndex, int threshold) const {
        if (modeIndex < 0 || modeIndex >= NUM_MODES_INT) return false;
        return evaluate(static_cast<Mode>(modeIndex), threshold);
    }

    // Apply probability (0.0 to 1.0)
    bool evaluateWithProb(Mode mode, int threshold, float probability, float randomValue) const {
        if (!evaluate(mode, threshold)) return false;
        return randomValue < probability;
    }

    bool evaluateWithProb(int modeIndex, int threshold, float probability, float randomValue) const {
        if (modeIndex < 0 || modeIndex >= NUM_MODES_INT) return false;
        return evaluateWithProb(static_cast<Mode>(modeIndex), threshold, probability, randomValue);
    }

    void reset() {
        currentPitch_ = 12;
        prevPitch_ = 12;
        prevPrevPitch_ = 12;
        gearAValue_ = 12;
        prevGearAValue_ = 12;
        gearBOffset_ = 0;
        prevGearBOffset_ = 0;
        interval_ = 0;
        absInterval_ = 0;
        gearADelta_ = 0;
        gearBDelta_ = 0;
    }

    // Getters for display
    int getInterval() const { return interval_; }
    int getAbsInterval() const { return absInterval_; }

private:
    int currentPitch_;
    int prevPitch_;
    int prevPrevPitch_;
    int gearAValue_;
    int prevGearAValue_;
    int gearBOffset_;
    int prevGearBOffset_;

    int interval_;
    int absInterval_;
    int gearADelta_;
    int gearBDelta_;
};

} // namespace WiggleRoom
