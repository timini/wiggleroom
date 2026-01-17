// The Abyss - Waterphone
// Horror-movie instrument with bowed metal rods and water modulation
// Used in The Matrix, Poltergeist, Alien

import("stdfaust.lib");

declare name "The Abyss";
declare author "WiggleRoom";
declare description "Waterphone - bowed metal rods with hydro-modulation";

// ==========================================================
// 1. CONTROLS (alphabetical for Faust parameter ordering)
// ==========================================================

// DECAY - Resonance time of the rods
decay = hslider("decay", 0.99, 0.9, 0.999, 0.001);

// PRESSURE - How hard you press the bow (affects timbre)
pressure = hslider("pressure", 0.5, 0.01, 1.0, 0.01);

// SLOSH - Amount of water pitch warping
slosh = hslider("slosh", 0.2, 0, 1, 0.01);

// VELOCITY - Bow speed (controlled by gate)
velocity = hslider("velocity", 0, 0, 1, 0.01) : si.smoo;

// VOLTS - Base frequency V/Oct
volts = hslider("volts", 0, -2, 8, 0.001);

// ==========================================================
// 2. DERIVED PARAMETERS
// ==========================================================

// Base frequency (A2 = 110Hz at 0V)
freq_base = 110 * (2.0 ^ volts);

// ==========================================================
// 3. THE BOW (Stick-Slip Friction Model)
// ==========================================================

// Real bowing has micro-variations in pressure, speed, and contact point
// This creates the living, breathing quality of bowed instruments

// Velocity with fast attack, slow release (rods ring out after bowing stops)
vel_attack = 0.995;   // ~40ms attack
vel_release = 0.9998; // ~400ms release
vel_smooth = velocity : si.smooth(ba.if(velocity > velocity', vel_attack, vel_release));

// Bow instability - subtle random variations in bow speed/pressure
bow_jitter = no.lfnoise(8) : fi.lowpass(1, 10) : *(0.15);  // Slow wobble
bow_flutter = no.lfnoise(25) : fi.lowpass(1, 30) : *(0.08); // Faster flutter

// Dynamic bow pressure with instability
effective_pressure = pressure * (1 + bow_jitter) : max(0.01) : min(1);

// Bow frequency with slight instability (creates organic pitch variation)
bow_freq = freq_base * (0.5 + effective_pressure * 0.5) * (1 + bow_flutter * 0.02);

// Multiple excitation components that blend based on pressure:

// 1. Soft bowing (low pressure) - filtered triangle, gentle
soft_bow = os.triangle(bow_freq) : fi.lowpass(1, 1500);

// 2. Medium bowing - sawtooth with filtering
medium_bow = os.sawtooth(bow_freq) : fi.lowpass(1, 2500 + pressure * 2000);

// 3. Hard bowing (high pressure) - adds harmonics and grit
hard_bow = os.sawtooth(bow_freq * 2) : fi.highpass(1, 500) : *(0.3);

// Crossfade between soft and hard based on pressure
bow_blend = soft_bow * (1 - pressure) + medium_bow * pressure + hard_bow * pressure * pressure;

// Rosin texture - subtle crackling that increases with pressure
rosin = no.noise : fi.bandpass(2, 800, 3000) : *(0.03 + pressure * 0.05);

// Amplitude dynamics - bow "catches" and releases
catch_release = 1 + (no.lfnoise(3) : fi.lowpass(1, 5) : *(0.2 * vel_smooth));

// Final bow signal with all dynamics
bow_excitation = (bow_blend + rosin) * vel_smooth * effective_pressure * catch_release * 0.4;

// ==========================================================
// 4. THE WATER (Chaotic Hydro-Modulation)
// ==========================================================

// Independent slow drifting movements for each rod
// Uses low-frequency noise for organic, unpredictable movement
// Different rates create the "sloshing" effect as rods go in/out of tune

water_lfo(i) = no.lfnoise(0.3 + i * 0.15) : fi.lowpass(1, 2) : *(slosh * 0.15);

// Secondary slower modulation for longer sweeps
water_slow(i) = no.lfnoise(0.08 + i * 0.03) : fi.lowpass(1, 0.5) : *(slosh * 0.08);

// Combined water modulation per rod
water_mod(i) = 1.0 + water_lfo(i) + water_slow(i);

// ==========================================================
// 5. THE RODS (Modal Resonator Bank)
// ==========================================================

// 5 Rods tuned to a dissonant cluster - "The Scary Chord"
// These ratios create the unsettling, metallic Waterphone sound
ratios(0) = 1.0;    // Fundamental
ratios(1) = 1.14;   // Sharp minor second
ratios(2) = 1.32;   // Tritone-ish
ratios(3) = 1.57;   // Sharp seventh
ratios(4) = 1.88;   // Minor ninth territory

// Individual rod amplitudes (outer rods slightly quieter)
rod_amp(0) = 1.0;
rod_amp(1) = 0.9;
rod_amp(2) = 0.85;
rod_amp(3) = 0.8;
rod_amp(4) = 0.75;

// Q factor increases with decay - longer decay = sharper resonance
q_factor = 30 + (decay - 0.9) * 500;  // Range ~30 to ~80

// A single resonating rod
rod(i, sig) = sig : fi.resonbp(f, q_factor, rod_amp(i))
    with {
        // Frequency = Base * Ratio * Water Modulation
        f = max(20, min(15000, freq_base * ratios(i) * water_mod(i)));
    };

// Bank of all 5 rods in parallel
rod_bank(sig) = sig <: par(i, 5, rod(i)) :> _;

// ==========================================================
// 6. RESONANT BODY (The Bowl)
// ==========================================================

// The metal bowl adds body resonance and sustain
// Simulated with a comb filter tuned below the fundamental
bowl_freq = freq_base * 0.5;  // Octave below
bowl_delay = max(8, ma.SR / bowl_freq);
bowl_fb = decay * 0.7;
bowl_resonator(sig) = sig + (sig : (+ ~ (de.fdelay(4096, bowl_delay) : fi.lowpass(1, 2000) : *(bowl_fb)))) * 0.3;

// ==========================================================
// 7. OUTPUT PROCESSING
// ==========================================================

dc_block = fi.dcblocker;
soft_limit = ma.tanh;

// Stereo spread - slight delay and filtering difference
stereo_left = fi.lowpass(1, 8000);
stereo_right = de.delay(512, 30) : fi.highpass(1, 200);
stereo_spread = _ <: (stereo_left, stereo_right);

// ==========================================================
// 8. MAIN PROCESS
// ==========================================================

// Signal chain: Bow -> Rods -> Bowl -> Output
// Reduced boost to prevent clipping, added extra limiting
process = bow_excitation
        : rod_bank
        : bowl_resonator
        : *(0.8)  // Reduced boost to prevent clipping
        : dc_block
        : soft_limit
        : *(0.9)  // Final safety gain
        : stereo_spread;
