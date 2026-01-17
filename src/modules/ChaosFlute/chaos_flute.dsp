// Chaos Flute - Physical Model with Overblow Chaos
// A "circuit bent" flute with internal breath envelope
// Push it into screaming multiphonics with the Pressure control

import("stdfaust.lib");
import("physmodels.lib");

declare name "Chaos Flute";
declare author "WiggleRoom";
declare description "Physical flute model with chaos and overblow";

// ==========================================================
// 1. CONTROLS (alphabetical for Faust parameter ordering)
// ==========================================================

// Breath Envelope - Attack time
attack = hslider("attack", 0.05, 0.001, 0.5, 0.001) : si.smoo;

// Gate input (triggers the "Lung")
gate = hslider("gate", 0, 0, 10, 0.01);

// Growl: Adds noise to breath (singing while playing)
growl = hslider("growl", 0, 0, 1, 0.01) : si.smoo;

// Mouth Position: Embouchure position (timbre/chaos control)
// Lower values = more unstable/chaotic
// Default 0.35 is slightly off-center for better initial resonance
mouth = hslider("mouth", 0.35, 0.1, 0.9, 0.01) : si.smoo;

// Pressure: How hard you blow. > 0.8 overblows, > 1.0 screams
pressure = hslider("pressure", 0.6, 0, 1.5, 0.01) : si.smoo;

// Breath Envelope - Release time
release = hslider("release", 0.5, 0.01, 5.0, 0.01) : si.smoo;

// Reverb mix: 0 = dry, 1 = full wet
reverb_mix = hslider("reverb", 0.3, 0, 1, 0.01) : si.smoo;

// Pitch (V/Oct input, 0V = C4)
volts = hslider("volts", 0, -5, 10, 0.001);
// Clamp frequency to reasonable range for flute (80Hz to 4kHz)
freq = max(80, min(4000, 261.62 * (2.0 ^ volts)));

// ==========================================================
// 2. THE "LUNG" (Internal Breath Envelope)
// ==========================================================

// ASR envelope for breath with sustain at 1.0
// When gate is high, envelope opens; when released, it decays
// Note: gate > 0.5 ensures trigger works with standard 0-10V gate signals
// Additional smoothing prevents discontinuities in the physical model
breath_env_raw = en.asr(attack, 1.0, release, gate > 0.5);
breath_env = breath_env_raw : si.smooth(0.997);  // Extra smoothing for PM stability

// ==========================================================
// 3. THE BLOWER (Breath Signal with Noise)
// ==========================================================

// Vibrato for natural sound
vib_freq = 5.0;
vib_gain = 0.02;

// Breath noise cutoff (higher = brighter breath)
breath_cutoff = 2000 + growl * 6000;

// The blower creates a pressure signal with breath noise and vibrato
// CRITICAL: Flute models NEED breath noise to excite the resonator!
// But too much breath overpowers the tone - careful balance
min_breath = 0.03;  // Reduced: just enough to excite, not overpower
breath_gain = min_breath + growl * 0.15;

// blower(pressure, breathGain, breathCutoff, vibratoFreq, vibratoGain)
blow_signal = pm.blower(
    breath_env * pressure,  // Envelope-shaped pressure
    breath_gain,            // Breath noise (always present + growl boost)
    breath_cutoff,          // Breath noise cutoff
    vib_freq,               // Vibrato frequency
    vib_gain                // Vibrato gain
);

// ==========================================================
// 4. THE FLUTE MODEL
// ==========================================================

// Calculate tube length in METERS from frequency
// Physical formula: L = speed_of_sound / (2 * frequency)
// speed of sound ≈ 340 m/s at room temperature
// For C4 (261.62 Hz): L = 340 / (2 * 261.62) ≈ 0.65 meters
speed_of_sound = 340.0;
tube_length = speed_of_sound / (2.0 * freq);

// fluteModel(tubeLength in meters, mouthPosition 0-1, pressure signal)
// Add stability: smooth tube length changes to prevent clicks
tube_length_smooth = tube_length : si.smoo;

// Physical model needs sufficient excitation energy to oscillate
// But not so much that it over-saturates and loses breath texture
excitation_boost = 1.5;  // Reduced: allows breath noise to remain audible
boosted_blow = blow_signal * excitation_boost;

// Add subtle chaos to mouth position - creates organic timbral movement
// This is what makes it a "Chaos" flute, not a static digital oscillator
chaos_lfo = no.lfnoise(3) : fi.lowpass(1, 5);  // Slow random movement
mouth_chaos = mouth + chaos_lfo * (1 - mouth) * 0.15 * pressure;  // More chaos at high pressure

flute_raw = pm.fluteModel(tube_length_smooth, mouth_chaos, boosted_blow);

// Frequency-dependent gain compensation
// Low frequencies produce more energy in waveguide models
// Scale down output at low pitches to prevent clipping
// Reference: 261.62 Hz (C4) = gain 1.0
freq_compensation = min(1.0, freq / 261.62);
flute_sound = flute_raw * freq_compensation;

// ==========================================================
// 5. OUTPUT PROCESSING
// ==========================================================

// Soft limiter using tanh for smooth saturation
soft_limit = ma.tanh;

// DC blocker - critical for physical models which can drift
dc_block = fi.dcblocker;

// High-pass to remove any sub-bass rumble from the physical model
rumble_filter = fi.highpass(2, 40);

// Low-pass filter to tame harsh high-end digital artifacts
// But allow enough highs for "breath" character
brightness_filter = fi.lowpass(1, 8000 + pressure * 6000);

// Attack smoothing to prevent onset clicks (~2ms ramp)
// Uses a faster smoother to preserve transients while eliminating clicks
attack_smooth = si.smooth(0.99);

// Output gain to bring to modular levels
// Reduced from 8.0 to prevent clipping at extreme settings
output_gain = 5.0;

// ==========================================================
// 6. REVERB
// ==========================================================

// Zita Rev1 stereo reverb
reverb_processor = re.zita_rev1_stereo(0, 200, 6000, 3, 4, 44100);

// Dry/wet mixer for stereo
dry_wet_mix(dry_l, dry_r, wet_l, wet_r) =
    dry_l * (1 - reverb_mix) + wet_l * reverb_mix,
    dry_r * (1 - reverb_mix) + wet_r * reverb_mix;

// Final processing chain: flute -> dc block -> filters -> attack smooth -> soft limit -> gain -> reverb
dry_signal = flute_sound : dc_block : rumble_filter : brightness_filter : attack_smooth : soft_limit : *(output_gain);
process = dry_signal <: (_, _), reverb_processor : dry_wet_mix;
