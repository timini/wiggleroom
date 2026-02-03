// VektorX - Complex Drone Synthesizer
// 4-oscillator cluster scanning synthesis with organic movement
// Inspired by Forge TME VHIKK X
//
// Features:
// - Cluster Scanning synthesis (4 morphing oscillators with frequency spread)
// - Three algorithm modes: Virtual Analog, Cluster, Diatonic
// - Field filter with tilt control (LP/BP/HP morphing)
// - Diffuse reverb processor with feedback

declare name "VektorX";
declare author "WiggleRoom";
declare description "Complex drone synthesizer inspired by Forge TME VHIKK X";
declare version "1.0";

import("stdfaust.lib");

// Import VHIKK component libraries (vh_ prefix to avoid conflicts with stdfaust)
vh_mo = library("vhikk_morph_osc.lib");
vh_co = library("vhikk_cluster_osc.lib");
vh_ff = library("vhikk_field_filter.lib");
vh_sc = library("vhikk_soft_clip.lib");

// ==========================================================
// CONTROLS (alphabetical for Faust parameter ordering)
// ==========================================================

// Algorithm selection (0=Analog, 1=Cluster, 2=Diatonic)
algorithm = nentry("algorithm", 0, 0, 2, 1);

// Processor decay time (controls feedback amount)
decay = hslider("decay", 0.5, 0, 1, 0.01) : si.smoo;

// Delay/reverb dry/wet mix (default 40% for audible reverb)
delay_mix = hslider("delay_mix", 0.4, 0, 1, 0.01) : si.smoo;

// VCA drive (saturation amount)
drive = hslider("drive", 1.0, 1.0, 3.0, 0.01) : si.smoo;

// Field filter cutoff frequency
field = hslider("field", 1000, 20, 15000, 1) : si.smoo;

// Gate input (0-10V, threshold at 0.9V)
gate = hslider("gate", 0, 0, 10, 0.01);

// Waveform morph (0=sine, 0.5=saw, 1=square)
morph = hslider("morph", 0.5, 0, 1, 0.01) : si.smoo;

// Filter resonance
resonance = hslider("resonance", 0.3, 0, 0.95, 0.01) : si.smoo;

// Cluster frequency spread (default higher for immediate drone character)
spread = hslider("spread", 0.5, 0, 1, 0.01) : si.smoo;

// Filter tilt (-1=LP, 0=BP, +1=HP)
tilt = hslider("tilt", 0, -1, 1, 0.01) : si.smoo;

// Master VCA level
vca = hslider("vca", 1, 0, 1, 0.01) : si.smoo;

// V/Oct pitch input (0V = C4 = 261.62Hz)
volts = hslider("volts", 0, -5, 5, 0.001);

// Cross-modulation amount for Analog mode (-1=FM, +1=RM)
warp = hslider("warp", 0, -1, 1, 0.01) : si.smoo;

// Stereo width for cluster oscillator
width = hslider("width", 0.5, 0, 1, 0.01) : si.smoo;

// ==========================================================
// INTERNAL MODULATION (for organic drone movement)
// ==========================================================

// Slow LFOs for pitch drift and movement (MUCH more audible for drone character)
// Faster LFOs so movement is perceptible in shorter clips
drift_lfo1 = os.osc(0.37) * 0.015;   // Strong pitch drift (~25 cents)
drift_lfo2 = os.osc(0.29) * 0.012;   // Secondary drift for stereo
drift_lfo3 = os.osc(0.47) * 0.008;   // Third drift for complexity
mod_lfo = os.osc(0.53) * 0.25;       // Morph modulation (more audible)
amp_lfo = os.osc(0.41) * 0.1 + 1.0;  // Amplitude swell (0.9-1.1)

// ==========================================================
// DERIVED VALUES
// ==========================================================

// Convert V/Oct to frequency (0V = C4) with organic drift
freq_base = max(20, min(10000, 261.62 * (2.0 ^ volts)));
freq = freq_base * (1.0 + drift_lfo1 + drift_lfo3 * 0.5);  // Multiple LFOs for complex drift

// Gate detection (threshold at 0.9V for test compatibility)
gate_active = gate > 0.9;

// Exponential envelope for smooth drone swell (150ms attack, 1s release)
// Using si.smooth with slower time constant for truly click-free onset
env_raw = en.asr(0.15, 1.0, 1.0, gate_active);
// Double smoothing with different time constants for exponential-like curve
env = env_raw : si.smooth(0.998) : si.smooth(0.997);

// Effective spread with higher minimum for immediate drone character
eff_spread = 0.25 + spread * 0.75;  // Min 25% spread always for wider sound

// ==========================================================
// SIGNAL CHAIN
// ==========================================================

