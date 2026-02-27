// Matter - Physical Modeling Synthesizer
// Simulates a solid object (block/bar/plate) struck by a mallet,
// resonating inside an acoustic tube chamber
//
// Architecture:
//   Mallet (Exciter) -> Block (Modal Resonator) -> Tube (Waveguide)

import("stdfaust.lib");

declare name "Matter";
declare author "WiggleRoom";
declare description "Physical model: struck solid inside resonant tube";

// ==========================================================
// CONTROLS
// ==========================================================

// PITCH (V/Oct, 0V = C4)
volts = hslider("volts", 0, -5, 10, 0.001);
freq_base = max(20, 261.62 * (2.0 ^ volts));

// TRIGGER (triggers when gate crosses ~1V threshold)
gate = hslider("gate", 0, 0, 10, 0.01);
trig = (gate > 0.9) & (gate' <= 0.9);

// A. THE MALLET (What are we hitting it with?)
// 0 = Soft Felt, 1 = Hard Wood
hardness = hslider("hardness", 0.5, 0.01, 1.0, 0.01) : si.smoo;

// B. THE SOLID (The Block)
// Structure: 0=Harmonic (string), 0.5=Stiff beam (marimba), 1=Inharmonic (bell)
structure = hslider("structure", 0.5, 0, 1, 0.01) : si.smoo;

// Position: 0=Center (fundamental), 1=Edge (harmonics)
position = hslider("position", 0.2, 0, 1, 0.01) : si.smoo;

// Decay: How long the block rings (Q factor multiplier)
decay = hslider("decay", 0.8, 0.1, 2.0, 0.01) : si.smoo;

// C. THE TUBE (The Environment)
// Length relative to block pitch (1.0 = unison, sympathetic resonance)
tube_len = hslider("tube", 1.0, 0.5, 2.0, 0.01) : si.smoo;

// How much tube sound to mix in
tube_mix = hslider("tube_mix", 0.5, 0, 1, 0.01) : si.smoo;

// ==========================================================
// STAGE 1: EXCITATION (The Mallet)
// ==========================================================

// Noise burst with short envelope
impulse = no.noise * en.ar(0.0001, 0.01, trig);

// Filter based on hardness: soft = dark, hard = bright
mallet = impulse : fi.lowpass(2, 200 + hardness * 12000);

// ==========================================================
// STAGE 2: THE BLOCK (Modal Resonator Bank)
// ==========================================================

// Frequency ratios interpolate between harmonic and inharmonic
// Harmonic: 1, 2, 3, 4 (string/air column)
// Inharmonic: 1, 2.7, 5.4, 8.9 (bar/bell)
ratio(i) = (i+1) * (1 - structure) + ((i+1) ^ 1.7) * structure;

// Mode frequencies (clamped to Nyquist)
f(i) = min(freq_base * ratio(i), ma.SR / 2.1);

// Gains based on strike position
// Center: fundamental dominates
// Edge: upper partials excited
gain(0) = 1.0 - position;
gain(1) = min(1.0, 0.5 + sin(position * ma.PI));
gain(2) = position;
gain(3) = position * position;

// Q factor based on decay (reduced max Q to prevent resonant buildup)
q = 25 * decay;

// Safe resonant bandpass
safe_resonbp(freq, q_val, g) = fi.resonbp(freq, max(1, q_val), g);

// The modal bank: 4 parallel resonators with normalized gains
// Total gain budget ~1.0 to prevent summation overload
block_sound = mallet <:
    safe_resonbp(f(0), q, gain(0) * 0.4),
    safe_resonbp(f(1), q * 0.8, gain(1) * 0.28),
    safe_resonbp(f(2), q * 0.6, gain(2) * 0.2),
    safe_resonbp(f(3), q * 0.4, gain(3) * 0.12)
    :> _;

// ==========================================================
// STAGE 3: THE TUBE (Waveguide Resonator)
// ==========================================================

// Delay line length in samples
tube_freq = freq_base * tube_len;
tube_samps = max(2, min(4000, ma.SR / tube_freq));

// Simple waveguide: delay with lowpass feedback (reduced feedback to prevent energy buildup)
tube_algo = (+ : de.fdelay(4096, tube_samps) : fi.lowpass(1, 3000)) ~ *(0.85);

// ==========================================================
// OUTPUT
// ==========================================================

// DC blocker for stability
dc_block = fi.dcblocker;

// Soft limiter
soft_limit = ma.tanh;

// Output gain - reduced from 8.0; gain staging now handled earlier in resonator bank
output_gain = 3.0;

// Mix dry block with tube, apply soft limit before gain to prevent overdrive
process = block_sound <: (_, tube_algo) :> _ * (1 - tube_mix * 0.5) + _ * tube_mix
    : soft_limit : *(output_gain) : dc_block : soft_limit <: _, _;
