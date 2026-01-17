// Plucked String - Karplus-Strong Physical Model
// Creates guitar, harp, and plucked instrument sounds
// Uses filtered delay feedback for string resonance

import("stdfaust.lib");

declare name "Plucked String";
declare author "WiggleRoom";
declare description "Karplus-Strong plucked string synthesizer";

// --- INPUTS ---
// Pitch (V/Oct input, 0V = C4)
volts = hslider("volts", 0, -5, 10, 0.001);
freq = max(20, 261.62 * (2.0 ^ volts));  // Clamp minimum frequency

// Gate input (triggers pluck when > 1V)
gate = hslider("gate", 0, 0, 10, 0.01);

// --- CONTROLS ---
// Damping: 0 = bright/long sustain, 1 = muted/short decay
damping = hslider("damping", 0.3, 0, 1, 0.001) : si.smoo;

// Position: where on the string we pluck (affects harmonics)
// 0 = near bridge (bright), 1 = near middle (mellow)
position = hslider("position", 0.5, 0.01, 0.99, 0.01) : si.smoo;

// Brightness: excitation brightness
brightness = hslider("brightness", 0.5, 0, 1, 0.01) : si.smoo;

// Chorus mix: 0 = dry, 1 = full chorus
chorus_mix = hslider("chorus", 0.3, 0, 1, 0.01) : si.smoo;

// Reverb mix: 0 = dry, 1 = full wet
reverb_mix = hslider("reverb", 0.3, 0, 1, 0.01) : si.smoo;

// Strength: how hard we pluck (affects amplitude and brightness)
// 0 = soft/gentle, 1 = hard/aggressive
strength = hslider("strength", 0.7, 0, 1, 0.01) : si.smoo;

// --- TRIGGER DETECTION ---
// Rising edge detection: triggers when gate crosses 0.5V threshold
trig = (gate > 0.5) & (gate' <= 0.5);

// --- EXCITER ---
// Noise burst filtered by position (pluck position affects initial spectrum)
// Near bridge = brighter, near middle = more fundamental
// Strength adds brightness (harder plucks excite more harmonics)
effective_brightness = min(1, brightness + strength * 0.3);
excite_filter = fi.lowpass(2, 1000 + effective_brightness * 14000);

// Strength controls amplitude: soft pluck = 0.3, hard pluck = 1.0
pluck_amplitude = 0.3 + strength * 0.7;
noise_burst = no.noise : excite_filter * en.ar(0.001, 0.005 + (1-brightness) * 0.02, trig) * pluck_amplitude;

// --- STRING MODEL ---
// Karplus-Strong: delay line with filtered feedback

// Delay length in samples (clamped to valid range)
// Max delay of 4096 samples limits lowest frequency to ~12Hz at 48kHz
delay_samples = min(4000, max(2, ma.SR / freq - 0.5));

// Damping filter: lowpass in feedback loop
// Lower damping = brighter, longer sustain
// Higher damping = darker, shorter decay
damp_freq = 500 + (1 - damping) * 15000;
damp_filter = fi.lowpass(1, damp_freq);

// Position comb filter: notches out harmonics based on pluck position
// This simulates where on the string you're plucking
pos_delay = min(3999, max(1, delay_samples * position));
position_filter = _ <: _, @(int(pos_delay)) :> /(2);

// Feedback coefficient (slightly less than 1 for decay)
feedback_coef = 0.998 - damping * 0.015;

// The string loop: excitation -> delay -> filter -> feedback
string = + ~ (de.fdelay4(4096, delay_samples) : damp_filter : *(feedback_coef));

// --- DC BLOCKER ---
dc_block = fi.dcblocker;

// --- CHORUS ---
// Stereo chorus with deeper modulation for more audible effect
// Two LFOs with different rates create movement
chorus_lfo1 = os.osc(0.7) * 0.008 + os.osc(2.3) * 0.003;   // Slow + medium LFO
chorus_lfo2 = os.osc(0.9) * 0.008 + os.osc(2.7) * 0.003;   // Offset rates for stereo

// Base delays: 12ms and 18ms for stereo width
chorus_delay_L = de.fdelay(4096, max(1, 0.012 * ma.SR * (1 + chorus_lfo1)));
chorus_delay_R = de.fdelay(4096, max(1, 0.018 * ma.SR * (1 + chorus_lfo2)));

// Second voice with different delays for thicker sound
chorus_delay_L2 = de.fdelay(4096, max(1, 0.020 * ma.SR * (1 - chorus_lfo2 * 0.7)));
chorus_delay_R2 = de.fdelay(4096, max(1, 0.025 * ma.SR * (1 - chorus_lfo1 * 0.7)));

// Stereo chorus: 2 modulated delay voices per channel
stereo_chorus(sig) = sig * (1 - chorus_mix) + wet_L * chorus_mix,
                     sig * (1 - chorus_mix) + wet_R * chorus_mix
with {
    wet_L = (sig : chorus_delay_L) * 0.6 + (sig : chorus_delay_L2) * 0.4;
    wet_R = (sig : chorus_delay_R) * 0.6 + (sig : chorus_delay_R2) * 0.4;
};

// --- REVERB ---
// Simple stereo reverb using freeverb algorithm
reverb_processor = re.zita_rev1_stereo(0, 200, 6000, 3, 4, 44100);

// Dry/wet mixer for stereo
dry_wet_mix(dry_l, dry_r, wet_l, wet_r) =
    dry_l * (1 - reverb_mix) + wet_l * reverb_mix,
    dry_r * (1 - reverb_mix) + wet_r * reverb_mix;

// --- OUTPUT ---
// Apply position filter to excitation, then into string model, then chorus, then reverb
dry_signal = noise_burst : position_filter : string : dc_block;

// Stereo reverb: split input to dry and wet paths, then mix
add_reverb = _,_ <: (_,_), reverb_processor : mix_drywet
with {
    mix_drywet(dryL, dryR, wetL, wetR) =
        dryL * (1 - reverb_mix) + wetL * reverb_mix,
        dryR * (1 - reverb_mix) + wetR * reverb_mix;
};

// Chorus produces stereo (L, R), then feed into reverb
process = dry_signal : stereo_chorus : add_reverb;
