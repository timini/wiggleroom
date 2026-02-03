// acid9voice.dsp - ACID9Voice main DSP file
// WiggleRoom VCV Rack plugin
//
// A TB-303 inspired acid synthesizer voice with modern enhancements:
// - Tri-core morphing oscillator with ISOTOPE detune/spread
// - Sub oscillator (sine/square switchable)
// - Pre-filter grit saturation
// - Dual filters (ACID diode ladder / LEAD OTA)
// - Filter envelope with accent boost
// - Insert loop (send/return)
// - Stereo delay with clock sync and ghost mode
// - Slide (portamento) on pitch

import("stdfaust.lib");

// Import custom libraries
import("lib/morph_osc.lib");
import("lib/tri_core_osc.lib");
import("lib/acid_filter.lib");
import("lib/acid_delay.lib");

// ============================================================================
// PARAMETERS (alphabetically ordered for Faust indexing)
// ============================================================================

accent = hslider("accent", 0, 0, 10, 0.001);
cutoff = hslider("cutoff", 1000, 20, 20000, 1) : si.smoo;
cutoff_cv = hslider("cutoff_cv", 0, -5, 5, 0.001);
decay = hslider("decay", 0.3, 0.01, 2.0, 0.001);
delay_fb = hslider("delay_fb", 0.3, 0, 0.95, 0.001);
delay_ghost = hslider("delay_ghost", 0, 0, 1, 0.001);
delay_mix = hslider("delay_mix", 0, 0, 1, 0.001);
delay_ratio = hslider("delay_ratio", 2, 0, 4, 1);
delay_time = hslider("delay_time", 250, 10, 2000, 1);
env_mod = hslider("env_mod", 0.5, -1, 1, 0.001);
filter_mode = hslider("filter_mode", 0, 0, 1, 0.001);
fm_in = hslider("fm_in", 0, -5, 5, 0.001);
gate = hslider("gate", 0, 0, 10, 0.001);
grit = hslider("grit", 0, 0, 1, 0.001);
isotope = hslider("isotope", 0, 0, 1, 0.001);
resonance = hslider("resonance", 0.3, 0, 1, 0.001) : si.smoo;
return_connected = hslider("return_connected", 0, 0, 1, 1);
return_in_l = hslider("return_in_l", 0, -10, 10, 0.001);
return_in_r = hslider("return_in_r", 0, -10, 10, 0.001);
shape = hslider("shape", 0, 0, 1, 0.001);
slide = hslider("slide", 0, 0, 10, 0.001);
sub_level = hslider("sub_level", 0, 0, 1, 0.001);
sub_mode = hslider("sub_mode", 0, 0, 1, 1);
volts = hslider("volts", 0, -5, 10, 0.001);

// ============================================================================
// PITCH PROCESSING WITH SLIDE
// ============================================================================

slide_time = select2(slide > 0.9, 0.001, 0.06);
pitched_volts = volts : si.smooth(ba.tau2pole(slide_time));

// V/Oct to frequency (0V = C4 = 261.62 Hz)
base_freq = 261.62 * (2.0 ^ (pitched_volts + fm_in * 0.1));
freq = max(20, min(base_freq, 20000));

// ============================================================================
// ENVELOPE PROCESSING
// ============================================================================

// Smooth the gate to prevent clicks
gate_smooth = gate : si.smooth(ba.tau2pole(0.002));
gate_on = gate_smooth > 0.9;
accent_on = accent > 0.9;

// Filter envelope (AD shape) - slightly slower attack for organic feel
attack_time = 0.008;  // 8ms attack
filter_env_raw = en.adsr(attack_time, decay, 0, 0.05, gate_on);
accent_boost_filt = select2(accent_on, 1.0, 1.5);
filter_env_val = filter_env_raw * accent_boost_filt;

// VCA envelope - match filter attack to prevent clicks
// Longer attack (8ms) and release (150ms) for smoother sound
vca_env_raw = en.adsr(0.008, 0.15, 0.7, 0.15, gate_on);
accent_boost_vca = select2(accent_on, 1.0, 1.3);
vca_env_val = vca_env_raw * accent_boost_vca;

// ============================================================================
// FILTER MODULATION
// ============================================================================

cv_mod = cutoff * (2.0 ^ (cutoff_cv));
env_mod_hz = cutoff * env_mod * 4 * filter_env_val;
final_cutoff = max(20, min(cv_mod + env_mod_hz, 20000));

// ============================================================================
// MAIN PROCESS
// ============================================================================

pwm = 0.5;

// Generate stereo oscillator signal (attenuated to prevent downstream clipping)
osc_stereo = tri_core_osc(freq, shape, pwm, isotope, sub_level, sub_mode) : (*(0.6), *(0.6));

// Grit saturation (stereo)
grit_stereo(l, r) = grit_sat(grit, l), grit_sat(grit, r);

// Filter stage (stereo)
filter_stereo(l, r) = dual_filter(final_cutoff, resonance, filter_mode, l),
                      dual_filter(final_cutoff, resonance, filter_mode, r);

// VCA stage (stereo)
vca_stereo(l, r) = l * vca_env_val, r * vca_env_val;

// Insert loop (stereo in, stereo + mono send out)
insert_stereo(l, r) = post_l, post_r, send
with {
    post_l = insert_loop(l, return_in_l, return_connected);
    post_r = insert_loop(r, return_in_r, return_connected);
    send = (l + r) * 0.5;
};

// Delay stage (stereo in + send, stereo + send out)
delay_stereo(l, r, send) = (l, r : stereo_delay_simple(delay_time, delay_fb, delay_mix, delay_ghost)), send;

// Simple stereo delay (no feedback cross-routing)
stereo_delay_simple(dt, fb, mix, ghost, l, r) = l_out, r_out
with {
    dt_ms = max(10, min(dt, 2000));
    delay_samps = dt_ms * ma.SR / 1000.0;
    fb_amt = min(fb, 0.95);
    mix_smooth = mix : si.smoo;

    // HP filter for ghost mode
    ghost_hp(sig) = sig : fi.highpass(2, 80 + ghost * 400);

    // Left delay
    wet_l = l : (+ : de.fdelay(192000, delay_samps) : ghost_hp) ~ (*(fb_amt));

    // Right delay with slight offset
    delay_samps_r = delay_samps * 1.05;
    wet_r = r : (+ : de.fdelay(192000, delay_samps_r) : ghost_hp) ~ (*(fb_amt));

    // Mix
    l_out = l * (1 - mix_smooth) + wet_l * mix_smooth;
    r_out = r * (1 - mix_smooth) + wet_r * mix_smooth;
};

// Output stage (gain staging + DC block + soft limit)
// Attenuate before limiter to prevent harsh clipping
output_gain = 0.7;
output_stereo(l, r, send) = (l * output_gain : fi.dcblocker : ma.tanh),
                            (r * output_gain : fi.dcblocker : ma.tanh),
                            (send * output_gain);

// Main process: chain all stages
process = osc_stereo : grit_stereo : filter_stereo : vca_stereo : insert_stereo : delay_stereo : output_stereo;
