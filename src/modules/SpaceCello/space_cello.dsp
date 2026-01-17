// Space Cello - Cathedral Yaybahar
// Full coupled physical model with sympathetic strings and reverb
// String -> Spring -> Tube -> Sympathetic String -> Reverb

import("stdfaust.lib");

declare name "Space Cello";
declare author "WiggleRoom";
declare description "Cathedral Yaybahar - coupled resonators with sympathetic strings and reverb";

// ==========================================================
// 1. CONTROLS (alphabetical for Faust parameter ordering)
// ==========================================================

// BODY - Overall resonance size (scales tube + reverb)
body = hslider("body", 0.5, 0, 1, 0.01) : si.smoo;

// DECAY - How long everything rings (extended range)
decay = hslider("decay", 0.97, 0.5, 0.999, 0.001);

// GATE - Trigger input
gate = button("gate");

// REVERB - Space/room amount
reverb_amt = hslider("reverb", 0.3, 0, 1, 0.01) : si.smoo;

// SPRING - Dispersion amount (laser effect)
spring = hslider("spring", 0.5, 0, 1, 0.01);

// SYMPATHETIC - Sympathetic string resonance amount
sympath = hslider("sympath", 0.5, 0, 1, 0.01) : si.smoo;

// TUBE - Body resonance ratio
tube_ratio = hslider("tube", 1.0, 0.25, 4.0, 0.01);

// VOLTS - String pitch (V/Oct)
volts = hslider("volts", 0, -2, 8, 0.001);

// ==========================================================
// 2. DERIVED PARAMETERS
// ==========================================================

// String frequency from V/Oct (A2 = 110Hz at 0V)
freq_string = 110.0 * (2.0 ^ volts);

// Sympathetic frequencies - rich harmonic series
freq_sympath1 = freq_string * 2.0;    // Octave
freq_sympath2 = freq_string * 1.5;    // Fifth
freq_sympath3 = freq_string * 3.0;    // Octave + Fifth (12th)
freq_sympath4 = freq_string * 4.0;    // Two octaves
freq_sympath5 = freq_string * 1.25;   // Major third

// Tube frequency with body scaling
body_scale = 1.0 + body * 0.5;  // Body makes tube larger
freq_tube = freq_string * tube_ratio / body_scale;

// Delay times in samples (with safety bounds)
string_delay = max(8, min(4000, ma.SR / freq_string - 0.5));
tube_delay = max(8, min(4000, ma.SR / freq_tube - 0.5));
sympath1_delay = max(8, min(4000, ma.SR / freq_sympath1 - 0.5));
sympath2_delay = max(8, min(4000, ma.SR / freq_sympath2 - 0.5));
sympath3_delay = max(8, min(4000, ma.SR / freq_sympath3 - 0.5));
sympath4_delay = max(8, min(4000, ma.SR / freq_sympath4 - 0.5));
sympath5_delay = max(8, min(4000, ma.SR / freq_sympath5 - 0.5));

// ==========================================================
// 3. THE EXCITATION (Pluck/Bow with Sustain)
// ==========================================================

