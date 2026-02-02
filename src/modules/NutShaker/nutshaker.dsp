// NutShaker - Stochastic Percussion Synthesizer
// Uses PhISM (Physically Informed Stochastic Modeling) to simulate
// particles (nuts/seeds) colliding inside a resonant shell.
//
// Unlike modal synthesis (bells, marimbas), stochastic synthesis uses
// probability clouds to generate organic, non-repeating textures.

import("stdfaust.lib");

// --- Controls ---

// Trigger input (VCV gate/trigger)
gate = hslider("gate", 0, 0, 10, 0.01);
trig = gate > 0.1;

// V/Oct pitch input for shell tuning
voct = hslider("voct", 0, -5, 5, 0.001);

// Shake Physics
force = hslider("force", 0.8, 0, 1, 0.01) : si.smoo;        // How hard you shake
duration = hslider("duration", 0.3, 0.05, 2.0, 0.01);       // How long the shake lasts

// Particle Properties (The Seeds)
density = hslider("density", 60, 1, 500, 1) : si.smoo;      // Collision rate (seeds per second)
chaos = hslider("chaos", 0.6, 0, 1, 0.01) : si.smoo;        // Timing randomness

// Shell Properties (The Resonant Body)
shell_pitch = hslider("pitch", 1200, 200, 6000, 1);         // Base frequency
shell_res = hslider("resonance", 8, 1, 40, 0.1) : si.smoo;  // Q factor (decay time)
shell_spread = hslider("spread", 0.35, 0, 1, 0.01);         // Harmonic spread (shell irregularity)

// Mix control
mix = hslider("mix", 0.7, 0, 1, 0.01) : si.smoo;            // Dry (clicks) vs wet (resonated)

// Output level
level = hslider("level", 0.8, 0, 1, 0.01) : si.smoo;

// --- DSP Algorithm ---

// Apply V/Oct to shell pitch
base_freq = shell_pitch * (2.0 ^ voct);

// 1. Shake Envelope
// Shaker envelope: snappy attack, sustained shake, gradual decay
attack_time = duration * 0.1;
decay_time = duration * 0.2;
sustain_level = 0.9;
release_time = duration * 0.7;

shake_env = trig : en.adsr(attack_time, decay_time, sustain_level, release_time) * force;

// 2. Particle Generator (Stochastic Impulse Train)
// Probability per sample - boosted for audibility
impulse_rate = max(0.0001, shake_env * density * 2.5 / ma.SR);

// Random trigger with probability
random_val = abs(no.noise);
seed_impulse = random_val < impulse_rate;

// 3. Impact Sound Generation - FULLER with longer envelope
// Each collision is a meaty burst of filtered noise
impact_amp = 0.6 + 0.4 * random_val;  // Random amplitude 0.6-1.0

// Longer, punchier impact envelope for body
impulse_env = seed_impulse : en.ar(0.001, 0.015);  // 1ms attack, 15ms release - much fuller

// Bandlimited noise burst (less harsh)
impact_noise = no.noise * impulse_env * impact_amp * shake_env;

// 4. Shell Resonators - EXPANDED for fuller sound
// 8 resonators including sub-harmonics for body

// Sub-harmonic for body/thump
f_sub = base_freq * 0.5;
// Main frequency cluster (irregular shell modes)
f1 = base_freq;
f2 = base_freq * 1.12;   // Slightly detuned
f3 = base_freq * 1.28;   // Minor third-ish
f4 = base_freq * 1.41;   // Tritone area - adds tension
f5 = base_freq * 1.62;   // Golden ratio
f6 = base_freq * 1.89;   // Upper brightness
// High overtone for attack definition
f_high = base_freq * 2.4;

// Variable Q values - longer decay for lower freqs
q_sub = shell_res * 1.5;   // Body rings longer
q1 = shell_res;
q2 = shell_res * 0.9;
q3 = shell_res * 0.8;
q4 = shell_res * 0.7;
q5 = shell_res * 0.6;
q6 = shell_res * 0.5;
q_high = shell_res * 0.4;  // Shorter high end

// Parallel resonator bank - 8 voices for richness
input_signal = impact_noise * 60;

resonated = input_signal <:
    fi.resonbp(f_sub, q_sub, 1.2),   // Sub body
    fi.resonbp(f1, q1, 1.0),          // Fundamental
    fi.resonbp(f2, q2, 0.8),          // Detuned
    fi.resonbp(f3, q3, 0.7),          // Third
    fi.resonbp(f4, q4, 0.5),          // Tension
    fi.resonbp(f5, q5, 0.4),          // Golden
    fi.resonbp(f6, q6, 0.3),          // Bright
    fi.resonbp(f_high, q_high, 0.25)  // Attack click
    :> + * 3;

// 5. Dynamic Pitch Wobble (The "Rattle")
// Subtle pitch variations as particles bounce
pitch_wobble = no.noise : ba.sAndH(seed_impulse) * chaos * 80;
// Wider bandwidth filter to let more resonance through
wobble_filtered = resonated : fi.resonbp(base_freq + pitch_wobble, 1.5, 1.0);

// 6. Add body with low shelf boost
body_boost = wobble_filtered : fi.lowshelf(2, 3, base_freq * 0.7);

// 7. Final Mix
// Blend between raw impacts and full resonated sound
dry_signal = impact_noise * 80;
wet_signal = body_boost * 2.5;

mixed = dry_signal * (1 - mix) + wet_signal * mix;

// 8. Output Processing
// Subtle saturation for warmth, then soft limit
saturated = mixed * level * 4 : *(1.2) : ma.tanh;
output = saturated : fi.dcblocker;

// Stereo spread - decorrelate L/R slightly
spread_amount = 0.2;
left = output * (0.5 + spread_amount * no.noise : si.smooth(0.999));
right = output * (0.5 - spread_amount * no.noise : si.smooth(0.999));

process = left, right;
