// Modal Bell - Resonant Bell/Chime Synthesizer
// Creates bell, marimba, and metallic percussion sounds
// Uses resonant bandpass filters for modal synthesis

import("stdfaust.lib");

declare name "Modal Bell";
declare author "WiggleRoom";
declare description "Resonant bell/chime synthesizer";

// --- CONTROLS ---
// Pitch (V/Oct input, 0V = C4)
volts = hslider("volts", 0, -5, 5, 0.001);
freq = 261.62 * (2.0 ^ volts);

// Trigger (Gate input, triggers when > 1V)
gate = hslider("gate", 0, 0, 10, 0.01);

// Timbre Knobs
// Brightness: affects harmonic content
brightness = hslider("brightness", 0.5, 0, 1, 0.01) : si.smoo;

// Decay: how long the bell rings
decayTime = hslider("decay", 0.5, 0.1, 5, 0.01) : si.smoo;

// Material: 0=Wood, 0.5=Mixed, 1=Metal (affects inharmonicity)
material = hslider("material", 0.5, 0, 1, 0.01) : si.smoo;

// --- TRIGGER DETECTION ---
// Rising edge detection: triggers when gate crosses 0.5V threshold
trig = (gate > 0.5) & (gate' <= 0.5);

// --- EXCITER ---
// Short noise burst when triggered
exciter = no.noise * en.ar(0.001, 0.01 + brightness * 0.02, trig);

// --- MODAL RESONATORS ---
// Create bell-like sound using resonant bandpass filters at harmonic ratios
// Bell harmonics are slightly inharmonic (not perfect integer ratios)

// Inharmonicity factor based on material
// 0 = harmonic (wood-like), 1 = inharmonic (metallic bell)
inharm = 1.0 + material * 0.15;

// Mode frequencies (bell-like ratios)
f1 = freq;
f2 = freq * 2.0 * inharm;
f3 = freq * 3.0 * (inharm ^ 2);
f4 = freq * 4.2 * (inharm ^ 3);
f5 = freq * 5.4 * (inharm ^ 4);

// Q factor (resonance) based on decay
baseQ = 50 + decayTime * 200;

// Resonant filters for each mode
mode(f, q, gain) = fi.resonbp(min(f, ma.SR/2.1), q, gain);

// Mix modes with decreasing amplitude for higher partials
modes = _ <:
    mode(f1, baseQ, 1.0),
    mode(f2, baseQ * 0.8, 0.6 * brightness),
    mode(f3, baseQ * 0.6, 0.4 * brightness),
    mode(f4, baseQ * 0.4, 0.25 * brightness),
    mode(f5, baseQ * 0.3, 0.15 * brightness)
    :> _;

// --- OUTPUT ---
// Apply soft clipping to prevent harsh distortion
softclip(x) = x : ma.tanh;

process = exciter : modes : softclip <: _,_;
