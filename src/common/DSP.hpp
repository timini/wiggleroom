#pragma once

#include <cmath>

namespace WiggleRoom {
namespace DSP {

// Mathematical constants
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;

// Convert V/Oct to frequency multiplier
// 0V = 1x, 1V = 2x, -1V = 0.5x, etc.
inline float voltToFreqMultiplier(float volts) {
    return std::pow(2.0f, volts);
}

// Convert MIDI note to frequency (A4 = 440Hz = note 69)
inline float midiToFreq(float note) {
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

// Clamp value between min and max
template<typename T>
inline T clamp(T value, T min, T max) {
    return value < min ? min : (value > max ? max : value);
}

// Linear interpolation
inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// Simple one-pole lowpass filter for parameter smoothing
class OnePoleLPF {
public:
    OnePoleLPF(float cutoff = 0.1f) : coeff(cutoff), state(0.0f) {}

    void setCutoff(float cutoff) {
        coeff = clamp(cutoff, 0.0f, 1.0f);
    }

    float process(float input) {
        state += coeff * (input - state);
        return state;
    }

    void reset(float value = 0.0f) {
        state = value;
    }

private:
    float coeff;
    float state;
};

// Polyblep for anti-aliased waveforms
inline float polyblep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

} // namespace DSP
} // namespace WiggleRoom
