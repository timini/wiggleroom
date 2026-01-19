// Modal Bell - Resonant Bell/Chime Synthesizer
// Creates bell, marimba, vibraphone, and metallic percussion sounds
// Uses resonant bandpass filters for modal synthesis with 9 modes

import("stdfaust.lib");

declare name "Modal Bell";
declare author "WiggleRoom";
declare description "Resonant bell/chime synthesizer with strike position and velocity";

// ==========================================================
// CONTROLS (alphabetical for Faust parameter ordering)
// ==========================================================

// Brightness: affects harmonic content (0=dark, 1=bright)
brightness = hslider("brightness", 0.5, 0, 1, 0.01) : si.smoo;

// Decay: how long the bell rings (scaled by pitch)
decayTime = hslider("decay", 0.5, 0.1, 5, 0.01) : si.smoo;

// Gate input (triggers when > 0.5V)
gate = hslider("gate", 0, 0, 10, 0.01);

// Material: 0=Wood (harmonic), 0.5=Bronze, 1=Metal (very inharmonic)
// Extended range for gamelan/church bell sounds
material = hslider("material", 0.5, 0, 1, 0.01) : si.smoo;

// Strike position: 0=center (fundamental), 1=edge (harmonics)
// Simulates where the mallet hits the bell/bar
strike = hslider("strike", 0.3, 0, 1, 0.01) : si.smoo;

// Velocity: controls intensity and brightness of strike
// 0=soft mallet, 1=hard mallet
velocity = hslider("velocity", 0.8, 0, 1, 0.01);

// Pitch (V/Oct input, 0V = C4)
volts = hslider("volts", 0, -5, 5, 0.001);
freq = max(20, 261.62 * (2.0 ^ volts));

// ==========================================================
// TRIGGER DETECTION
// ==========================================================

// Rising edge detection: triggers when gate crosses 0.5V threshold
trig = (gate > 0.5) & (gate' <= 0.5);

// ==========================================================
// EXCITER (Mallet Strike)
// ==========================================================

// Attack time based on velocity (soft=slow, hard=fast)
// Soft mallet: 5ms attack, Hard mallet: 0.5ms attack
attackTime = 0.0005 + (1 - velocity) * 0.005;

// Exciter decay time (slightly longer for soft mallets)
exciterDecay = 0.01 + (1 - velocity) * 0.02 + brightness * 0.01;

// Noise brightness based on velocity (hard hits = brighter noise)
noiseCutoff = 2000 + velocity * 8000 + brightness * 4000;

// Short filtered noise burst when triggered
noiseSource = no.noise : fi.lowpass(2, noiseCutoff);
exciter = noiseSource * en.ar(attackTime, exciterDecay, trig) * (0.5 + velocity * 0.5);

// ==========================================================
// MODAL RESONATORS (9 modes)
// ==========================================================

// Inharmonicity factor based on material
// 0 = perfect harmonics (marimba/vibraphone)
// 1 = 30% frequency stretch (church bell/gamelan)
inharm = 1.0 + material * 0.30;

// Bell-like mode frequency ratios
// Based on real bell acoustics with inharmonic stretching
f1 = freq;                          // Fundamental
f2 = freq * 2.0 * inharm;           // Second partial
f3 = freq * 2.6 * (inharm ^ 1.2);   // Minor third partial (bell characteristic)
f4 = freq * 3.5 * (inharm ^ 1.5);   //
f5 = freq * 4.2 * (inharm ^ 2.0);   //
f6 = freq * 5.4 * (inharm ^ 2.5);   //
f7 = freq * 6.3 * (inharm ^ 3.0);   // Upper partials
f8 = freq * 8.1 * (inharm ^ 3.5);   //
f9 = freq * 10.2 * (inharm ^ 4.0);  // Highest shimmer

// Pitch-tracking decay: higher notes decay faster (physically accurate)
// Reference: C4 (261.62 Hz) uses full decay time
// One octave up = decay * 0.7, one octave down = decay * 1.4
pitchDecayScale = pow(261.62 / freq, 0.5);
scaledDecay = decayTime * pitchDecayScale;

// Q factor (resonance) based on scaled decay
// Higher Q = longer ring time
baseQ = 50 + scaledDecay * 200;

// Strike position affects mode amplitudes
// Center strike (0): emphasizes fundamental and odd harmonics
// Edge strike (1): emphasizes higher partials, reduces fundamental
// Based on vibration nodes of a circular membrane/bar
strikeGain(modeNum) = select2(modeNum == 1,
    // Higher modes: boosted at edge
    0.3 + strike * 0.7,
    // Fundamental: strongest at center
    1.0 - strike * 0.6
);

// Combined brightness + velocity effect on upper partials
// Soft velocity reduces upper partials further
effectiveBrightness = brightness * (0.4 + velocity * 0.6);

// Mode gains: fundamental strong, upper partials scaled by brightness and strike
g1 = 1.0 * strikeGain(1);
g2 = 0.7 * effectiveBrightness * strikeGain(2);
g3 = 0.55 * effectiveBrightness * strikeGain(3);
g4 = 0.4 * effectiveBrightness * strikeGain(4);
g5 = 0.3 * effectiveBrightness * strikeGain(5);
g6 = 0.22 * effectiveBrightness * strikeGain(6);
g7 = 0.15 * effectiveBrightness * strikeGain(7);
g8 = 0.1 * effectiveBrightness * strikeGain(8);
g9 = 0.06 * effectiveBrightness * strikeGain(9);

// Resonant filter for each mode
// Frequency clamped to avoid aliasing near Nyquist
mode(f, q, gain) = fi.resonbp(min(f, ma.SR/2.2), q, gain);

// Q scaling: higher modes have lower Q (decay faster)
qScale(n) = 1.0 / sqrt(n);

// All 9 modes in parallel
modes = _ <:
    mode(f1, baseQ * qScale(1), g1),
    mode(f2, baseQ * qScale(2), g2),
    mode(f3, baseQ * qScale(3), g3),
    mode(f4, baseQ * qScale(4), g4),
    mode(f5, baseQ * qScale(5), g5),
    mode(f6, baseQ * qScale(6), g6),
    mode(f7, baseQ * qScale(7), g7),
    mode(f8, baseQ * qScale(8), g8),
    mode(f9, baseQ * qScale(9), g9)
    :> _;

// ==========================================================
// OUTPUT PROCESSING
// ==========================================================

// Output gain compensation
// Boost when brightness is low to maintain usable output
// Also compensate for velocity
brightnessCompensation = 1.0 + (1 - effectiveBrightness) * 0.8;
velocityCompensation = 0.7 + velocity * 0.3;
outputGain = 2.5 * brightnessCompensation * velocityCompensation;

// Soft clipping to prevent harsh distortion
softclip(x) = ma.tanh(x);

// DC blocker (resonant filters can accumulate DC)
dcblock = fi.dcblocker;

// Final processing chain
process = exciter : modes : dcblock : *(outputGain) : softclip <: _, _;