// 1. GENERATOR - Native stereo throughout to maintain cluster width
// Algorithm 0: Virtual Analog - mono to stereo via wide decorrelation
analog_mono = vh_co.analog_osc(freq, morph + mod_lfo * 0.05, warp);
// Longer allpass delays (2048 samples ~46ms) for true stereo width
analog_l = analog_mono : fi.allpass_comb(2048, 557, 0.4);
analog_r = analog_mono : fi.allpass_comb(2048, 773, 0.4);

// Algorithm 1: Cluster (4 oscillators with frequency spread) - KEEP STEREO
cluster_stereo = vh_co.cluster_osc(freq, morph, eff_spread, width);
cluster_l = cluster_stereo : ba.selector(0, 2);
cluster_r = cluster_stereo : ba.selector(1, 2);

// Algorithm 2: Diatonic (chord intervals) - KEEP STEREO
diatonic_stereo = vh_co.cluster_osc_diatonic(freq, morph, eff_spread, width);
diatonic_l = diatonic_stereo : ba.selector(0, 2);
diatonic_r = diatonic_stereo : ba.selector(1, 2);

// Select algorithm using signal math (stereo output)
// Smooth selection to prevent clicks when switching
sel = int(algorithm);
is_analog = (sel == 0) : si.smooth(0.99);
is_cluster = (sel == 1) : si.smooth(0.99);
is_diatonic = (sel == 2) : si.smooth(0.99);

// Mix algorithms with amplitude modulation
gen_l = (analog_l * is_analog + cluster_l * is_cluster + diatonic_l * is_diatonic) * amp_lfo;
gen_r = (analog_r * is_analog + cluster_r * is_cluster + diatonic_r * is_diatonic) * (1.0 + drift_lfo2);

// 2. FILTER - Field filter with tilt control (stereo)
// DC block BEFORE filter to prevent any DC offset from causing clicks
filtered_l = vh_ff.field_filter(gen_l : fi.dcblocker, field, resonance, tilt);
filtered_r = vh_ff.field_filter(gen_r : fi.dcblocker, field, resonance, tilt);

// 3. VCA - Anti-alias filter before saturation, then soft clip with envelope
// Lowpass at 8kHz before distortion to reduce aliasing (darker, warmer sound)
antialias = fi.lowpass(2, 8000);
vca_l = filtered_l * env * vca : antialias : vh_sc.soft_clip(_, drive) : fi.dcblocker;
vca_r = filtered_r * env * vca : antialias : vh_sc.soft_clip(_, drive) : fi.dcblocker;

// 4. PROCESSOR - Diffuse reverb with feedback
// Uses allpass filters for diffusion and feedback delay for sustain
feedback_amt = 0.4 + decay * 0.5;  // 40-90% feedback for long drone tails
diffusion_amt = 0.5 + decay * 0.3;  // More diffusion with longer decay

// Allpass diffusers (prime number delays for smearing)
ap1(x) = x : fi.allpass_comb(2048, 113, diffusion_amt);
ap2(x) = x : fi.allpass_comb(2048, 337, diffusion_amt);
ap3(x) = x : fi.allpass_comb(2048, 241, diffusion_amt);
ap4(x) = x : fi.allpass_comb(2048, 421, diffusion_amt);

// Delay times based on decay (longer decay = longer delays)
delay_time_l = int(1000 + decay * 15000);  // 1000-16000 samples
delay_time_r = int(1100 + decay * 16500);  // Slightly different for stereo

// High frequency damping in feedback path (gentler to preserve tail)
damping_filter(x) = x : fi.lowpass(1, 6000 + (1.0 - decay) * 10000);

// Reverb processing with feedback loop
reverb_l = vca_l : ap1 : ap2 : (+ ~ (de.delay(22000, delay_time_l) : damping_filter : *(feedback_amt))) : ap3;
reverb_r = vca_r : ap3 : ap4 : (+ ~ (de.delay(22000, delay_time_r) : damping_filter : *(feedback_amt))) : ap4;

// 5. DRY/WET MIX - Equal power crossfade for smooth blending
// sqrt curves maintain perceived loudness during crossfade
dry_gain = sqrt(1.0 - delay_mix);
wet_gain = sqrt(delay_mix);
mixed_l = vca_l * dry_gain + reverb_l * wet_gain;
mixed_r = vca_r * dry_gain + reverb_r * wet_gain;

// 6. OUTPUT PROCESSING
// DC blocker and final limiting
dc_block = fi.dcblocker;
output_limit = ma.tanh;

// Output gain to reach modular levels (~5V peak) - reduced for cleaner sound
output_gain = 0.65;

// Final stereo output
process = mixed_l, mixed_r :
    par(i, 2, dc_block) :
    par(i, 2, output_limit) :
    par(i, 2, *(output_gain));
