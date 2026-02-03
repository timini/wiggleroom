// Modal Bell - Morphing Percussion Synthesizer
// Morphs between Wood Block, Marimba, Vibraphone, Kalimba, and Glockenspiel
// Uses resonant bandpass filters for modal synthesis with 9 modes
// Based on physical modeling of struck bars and plates

import("stdfaust.lib");

declare name "Modal Bell";
declare author "WiggleRoom";
declare description "Morphing percussion synthesizer - Wood Block to Glockenspiel";

// ==========================================================
// CONTROLS (alphabetical for Faust parameter ordering)
// ==========================================================

// Brightness: affects harmonic content (0=dark, 1=bright)
brightness = hslider("brightness", 0.5, 0, 1, 0.01) : si.smoo;

// Damping: 0=ring freely, 1=muted/choked
damping = hslider("damping", 0, 0, 1, 0.01) : si.smoo;

// Gate input (triggers when > 0.5V)
gate = hslider("gate", 0, 0, 10, 0.01);

// Morph: The main macro control for instrument type
// 0.00 = Wood Block / Slit Drum (inharmonic, hollow, short decay)
// 0.25 = Marimba (harmonic 1:4 ratio, warm wood, medium decay)
// 0.50 = Vibraphone (harmonic 1:4 ratio, singing metal, long decay)
// 0.75 = Kalimba / Mbira (clamped tine, anharmonic buzz)
// 1.00 = Glockenspiel / Chimes (stiff steel bar, piercing, long decay)
morph = hslider("morph", 0.25, 0, 1, 0.001) : si.smoo;

// Strike position: 0.03=edge (thin, harmonics), 0.5=center (fundamental)
// Simulates where the mallet hits the bar
// Minimum 0.03 prevents silent output (sin(0) = 0 for all modes)
strike = hslider("strike", 0.3, 0.03, 0.5, 0.01) : si.smoo;

// Velocity: controls intensity and brightness of strike
// 0=soft wool mallet, 1=hard metal mallet
velocity = hslider("velocity", 0.8, 0, 1, 0.01);

// Pitch (V/Oct input, 0V = C4)
volts = hslider("volts", 0, -5, 5, 0.001);
freq = max(20, 261.62 * (2.0 ^ volts));

// ==========================================================
// INSTRUMENT PRESET DATA (The Physics)
// ==========================================================

// Each instrument has:
// - Mode 2 frequency ratio (relative to fundamental)
// - Mode 3 frequency ratio
// - Base decay time scale

// Inst 0: Wood Block / Slit Drum
// Inharmonic, hollow, short decay
p0_r2 = 1.41;   p0_r3 = 2.3;    p0_d = 0.4;

// Inst 1: Marimba
// Harmonic 1:4:10 ratio, warm wood
p1_r2 = 3.99;   p1_r3 = 10.0;   p1_d = 1.2;

// Inst 2: Vibraphone
// Harmonic 1:4:9, singing aluminum, long sustain
p2_r2 = 3.99;   p2_r3 = 9.2;    p2_d = 4.0;

// Inst 3: Kalimba / Mbira
// Clamped tine, anharmonic 1:6:14, unique buzz
p3_r2 = 6.2;    p3_r3 = 14.3;   p3_d = 2.5;

// Inst 4: Glockenspiel / Chimes
// Stiff steel bar, 1:2.7:5.4, piercing and pure
p4_r2 = 2.71;   p4_r3 = 5.35;   p4_d = 3.5;

// ==========================================================
// 5-STAGE INTERPOLATOR
// ==========================================================

// Interpolates between 5 preset values based on morph knob position
// Maps 0-1 range to 0-4 segments for smooth transitions
interp5(v0, v1, v2, v3, v4, x) = result
with {
    // Scale to 0-4 range
    s = x * 4.0;

    // Segment-based linear interpolation
    result =
        ba.if(s < 1, v0 + (v1 - v0) * s,
        ba.if(s < 2, v1 + (v2 - v1) * (s - 1),
        ba.if(s < 3, v2 + (v3 - v2) * (s - 2),
                     v3 + (v4 - v3) * (s - 3)
        )));
};

// Calculate current instrument parameters from morph position
currentR2 = interp5(p0_r2, p1_r2, p2_r2, p3_r2, p4_r2, morph);
currentR3 = interp5(p0_r3, p1_r3, p2_r3, p3_r3, p4_r3, morph);
currentDecay = interp5(p0_d, p1_d, p2_d, p3_d, p4_d, morph);

// ==========================================================
// TRIGGER DETECTION
// ==========================================================

