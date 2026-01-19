// Spectral Chord Resonator
// 6 tuned resonant filters creating lush, melodic chords
// Inspired by 4ms SMR and Mutable Instruments Rings

import("stdfaust.lib");

declare name "Spectral Resonator";
declare author "WiggleRoom";
declare description "6-band resonant filter bank for spectral chord processing";

// ==========================================================
// 1. CONTROLS
// ==========================================================

// DAMP: Lowpass filter on output to tame harsh highs (1000-20000 Hz)
damp = hslider("damp", 10000, 1000, 20000, 10) : si.smoo;

// Q (RESONANCE): How "ringing" the filters are (1-100)
// High values make it sound like a glass harp
q = hslider("q", 50, 1, 100, 0.1) : si.smoo;

// SPREAD: Determines the chord voicing (1.0-3.0)
// 1.0 = Unison, 1.5 = Fifths, 2.0 = Octaves
spread = hslider("spread", 1.5, 1.0, 3.0, 0.001) : si.smoo;

// CENTER FREQ via V/Oct input (0-10V range)
// Base is A2 (110Hz), each volt doubles frequency
volts = hslider("volts", 4, 0, 10, 0.01) : si.smoo;
base_freq = 110 * (2.0 ^ volts);

// ==========================================================
// 2. DSP LOGIC
// ==========================================================

// Function to create ONE resonant filter band
// 'i' is the index (0 to 5)
// We center the spread around band 2.5 so the pitch doesn't just go up
make_band(i) = fi.resonbp(f, q, gain)
with {
    // Calculate frequency ratio for this band relative to center
    ratio = spread ^ (i - 2.5);
    // Clamp frequency to safe audio range
    f = max(20, min(20000, base_freq * ratio));
    // Q-dependent gain compensation: high Q = lower gain to prevent clipping
    // At Q=1, gain=1.0; at Q=100, gain=0.1
    gain = 1.0 / max(1.0, q * 0.1);
};

// ==========================================================
// 3. MAIN PROCESS
// ==========================================================

// Signal flow:
// 1. Split mono input into 6 parallel wires
// 2. Run through 6 resonant bandpass filters
// 3. Group into stereo: Odd bands (0,2,4) = Left, Even bands (1,3,5) = Right
// 4. Apply lowpass damping filter to each channel

// Gentle soft clipper - polynomial curve that's nearly linear until |x| > 0.7
// This avoids the harmonic distortion of tanh for moderate signals
soft_clip(x) = x * (1.5 - 0.5 * x * x) : max(-1.0) : min(1.0);

// Output gain
output_gain = 0.6;

// Odd bands (0, 2, 4) summed to left channel
// Sum 3 bands, scale by 1/4 to normalize, filter, then gentle clip
group_odd = (_, !, _, !, _, !) :> *(0.25) : fi.lowpass(2, damp) : soft_clip : *(output_gain);

// Even bands (1, 3, 5) summed to right channel
group_even = (!, _, !, _, !, _) :> *(0.25) : fi.lowpass(2, damp) : soft_clip : *(output_gain);

// Main process: mono in -> 6 parallel filters -> stereo out
process = _ <: par(i, 6, make_band(i)) <: (group_odd, group_even);
