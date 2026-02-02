// Tetanus Coil - Extreme Dispersive Spring Reverb
// Physical model of spring coils with non-linear feedback
// Creates "bonkers" drippy, chirpy, screaming spring sounds

import("stdfaust.lib");

declare name "Tetanus Coil";
declare author "WiggleRoom";
declare description "Extreme dispersive spring reverb with non-linear feedback";

// ==========================================================
// 1. CONTROLS (Alphabetical for Faust indexing)
// ==========================================================

// DARKNESS: Lowpass cutoff in feedback loop (200Hz - 18kHz)
darkness = hslider("darkness", 5000, 200, 18000, 10) : si.smoo;

// FEEDBACK: Recirculation amount (0 - 0.99)
feedback = hslider("feedback", 0.9, 0, 0.99, 0.001);

// GRIT: Distortion amount inside feedback loop (0 - 10)
// Scaled to 0-1 drive range for cubicnl (0.1x multiplier)
grit = hslider("grit", 0, 0, 10, 0.1);

// KICK: Trigger input for noise burst (gate > 0.9)
kick = hslider("kick", 0, 0, 10, 0.01);

// TENSION: Base delay length affecting pitch/dispersion (1 - 500 samples)
tension = hslider("tension", 100, 1, 500, 1) : si.smoo;

// WOBBLE: LFO modulation depth for spring segments (0 - 5ms)
wobble = hslider("wobble", 0.1, 0, 5, 0.01);

// ==========================================================
// 2. KICK CIRCUIT - Noise burst injection
// ==========================================================

// Detect rising edge: gate > 0.9 (test compatible threshold)
kick_trig = (kick > 0.9) & (kick' <= 0.9);

// Envelope for kick burst: fast attack, medium decay
kick_env = kick_trig : ba.impulsify : en.ar(0.001, 0.2);

// Low-passed noise burst (noise -> filter -> scale by envelope)
kick_gen = no.noise : fi.lowpass(2, 400) * kick_env;

// ==========================================================
// 3. SPRING STAGE - Dispersive all-pass filter
// ==========================================================

// A single "coil" is an all-pass filter with modulation
// idx parameter offsets LFO phase and delay for ripple effect
spring_stage(freq_mod, idx) = fi.allpass_fcomb(1024, delay_len, -0.7)
with {
    // Offset LFO phase per stage for "rippling" effect
    lfo = os.osc(freq_mod + (idx * 0.01)) * wobble;
    // Prime number offsets create inharmonic dispersion (the "chirp")
    delay_len = max(1, tension + (tension * 0.1 * lfo) + (idx * 13));
};

// ==========================================================
// 4. SPRING TANK - 8 cascaded dispersion stages
// ==========================================================

// Chain of 8 all-pass filters creates the classic spring "drip" sound
// Different LFO rates per stage create complex modulation
tank =
    spring_stage(0.2, 1) :
    spring_stage(0.3, 2) :
    spring_stage(0.1, 3) :
    spring_stage(0.5, 4) :
    spring_stage(0.2, 5) :
    spring_stage(0.3, 6) :
    spring_stage(0.1, 7) :
    spring_stage(0.5, 8);

// ==========================================================
// 5. NON-LINEAR FEEDBACK LOOP
// ==========================================================

// The secret sauce: distortion INSIDE the loop
// As feedback builds, it doesn't just get louder - it gets angrier
// Drive scaled 0.1x so grit 0-10 maps to drive 0-1 (reasonable range)
loop_body =
    tank :                          // Spring dispersion
    fi.lowpass(1, darkness) :       // Darken the tail
    ef.cubicnl(grit * 0.1, 0) :     // Saturation (drive 0-1)
    fi.dcblocker;                   // Prevent DC buildup in feedback

// Single channel spring with feedback loop
// sig -> add feedback -> add kick -> process -> feedback out
spring_channel = (+ : + (kick_gen) : loop_body) ~ (*(feedback));

// ==========================================================
// 6. STEREO CROSS-COUPLING
// ==========================================================

// Cross-coupling factor (how much left leaks into right and vice versa)
cross_couple = 0.15;

// Stereo spring with cross-feed for wider image
// Process L and R through independent springs, then cross-couple output
stereo_springs = (spring_channel, spring_channel) : cross_mix
with {
    // Mix some of each channel into the other for stereo width
    cross_mix(l, r) = l + r * cross_couple, r + l * cross_couple;
};

// ==========================================================
// 7. MAIN PROCESS
// ==========================================================

// Output limiting to prevent clipping
output_limiter = ma.tanh;

// True stereo processing with cross-coupled springs
// Mono input is split if only one channel connected (handled in C++)
process = stereo_springs : output_limiter, output_limiter;