// Rising edge detection: triggers when gate crosses 0.5V threshold
trig = (gate > 0.5) & (gate' <= 0.5);

// ==========================================================
// EXCITER (Mallet Strike)
// ==========================================================

// Attack time based on velocity (soft=slow, hard=fast)
// Soft mallet: 5.5ms attack, Hard mallet: 0.5ms attack
attackTime = 0.0005 + (1 - velocity) * 0.005;

// Exciter decay time (slightly longer for soft mallets)
exciterDecay = 0.01 + (1 - velocity) * 0.02 + brightness * 0.01;

// Noise brightness based on velocity and morph
// Wood instruments get darker noise, metal gets brighter
morphBrightness = 0.5 + morph * 0.5;
noiseCutoff = 1500 + velocity * 8000 * morphBrightness + brightness * 4000;

// Short filtered noise burst when triggered
noiseSource = no.noise : fi.lowpass(2, noiseCutoff);
exciter = noiseSource * en.ar(attackTime, exciterDecay, trig) * (0.5 + velocity * 0.5);

// ==========================================================
// MODAL RESONATORS (9 modes)
// ==========================================================

// Dynamic damping: if damping knob is up, drastically shorten decay
dampMod = 1.0 - (damping * 0.95);
realDecay = currentDecay * dampMod;

// Pitch-tracking decay: higher notes decay faster (physically accurate)
// Reference: C4 (261.62 Hz) uses full decay time
pitchDecayScale = pow(261.62 / freq, 0.5);
scaledDecay = realDecay * pitchDecayScale;

// Build frequency ratios for 9 modes
// Modes 1-3 use the interpolated ratios from instrument presets
// Modes 4-9 are extrapolated based on the character of modes 2 and 3
ratio1 = 1.0;
ratio2 = currentR2;
ratio3 = currentR3;

// Extrapolate higher modes based on the relationship between r2 and r3
// This preserves the harmonic/inharmonic character of each instrument
rateOfGrowth = (currentR3 / currentR2);
ratio4 = currentR3 * rateOfGrowth * 0.6;
ratio5 = ratio4 * rateOfGrowth * 0.7;
ratio6 = ratio5 * rateOfGrowth * 0.75;
ratio7 = ratio6 * rateOfGrowth * 0.8;
ratio8 = ratio7 * rateOfGrowth * 0.85;
ratio9 = ratio8 * rateOfGrowth * 0.9;

// Calculate mode frequencies
f1 = freq * ratio1;
f2 = freq * ratio2;
f3 = freq * ratio3;
f4 = freq * ratio4;
f5 = freq * ratio5;
f6 = freq * ratio6;
f7 = freq * ratio7;
f8 = freq * ratio8;
f9 = freq * ratio9;

// Q factor (resonance) based on scaled decay
// Higher Q = longer ring time
baseQ = 50 + scaledDecay * 200;

// Strike position affects mode amplitudes
// Center strike (0.5): emphasizes fundamental, suppresses mode 2 (even harmonic)
// Edge strike (0): emphasizes higher partials
// Based on sin(mode_number * position * PI) - simulates nodes/antinodes
strikeAmp(modeNum) = sin(modeNum * strike * 3.14159);

// Combined brightness + velocity effect on upper partials
effectiveBrightness = brightness * (0.4 + velocity * 0.6);

// Mode gains: fundamental strong, upper partials scaled by brightness, strike, velocity
g1 = 1.0 * strikeAmp(1);
g2 = 0.7 * effectiveBrightness * strikeAmp(2);
g3 = 0.55 * effectiveBrightness * strikeAmp(3);
g4 = 0.4 * effectiveBrightness * strikeAmp(4);
g5 = 0.3 * effectiveBrightness * strikeAmp(5);
g6 = 0.22 * effectiveBrightness * strikeAmp(6);
g7 = 0.15 * effectiveBrightness * strikeAmp(7);
g8 = 0.1 * effectiveBrightness * strikeAmp(8);
g9 = 0.06 * effectiveBrightness * strikeAmp(9);

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
brightnessCompensation = 1.0 + (1 - effectiveBrightness) * 0.8;
velocityCompensation = 0.7 + velocity * 0.3;
outputGain = 2.5 * brightnessCompensation * velocityCompensation;

// Soft clipping to prevent harsh distortion
softclip(x) = ma.tanh(x);

// DC blocker (resonant filters can accumulate DC)
dcblock = fi.dcblocker;

// Final processing chain
process = exciter : modes : dcblock : *(outputGain) : softclip <: _, _;
