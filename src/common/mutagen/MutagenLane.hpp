#pragma once
/******************************************************************************
 * MUTAGEN LANE
 * State and persistence shared by Mutagen and its MutagenX expander.
 *
 * Both modules are one lane: a sequence, a position, and sixteen banks.
 * Mutagen adds transport and bank controls on top; MutagenX takes those
 * from the chain. Keeping the shared part here means there is one copy to
 * fix, unlike the EucSeq family where the 4-channel originals drifted from
 * the templates.
 ******************************************************************************/

#include "rack.hpp"

#include "mutagen/BankStore.hpp"
#include "mutagen/MutagenMessage.hpp"
#include "mutagen/StepAddresser.hpp"
#include "mutagen/StepSequence.hpp"

using namespace rack;

namespace WiggleRoom {

struct MutagenLane : Module {
    StepSequence seq;
    StepAddresser addresser;
    BankStore banks;

    int currentBank = 0;
    bool bipolarOutput = false;  // right-click option; 0-10V by default

    // The step to read and draw, or -1 before the first tick so the screen
    // highlights nothing rather than falsely lighting a row.
    int displayStep() const {
        if (!addresser.hasPlayed()) return -1;
        return addresser.current(seq.steps);
    }

    float outputVoltage() const {
        int step = addresser.current(seq.steps);
        float v = seq.valueAt(step);
        return bipolarOutput ? (v * 10.f - 5.f) : (v * 10.f);
    }

    // Everything a lane owns, so both modules persist identically.
    json_t* laneToJson() {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "steps", json_integer(seq.steps));
        json_object_set_new(rootJ, "currentBank", json_integer(currentBank));
        json_object_set_new(rootJ, "bipolar", json_boolean(bipolarOutput));

        json_t* valuesJ = json_array();
        for (int s = 0; s < seq.steps; s++)
            json_array_append_new(valuesJ, json_real(seq.values[s]));
        json_object_set_new(rootJ, "values", valuesJ);

        json_t* banksJ = json_array();
        for (int b = 0; b < BankStore::NUM_BANKS; b++) {
            const BankSlot& slot = banks.banks[b];
            json_t* slotJ = json_object();
            json_object_set_new(slotJ, "occupied", json_boolean(slot.occupied));
            json_object_set_new(slotJ, "steps", json_integer(slot.steps));
            if (slot.occupied) {
                json_t* svJ = json_array();
                for (int s = 0; s < slot.steps; s++)
                    json_array_append_new(svJ, json_real(slot.values[s]));
                json_object_set_new(slotJ, "values", svJ);
            }
            json_array_append_new(banksJ, slotJ);
        }
        json_object_set_new(rootJ, "banks", banksJ);
        return rootJ;
    }

    void laneFromJson(json_t* rootJ) {
        if (!rootJ) return;

        if (json_t* j = json_object_get(rootJ, "steps"))
            seq.setSteps(static_cast<int>(json_integer_value(j)));
        if (json_t* j = json_object_get(rootJ, "currentBank"))
            currentBank = static_cast<int>(json_integer_value(j));
        if (json_t* j = json_object_get(rootJ, "bipolar"))
            bipolarOutput = json_boolean_value(j);

        if (json_t* valuesJ = json_object_get(rootJ, "values")) {
            if (json_is_array(valuesJ)) {
                int n = static_cast<int>(json_array_size(valuesJ));
                for (int s = 0; s < StepSequence::MAX_STEPS && s < n; s++)
                    seq.values[s] = static_cast<float>(json_real_value(json_array_get(valuesJ, s)));
            }
        }

        if (json_t* banksJ = json_object_get(rootJ, "banks")) {
            if (json_is_array(banksJ)) {
                int n = static_cast<int>(json_array_size(banksJ));
                for (int b = 0; b < BankStore::NUM_BANKS && b < n; b++) {
                    json_t* slotJ = json_array_get(banksJ, b);
                    if (!slotJ) continue;
                    BankSlot& slot = banks.banks[b];
                    if (json_t* j = json_object_get(slotJ, "occupied"))
                        slot.occupied = json_boolean_value(j);
                    if (json_t* j = json_object_get(slotJ, "steps"))
                        slot.steps = static_cast<int>(json_integer_value(j));
                    if (json_t* svJ = json_object_get(slotJ, "values")) {
                        if (json_is_array(svJ)) {
                            int sn = static_cast<int>(json_array_size(svJ));
                            for (int s = 0; s < StepSequence::MAX_STEPS && s < sn; s++)
                                slot.values[s] = static_cast<float>(json_real_value(json_array_get(svJ, s)));
                        }
                    }
                }
            }
        }

        // Loading a patch is not an edit the user should be able to undo into.
        seq.clearHistory();
    }

    // Act on a bank index arriving from the chain, or from this module's own
    // knob. Recall only fires when the slot actually changes.
    void applyBankIndex(int bank) {
        if (!BankStore::validIndex(bank) || bank == currentBank) return;
        currentBank = bank;
        banks.load(currentBank, seq);
    }
};

} // namespace WiggleRoom
