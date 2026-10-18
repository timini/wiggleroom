#pragma once
/******************************************************************************
 * BANK STORE
 * Sixteen slots of step values for one Mutagen lane.
 *
 * No VCV Rack dependencies - fully testable standalone. JSON lives in the
 * module files, since jansson comes from Rack.
 *
 * Every unit in a chain owns its own store. The master broadcasts only the
 * slot number, so save and recall never write into a neighbour's params.
 * That is what stops this repeating EucBank, whose Load button does nothing
 * because there is no reverse write path in the expander protocol.
 ******************************************************************************/

#include <algorithm>

#include "StepSequence.hpp"

namespace WiggleRoom {

struct BankSlot {
    float values[StepSequence::MAX_STEPS] = {};
    int steps = StepSequence::DEFAULT_STEPS;
    bool occupied = false;
};

struct BankStore {
    static constexpr int NUM_BANKS = 16;

    BankSlot banks[NUM_BANKS];

    static bool validIndex(int bank) { return bank >= 0 && bank < NUM_BANKS; }

    void save(int bank, const StepSequence& seq) {
        if (!validIndex(bank)) return;
        BankSlot& slot = banks[bank];
        std::copy(seq.values, seq.values + StepSequence::MAX_STEPS, slot.values);
        slot.steps = seq.steps;
        slot.occupied = true;
    }

    // Returns false for an empty slot, leaving the sequence untouched, so
    // stepping through banks does not blank a lane that was never saved.
    bool load(int bank, StepSequence& seq) const {
        if (!validIndex(bank)) return false;
        const BankSlot& slot = banks[bank];
        if (!slot.occupied) return false;
        seq.pushUndo();
        std::copy(slot.values, slot.values + StepSequence::MAX_STEPS, seq.values);
        seq.setSteps(slot.steps);
        return true;
    }

    bool occupied(int bank) const {
        return validIndex(bank) && banks[bank].occupied;
    }

    void clear(int bank) {
        if (!validIndex(bank)) return;
        banks[bank] = BankSlot();
    }
};

} // namespace WiggleRoom
