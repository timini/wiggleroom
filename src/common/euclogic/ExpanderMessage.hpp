#pragma once
#include <cstdint>

namespace WiggleRoom {

template<int N>
struct EuclogicExpanderMessageT {
    static constexpr int NUM_CH = N;
    static constexpr int N_STATES = (1 << N);
    static constexpr uint8_t OUTPUT_MASK = static_cast<uint8_t>((1 << N) - 1);

    bool gates[N];
    bool triggers[N];
    float lfo[N];
    float cv[N];
    int currentStep[N];
    int totalSteps[N];

    uint8_t truthTableMapping[N_STATES];
    uint8_t truthTableLocks[N_STATES];
    bool postLogicGates[N];
    float probB[N];
    float colDensity[N];

    int steps[N];
    int hits[N];
    int quant[N];
    float probA[N];
    bool retrigger[N];
    bool bipolar[N];
    float speed;
    float swing;

    bool valid;

    EuclogicExpanderMessageT() { clear(); }

    void clear() {
        for (int i = 0; i < N; i++) {
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
        for (int i = 0; i < N_STATES; i++) {
            truthTableMapping[i] = static_cast<uint8_t>(i) & OUTPUT_MASK;
            truthTableLocks[i] = 0;
        }
        speed = 8.f;
        swing = 50.f;
        valid = false;
    }
};

// Backward-compatible alias
using EuclogicExpanderMessage = EuclogicExpanderMessageT<4>;

} // namespace WiggleRoom
