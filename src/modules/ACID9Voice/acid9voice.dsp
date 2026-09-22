// acid9voice.dsp - ACID9Voice main DSP file
// WiggleRoom VCV Rack plugin
//
// A TB-303 inspired acid synthesizer voice with modern enhancements:
// - Tri-core morphing oscillator with ISOTOPE detune/spread
// - Sub oscillator (sine/square switchable)
// - Pre-filter grit saturation
// - Dual filters (ACID diode ladder / LEAD TPT ladder)
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

accent_amount = hslider("accent_amount", 0.5, 0, 1, 0.001);
note_trigger = button("note_trigger");
slide_time_ms = hslider("slide_time_ms", 60, 10, 200, 1);
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

slide_time = select2(slide > 0.9, 0.001, slide_time_ms / (1000*log(10)));
pitched_volts = volts : si.smooth(ba.tau2pole(slide_time));

// V/Oct to frequency (0V = C4 = 261.62 Hz)
base_freq = 261.62 * (2.0 ^ (pitched_volts + fm_in * 0.1));
freq = max(20, min(base_freq, 20000));

// ============================================================================
// ENVELOPE PROCESSING
// ============================================================================

// The wrapper supplies the tied internal gate, latched accent and a one-sample
// trigger only for untied notes. Envelopes attack from their current level.
gate_on = gate > .9;
accent_on = (accent > .9) & (accent_amount > 0);
continuous_env(trig,hold,at,dt,rt) = step ~ _
with {
    elapsed = (+(1) : min(ma.SR*4) : *(1-trig)) ~ _;
    seen = max(trig) ~ _;
    step(y) = select2(hold & seen, y*exp(-1/(rt*ma.SR)),
        select2(elapsed<at*ma.SR, y*exp(-1/(dt*ma.SR)), min(1,y+1/(at*ma.SR))));
};
main_decay = select2(accent_on,decay,.06);
filter_env_raw = continuous_env(note_trigger,1,.001,main_decay,.003);
vca_env_raw = continuous_env(note_trigger,gate_on,.003,1.2,.003);
// Accent sweep: capacitor charged through 47k + resonance*100k, discharged
// through 100k, C=1uF. Resonance moves the mix from direct MEG to stored charge.
accent_drive = filter_env_raw*accent_on*accent_amount;
accent_cap = charge ~ _
with {
    charge(c) = c + max(0,accent_drive-c)/(ma.SR*(.047+.1*resonance)) - c/(ma.SR*.1);
};
accent_sweep = (1-resonance)*max(0,(100.0/147)*accent_drive-accent_cap)+resonance*accent_cap;
accent_vca = accent_drive : si.smooth(ba.tau2pole(.001551));
// Preserve the short release instead of multiplying the whole voice by a gate.
// Accent contribution releases through the same continuous amplitude envelope.
vca_env_val = vca_env_raw*(1+.3*accent_vca) : si.smooth(ba.tau2pole(.001));
filter_env_val = filter_env_raw : si.smooth(ba.tau2pole(.001));
final_cutoff = max(20,min(cutoff*pow(2,cutoff_cv+4*env_mod*filter_env_val+2*accent_sweep),20000));

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
    wet_l = l : (+ : de.fdelay(403200, delay_samps) : ghost_hp) ~ (*(fb_amt));

    // Right delay with slight offset
    delay_samps_r = delay_samps * 1.05;
    wet_r = r : (+ : de.fdelay(403200, delay_samps_r) : ghost_hp) ~ (*(fb_amt));

    // Mix
    l_out = l * (1 - mix_smooth) + wet_l * mix_smooth;
    r_out = r * (1 - mix_smooth) + wet_r * mix_smooth;
};

// Output stage (gain staging + DC block + soft limit)
// Attenuate before limiter to prevent harsh clipping
output_gain = 0.7;
output_stereo(l, r, send) = (l * output_gain : fi.dcblocker : ma.tanh),
                            (r * output_gain : fi.dcblocker : ma.tanh),
                            (send * output_gain : fi.dcblocker : ma.tanh);

// Main process: chain all stages
process = osc_stereo : grit_stereo : filter_stereo : vca_stereo : insert_stereo : delay_stereo : output_stereo;
