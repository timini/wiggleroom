#pragma once
/******************************************************************************
 * EUCLOGIC EXPANDER MESSAGE
 * Shared struct for passing state between EucSeq, LogicMangler, EucBank, EucMix
 * via VCV Rack's expander system.
 *
 * Chain: EucSeq -> LogicMangler -> EucBank -> EucMix (left to right)
 ******************************************************************************/

#include <cstdint>

namespace WiggleRoom {

struct EuclogicExpanderMessage {
    // EucSeq state
    bool gates[4];           // current gate states per channel (pre-logic)
    bool triggers[4];        // current trigger states
    float lfo[4];            // LFO values (0-10V)
    float cv[4];             // CV step values per channel
    int currentStep[4];      // current step index per channel
    int totalSteps[4];       // total steps per channel

    // Truth table state (populated by LogicMangler)
    uint8_t truthTableMapping[16];
    uint8_t truthTableLocks[16];
    bool postLogicGates[4];  // post-truth-table gate states
    float probB[4];          // post-logic probability values
    float colDensity[4];     // per-column density values

    // EucSeq params (for bank storage)
    int steps[4];
    int hits[4];
    int quant[4];
    float probA[4];
    bool retrigger[4];
    bool bipolar[4];
    float speed;
    float swing;

    bool valid;              // true when message is populated

    EuclogicExpanderMessage() { clear(); }

    void clear() {
        for (int i = 0; i < 4; i++) {
            gates[i] = false;
            triggers[i] = false;
            lfo[i] = 0.f;
            cv[i] = 0.f;
            currentStep[i] = 0;
            totalSteps[i] = 16;
            postLogicGates[i] = false;
            probB[i] = 1.f;
            colDensity[i] = 0.5f;
            steps[i] = 16;
            hits[i] = 8;
            quant[i] = 0;
            probA[i] = 1.f;
            retrigger[i] = true;
            bipolar[i] = false;
        }
        for (int i = 0; i < 16; i++) {
            truthTableMapping[i] = static_cast<uint8_t>(i) & 0x0F;
            truthTableLocks[i] = 0;
        }
        speed = 8.f;  // x1 index
        swing = 50.f;
        valid = false;
    }
};

} // namespace WiggleRoom
