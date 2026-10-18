#pragma once
/******************************************************************************
 * STEP ADDRESSER
 * Decides which step a Mutagen lane is on.
 *
 * No VCV Rack dependencies - fully testable standalone.
 *
 * The position only ever moves on a clock tick. When an address CV is
 * patched it selects the step outright; otherwise the position advances by
 * one and wraps. Either way, nothing moves between ticks, so scrubbing the
 * address CV with a smooth source still gives stepped, in-time output.
 ******************************************************************************/

namespace WiggleRoom {

struct StepAddresser {
    // -1 means no step has played yet, so a fresh lane sits at the start
    // rather than reporting the last step of the pattern.
    int position = -1;

    void reset() { position = -1; }

    // `cv01` is the address input normalised to [0, 1]; it is ignored unless
    // `addressed` is true. Returns the step now selected.
    int tick(int steps, bool addressed, float cv01) {
        if (steps <= 0) {
            position = 0;
            return position;
        }

        if (addressed) {
            int idx = static_cast<int>(cv01 * static_cast<float>(steps));
            // A full-scale CV would land one past the end.
            position = clampInt(idx, 0, steps - 1);
        } else if (position < 0) {
            position = 0;
        } else {
            position = (position + 1) % steps;
        }
        return position;
    }

    // The step to read and display: step 0 until the first tick.
    int current(int steps) const {
        if (steps <= 0) return 0;
        if (position < 0) return 0;
        return clampInt(position, 0, steps - 1);
    }

    bool hasPlayed() const { return position >= 0; }

    // Follow an address broadcast down the expander chain. A slave with a
    // different length wraps into its own pattern, so equal lengths run in
    // lockstep and unequal ones phase against each other.
    void followBroadcast(int broadcastStep, int steps) {
        if (steps <= 0 || broadcastStep < 0) return;
        int idx = broadcastStep % steps;
        if (idx < 0) idx += steps;
        position = idx;
    }

private:
    static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
};

} // namespace WiggleRoom