// Smooth the gate to prevent clicks at onset
// This creates a ~5ms ramp instead of instant transition
gate_smooth = gate : si.smooth(0.999);
trig = (gate_smooth > 0.5) & (gate_smooth' <= 0.5);

// Noise burst for pluck (brighter, snappier)
pluck_noise = no.noise : fi.bandpass(2, 200, 4000);

// Softer noise for bow/sustain
bow_noise = no.noise : fi.lowpass(1, 1500) : fi.highpass(1, 100);

// Pluck envelope - slightly longer attack (5ms) to avoid clicks
pluck_env = en.ar(0.005, 0.04, trig);

// Bow/sustain envelope (ramps up and holds while gate is high)
bow_env = gate_smooth * 0.12;

// Combined excitation: strong pluck + gentle sustain
sustain_amount = body * 0.25;
excitation = pluck_noise * pluck_env * 0.5 + bow_noise * bow_env * (0.1 + sustain_amount);

// ==========================================================
// 4. THE MAIN STRING (Karplus-Strong) - Balanced resonance
// ==========================================================

// Damping controlled by decay - lower decay = darker, shorter
string_damp = fi.lowpass(1, 2000 + decay * 5000);

// Feedback with minimum floor so string always resonates
// Low decay = short plucky sound, high decay = long sustain
string_fb = 0.9 + decay * 0.085;  // Range: 0.9 to 0.985

string_model(exc) = exc : (+ : de.fdelay(4096, string_delay) : string_damp) ~ (*(string_fb))
                  : *(0.6)
                  : ma.tanh;

// ==========================================================
// 5. THE SPRING (Massive Dispersive Allpass Chain)
// ==========================================================

// Multiple spring chains in parallel for thick "boing" effect
// Prime number delays for rich, non-repeating dispersion

// Spring 1 - short chirpy spring
spring1_ap1 = fi.allpass_comb(1024, 89, -0.75);
spring1_ap2 = fi.allpass_comb(1024, 137, -0.7);
spring1_ap3 = fi.allpass_comb(1024, 199, -0.65);
spring1_ap4 = fi.allpass_comb(1024, 283, -0.6);
spring1 = spring1_ap1 : spring1_ap2 : spring1_ap3 : spring1_ap4;

// Spring 2 - medium twangy spring
spring2_ap1 = fi.allpass_comb(2048, 311, -0.7);
spring2_ap2 = fi.allpass_comb(2048, 419, -0.65);
spring2_ap3 = fi.allpass_comb(2048, 541, -0.6);
spring2_ap4 = fi.allpass_comb(2048, 661, -0.55);
spring2_ap5 = fi.allpass_comb(2048, 811, -0.5);
spring2 = spring2_ap1 : spring2_ap2 : spring2_ap3 : spring2_ap4 : spring2_ap5;

// Spring 3 - long swooshy spring
spring3_ap1 = fi.allpass_comb(4096, 701, -0.65);
spring3_ap2 = fi.allpass_comb(4096, 907, -0.6);
spring3_ap3 = fi.allpass_comb(4096, 1103, -0.55);
spring3_ap4 = fi.allpass_comb(4096, 1301, -0.5);
spring3 = spring3_ap1 : spring3_ap2 : spring3_ap3 : spring3_ap4;

// Combine all three springs
dispersion_chain = _ <: (spring1, spring2, spring3) :> /(2);

// Add feedback for extended spring resonance
spring_feedback = (+ : dispersion_chain : *(0.3)) ~ (de.delay(2048, 500) : *(spring * 0.4));

// Simple compressor to maintain level when spring spreads energy
// Attack fast, release medium, ratio ~4:1
spring_comp = co.compressor_mono(4, -20, 0.005, 0.1);

// Makeup gain increases with spring amount
spring_makeup = 1 + spring * 1.5;

spring_model(sig) = (sig * (1 - spring) + (sig : spring_feedback) * spring * 0.8)
                  : spring_comp : *(spring_makeup);

// ==========================================================
// 6. THE TUBE (Waveguide Body Resonator) - Controlled resonance
// ==========================================================

tube_damp = fi.lowpass(1, 2500 + body * 2000 + decay * 1500);

// Feedback with floor - always resonates, decay controls sustain
tube_fb = (0.85 + decay * 0.08) * (0.9 + body * 0.08);
tube_resonator(sig) = sig * 0.7 : (+ ~ (de.fdelay(4096, tube_delay) : tube_damp : *(tube_fb) : ma.tanh));

// ==========================================================
// 7. SYMPATHETIC STRINGS - Rich harmonic shimmer
// ==========================================================

// These are excited by the tube output, creating sitar-like shimmer
// Five strings tuned to harmonics of the fundamental

sympath_damp1 = fi.lowpass(1, 5000);
sympath_damp2 = fi.lowpass(1, 4500);
sympath_damp3 = fi.lowpass(1, 6000);
sympath_damp4 = fi.lowpass(1, 7000);
sympath_damp5 = fi.lowpass(1, 4000);

// Resonant bandpass to pick out sympathetic frequencies
sympath_excite1 = fi.resonbp(freq_sympath1, 5, 0.8);
sympath_excite2 = fi.resonbp(freq_sympath2, 5, 0.8);
sympath_excite3 = fi.resonbp(freq_sympath3, 6, 0.7);
sympath_excite4 = fi.resonbp(freq_sympath4, 7, 0.6);
sympath_excite5 = fi.resonbp(freq_sympath5, 4, 0.9);

// Sympathetic string 1 (octave)
sympath_string1(sig) = sig : sympath_excite1 : (+ : de.fdelay(4096, sympath1_delay) : sympath_damp1)
                     ~ (*(0.88 + decay * 0.07)) : *(sympath * 0.3);

// Sympathetic string 2 (fifth)
sympath_string2(sig) = sig : sympath_excite2 : (+ : de.fdelay(4096, sympath2_delay) : sympath_damp2)
                     ~ (*(0.87 + decay * 0.07)) : *(sympath * 0.25);

// Sympathetic string 3 (12th - octave + fifth)
sympath_string3(sig) = sig : sympath_excite3 : (+ : de.fdelay(4096, sympath3_delay) : sympath_damp3)
                     ~ (*(0.86 + decay * 0.07)) : *(sympath * 0.2);

// Sympathetic string 4 (two octaves)
sympath_string4(sig) = sig : sympath_excite4 : (+ : de.fdelay(4096, sympath4_delay) : sympath_damp4)
                     ~ (*(0.85 + decay * 0.07)) : *(sympath * 0.15);

// Sympathetic string 5 (major third)
sympath_string5(sig) = sig : sympath_excite5 : (+ : de.fdelay(4096, sympath5_delay) : sympath_damp5)
                     ~ (*(0.87 + decay * 0.07)) : *(sympath * 0.25);

// Combined sympathetic resonance - all 5 strings
sympathetic_resonance(sig) = sig + sympath_string1(sig) + sympath_string2(sig)
                           + sympath_string3(sig) + sympath_string4(sig) + sympath_string5(sig);

// ==========================================================
// 8. REVERB (Large Space) - Using Faust's built-in reverb
// ==========================================================

// Use dm.zita_light for lush, audible reverb
// Parameters: (predel, crossover, damping, rt60low, rt60mid, mix)
reverb_time = 2 + decay * 8 + body * 5;  // 2-15 seconds based on decay and body

// Zita-style reverb (stereo in, stereo out) - we'll use mono->stereo
zita_reverb = _ <: (_, _) : re.zita_rev1_stereo(
    200 + body * 800,    // predelay (ms * SR / 1000, but param is in samples)
    200,                  // crossover freq
    4000 + body * 2000,  // HF damping
    reverb_time,         // RT60 low
    reverb_time * 0.8,   // RT60 mid
    48000                // sample rate (will be overridden)
) : (_, !);  // Take left channel only for mono mix

// Lush parallel delay reverb with more presence
simple_reverb = _ <: par(i, 6,
    de.delay(16384, 800 + i * 600 + body * 1500) : *(0.7)
) :> /(3) : (+ ~ (de.delay(8192, 2500) : *(decay * 0.5) : fi.lowpass(1, 4000) : ma.tanh))
         : fi.lowpass(1, 4000 + body * 4000);

// Mix: more wet signal when reverb is turned up
reverb_mix(sig) = sig * (1 - reverb_amt * 0.7) + (sig : simple_reverb) * reverb_amt * 1.5;

// ==========================================================
// 9. OUTPUT PROCESSING
// ==========================================================

dc_block = fi.dcblocker;
soft_limit = ma.tanh;

// Wider stereo spread based on body
stereo_spread = _ <: (_, de.delay(512, 20 + body * 30));

// ==========================================================
// 10. MAIN PROCESS
// ==========================================================

// Full signal chain:
// Excitation -> String -> Spring -> Tube -> Sympathetic -> Reverb -> Output
process = excitation
        : string_model
        : spring_model
        : tube_resonator
        : sympathetic_resonance
        : reverb_mix
        : dc_block
        : soft_limit
        : stereo_spread;
