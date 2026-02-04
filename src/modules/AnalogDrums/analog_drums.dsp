// TR-808 Virtual Analog Drum Machine
// Component-level modeling of Roland TR-808
// 12 drum voices with circuit bending modifications

import("stdfaust.lib");

declare name "TR-808";
declare author "WiggleRoom";
declare description "Virtual analog TR-808 drum machine with circuit bending";

// Import our circuit library
import("tr808_lib.lib");

//=====================================================================
// BASS DRUM (BD)
// Bridged-T resonator with pitch envelope
// Circuit bend: long_decay for self-oscillation, drive for saturation
//=====================================================================

bd_trig = hslider("bd_trig", 0, 0, 10, 0.001);
bd_decay = hslider("bd_decay", 0.5, 0, 1, 0.001) : si.smoo;
bd_tune = hslider("bd_tune", 0, -1, 1, 0.001) : si.smoo;
bd_tone = hslider("bd_tone", 0.5, 0, 1, 0.001) : si.smoo;
bd_click = hslider("bd_click", 0.3, 0, 1, 0.001) : si.smoo;
bd_long_decay = hslider("bd_long_decay", 0, 0, 1, 0.001) : si.smoo;  // Circuit bend
bd_drive = hslider("bd_drive", 0, 0, 1, 0.001) : si.smoo;  // Circuit bend

bd_trigger = (bd_trig > 0.9) & (bd_trig' <= 0.9);

bd_voice = output
with {
    // Base frequency: 30-80 Hz based on tune
    base_freq = 45 * (2 ^ (bd_tune * 0.5));

    // Pitch envelope: sweeps down from higher frequency
    pitch_env = en.ar(0.001, 0.03 + bd_tone * 0.02, bd_trigger);
    sweep_freq = base_freq * (1 + pitch_env * (2 + bd_click * 4));

    // Main resonator with decay control
    // Long_decay extends the Q for self-oscillation territory
    effective_decay = bd_decay + bd_long_decay * (1 - bd_decay);
    q = 10 + effective_decay * 300;
    resonator = fi.resonbp(sweep_freq, q, 1);

    // Amplitude envelope
    amp_env = en.ar(0.001, 0.1 + bd_decay * 0.9 + bd_long_decay * 2, bd_trigger);

    // Click transient (noise burst)
    click = no.noise : fi.lowpass(1, 200) * en.ar(0.0001, 0.005, bd_trigger) * bd_click * 2;

    // Combine: click excites resonator, then add direct click
    excite = click + (bd_trigger : ba.impulsify) * 2;
    body = excite : resonator * amp_env;

    // Tone filter (lowpass) - raised minimum for fuller sound
    tone_freq = 200 + bd_tone * 600;
    filtered = body : fi.lowpass(1, tone_freq);

    // Drive (saturation)
    driven = filtered : hard_clip(1 + bd_drive * 4);

    // Output with compression and level boost
    output = driven : drum_channel(30);
};

//=====================================================================
// SNARE DRUM (SD)
// Two bridged-T resonators + HPF noise
// Circuit bend: snap_filter frequency, shell_detune
//=====================================================================

sd_trig = hslider("sd_trig", 0, 0, 10, 0.001);
sd_tune = hslider("sd_tune", 0, -1, 1, 0.001) : si.smoo;
sd_tone = hslider("sd_tone", 0.5, 0, 1, 0.001) : si.smoo;
sd_snappy = hslider("sd_snappy", 0.5, 0, 1, 0.001) : si.smoo;
sd_snap_decay = hslider("sd_snap_decay", 0.3, 0, 1, 0.001) : si.smoo;
sd_snap_filter = hslider("sd_snap_filter", 0.5, 0, 1, 0.001) : si.smoo;  // Circuit bend
sd_detune = hslider("sd_detune", 0, 0, 1, 0.001) : si.smoo;  // Circuit bend

sd_trigger = (sd_trig > 0.9) & (sd_trig' <= 0.9);

sd_voice = output
with {
    // Two resonator frequencies (shell)
    base_freq = 180 * (2 ^ (sd_tune * 0.3));
    freq1 = base_freq * (1 - sd_detune * 0.1);
    freq2 = base_freq * 1.5 * (1 + sd_detune * 0.15);  // ~5th apart

    // Shell resonators
    shell_env = en.ar(0.001, 0.08 + sd_tone * 0.15, sd_trigger);
    shell1 = fi.resonbp(freq1, 15, 1);
    shell2 = fi.resonbp(freq2, 12, 1);

    // Excite with impulse
    excite = (sd_trigger : ba.impulsify) * 2;
    shell = (excite : shell1) + (excite : shell2) * 0.7;
    shell_out = shell * shell_env * (1 - sd_snappy * 0.5);

    // Snappy noise (snare wires)
    snap_hpf_freq = 2000 + sd_snap_filter * 6000;
    snap_env = en.ar(0.001, 0.05 + sd_snap_decay * 0.2, sd_trigger);
    snap = no.noise : fi.highpass(2, snap_hpf_freq) * snap_env * sd_snappy * 1.5;

    // Mix shell and snap with compression
    output = (shell_out + snap) : drum_channel(30);
};

//=====================================================================
// TOM DRUMS (LT, MT, HT)
// Bridged-T resonators, same circuit different tuning
// Circuit bend: pitch_mod for disco tom sweep
//=====================================================================

// Low Tom
lt_trig = hslider("lt_trig", 0, 0, 10, 0.001);
lt_tune = hslider("lt_tune", 0, -1, 1, 0.001) : si.smoo;
lt_decay = hslider("lt_decay", 0.5, 0, 1, 0.001) : si.smoo;
lt_pitch_mod = hslider("lt_pitch_mod", 0, 0, 1, 0.001) : si.smoo;

lt_trigger = (lt_trig > 0.9) & (lt_trig' <= 0.9);

// Mid Tom
mt_trig = hslider("mt_trig", 0, 0, 10, 0.001);
mt_tune = hslider("mt_tune", 0, -1, 1, 0.001) : si.smoo;
mt_decay = hslider("mt_decay", 0.5, 0, 1, 0.001) : si.smoo;
mt_pitch_mod = hslider("mt_pitch_mod", 0, 0, 1, 0.001) : si.smoo;

mt_trigger = (mt_trig > 0.9) & (mt_trig' <= 0.9);

// High Tom
ht_trig = hslider("ht_trig", 0, 0, 10, 0.001);
ht_tune = hslider("ht_tune", 0, -1, 1, 0.001) : si.smoo;
ht_decay = hslider("ht_decay", 0.5, 0, 1, 0.001) : si.smoo;
ht_pitch_mod = hslider("ht_pitch_mod", 0, 0, 1, 0.001) : si.smoo;

ht_trigger = (ht_trig > 0.9) & (ht_trig' <= 0.9);

// Generic tom voice
tom_voice(base_f, tune, decay, pitch_mod, trig) = output
with {
    trigger = (trig > 0.9) & (trig' <= 0.9);

    // Frequency with pitch envelope (disco sweep)
    freq = base_f * (2 ^ (tune * 0.3));
    pitch_env = en.ar(0.001, 0.06, trigger);
    sweep_freq = freq * (1 + pitch_env * pitch_mod * 0.5);

    // Resonator
    q = 20 + decay * 80;
    resonator = fi.resonbp(sweep_freq, q, 1);

    // Amplitude envelope
    amp_env = en.ar(0.001, 0.15 + decay * 0.5, trigger);

    // Excite
    excite = (trigger : ba.impulsify) * 2;

    output = (excite : resonator * amp_env) : drum_channel(30);
};

lt_voice = tom_voice(100, lt_tune, lt_decay, lt_pitch_mod, lt_trig);
mt_voice = tom_voice(150, mt_tune, mt_decay, mt_pitch_mod, mt_trig);
ht_voice = tom_voice(200, ht_tune, ht_decay, ht_pitch_mod, ht_trig);

//=====================================================================
// COWBELL (CB)
// Two square oscillators (540 Hz + 800 Hz) through bandpass
// Circuit bend: detune for beating, filter Q
//=====================================================================

cb_trig = hslider("cb_trig", 0, 0, 10, 0.001);
cb_tune = hslider("cb_tune", 0, -1, 1, 0.001) : si.smoo;
cb_decay = hslider("cb_decay", 0.5, 0, 1, 0.001) : si.smoo;
cb_detune = hslider("cb_detune", 0, 0, 1, 0.001) : si.smoo;  // Circuit bend

cb_trigger = (cb_trig > 0.9) & (cb_trig' <= 0.9);

cb_voice = output
with {
    // Two square oscillators
    base_freq = 540 * (2 ^ (cb_tune * 0.2));
    freq1 = base_freq * (1 - cb_detune * 0.015);
    freq2 = base_freq * 1.481 * (1 + cb_detune * 0.015);  // 800/540 ratio

    osc1 = os.square(freq1);
    osc2 = os.square(freq2);
    oscs = (osc1 + osc2) * 0.5;

    // Bandpass filter for metallic tone
    filtered = oscs : fi.resonbp(base_freq * 1.2, 5, 1);

    // Amplitude envelope (two-stage for authentic 808 cowbell)
    env1 = en.ar(0.001, 0.02, cb_trigger);
    env2 = en.ar(0.001, 0.3 + cb_decay * 0.5, cb_trigger);
    amp_env = env1 * 0.6 + env2 * 0.4;

    output = (filtered * amp_env) : drum_channel(8);
};

//=====================================================================
// HI-HATS (CH, OH)
// 6-oscillator metallic bank with different decay times
// Choke group: CH chokes OH
// Circuit bend: spread (oscillator detuning)
//=====================================================================

ch_trig = hslider("ch_trig", 0, 0, 10, 0.001);
ch_tune = hslider("ch_tune", 0, -1, 1, 0.001) : si.smoo;
ch_decay = hslider("ch_decay", 0.3, 0, 1, 0.001) : si.smoo;
ch_spread = hslider("ch_spread", 0, 0, 1, 0.001) : si.smoo;  // Circuit bend

ch_trigger = (ch_trig > 0.9) & (ch_trig' <= 0.9);

oh_trig = hslider("oh_trig", 0, 0, 10, 0.001);
oh_tune = hslider("oh_tune", 0, -1, 1, 0.001) : si.smoo;
oh_decay = hslider("oh_decay", 0.6, 0, 1, 0.001) : si.smoo;
oh_spread = hslider("oh_spread", 0, 0, 1, 0.001) : si.smoo;  // Circuit bend

oh_trigger = (oh_trig > 0.9) & (oh_trig' <= 0.9);

// Metallic oscillator bank (6 square waves at enharmonic ratios)
hat_bank(base_freq, spread) = sum(i, 6, osc(i)) / 6.0
with {
    ratios = (1.0, 1.493, 1.790, 2.088, 2.634, 3.902);
    ratio(i) = ba.take(i + 1, ratios);
    detune(i) = 1 + (i - 2.5) * spread * 0.03;
    freq(i) = base_freq * ratio(i) * detune(i);
    osc(i) = os.square(freq(i));
};

// Closed hi-hat
ch_voice = output
with {
    base_freq = 205 * (2 ^ (ch_tune * 0.3));
    oscs = hat_bank(base_freq, ch_spread);

    // Highpass and bandpass filtering
    filtered = oscs : fi.highpass(2, 7000) : fi.lowpass(2, 12000);

    // Short decay
    amp_env = en.ar(0.001, 0.02 + ch_decay * 0.08, ch_trigger);

    output = (filtered * amp_env) : drum_channel(8);
};

// Open hi-hat (with choke from closed hat)
oh_voice = output
with {
    base_freq = 205 * (2 ^ (oh_tune * 0.3));
    oscs = hat_bank(base_freq, oh_spread);

    // Similar filtering
    filtered = oscs : fi.highpass(2, 7000) : fi.lowpass(2, 12000);

    // Longer decay, but choked by closed hat
    // Choke: when CH triggers, OH envelope resets to zero
    amp_env = en.ar(0.001, 0.15 + oh_decay * 0.6, oh_trigger) * (1 - ch_trigger);

    output = (filtered * amp_env) : drum_channel(8);
};

//=====================================================================
// CYMBAL (CY)
// 6-oscillator metallic bank with dual-band decay
// Circuit bend: hi_decay, lo_decay separate control
//=====================================================================

cy_trig = hslider("cy_trig", 0, 0, 10, 0.001);
cy_tune = hslider("cy_tune", 0, -1, 1, 0.001) : si.smoo;
cy_decay = hslider("cy_decay", 0.7, 0, 1, 0.001) : si.smoo;
cy_tone = hslider("cy_tone", 0.5, 0, 1, 0.001) : si.smoo;
cy_spread = hslider("cy_spread", 0.3, 0, 1, 0.001) : si.smoo;  // Circuit bend

cy_trigger = (cy_trig > 0.9) & (cy_trig' <= 0.9);

cy_voice = output
with {
    base_freq = 300 * (2 ^ (cy_tune * 0.3));
    oscs = hat_bank(base_freq, cy_spread);

    // Dual-band processing
    lo_band = oscs : fi.lowpass(2, 5000);
    hi_band = oscs : fi.highpass(2, 5000);

    // Different envelopes for each band
    lo_env = en.ar(0.001, 0.3 + cy_decay * 1.0, cy_trigger);
    hi_env = en.ar(0.001, 0.5 + cy_decay * 1.5, cy_trigger);

    // Mix based on tone
    mixed = lo_band * lo_env * (1 - cy_tone) + hi_band * hi_env * cy_tone;

    output = mixed : drum_channel(8);
};

//=====================================================================
// CLAP (CP)
// Noise with 3-burst sawtooth envelope
// Circuit bend: spread (burst timing)
//=====================================================================

cp_trig = hslider("cp_trig", 0, 0, 10, 0.001);
cp_reverb = hslider("cp_reverb", 0.5, 0, 1, 0.001) : si.smoo;
cp_spread = hslider("cp_spread", 0.3, 0, 1, 0.001) : si.smoo;  // Circuit bend
cp_tone = hslider("cp_tone", 0.5, 0, 1, 0.001) : si.smoo;

cp_trigger = (cp_trig > 0.9) & (cp_trig' <= 0.9);

cp_voice = output
with {
    // Noise source, bandpass filtered
    noise = no.noise : fi.bandpass(2, 1000 + cp_tone * 1500, 3500);

    // Use a simple counter-based approach for burst timing
    // When trigger arrives, generate 4 bursts at fixed intervals
    burst_window_ms = 100;  // 100ms total window
    burst_counter = ba.countdown(int(ma.SR * burst_window_ms / 1000), cp_trigger);
    burst_phase = 1 - burst_counter / max(1, ma.SR * burst_window_ms / 1000);

    // Spread controls burst timing: 0 = tight bursts, 1 = wide bursts
    burst_width = 0.2 + cp_spread * 0.3;  // 20-50% of window for bursts

    // Generate bursts at 0%, 25%, 50%, 75% of burst window
    in_burst_window = burst_phase < burst_width;
    burst_idx = int(burst_phase * 10);

    // Burst envelope - multiple short bursts
    burst_env = en.ar(0.0005, 0.01, cp_trigger) * select2(in_burst_window, 0.3, 1.0);

    // Reverb tail
    tail_env = en.ar(0.001, 0.08 + cp_reverb * 0.25, cp_trigger);

    // Combined envelope - burst emphasis then tail
    amp_env = burst_env * 0.5 + tail_env * 0.8;

    output = (noise * amp_env) : drum_channel(30);
};

//=====================================================================
// MARACAS (MA)
// High metallic oscillators with ASR envelope
// Circuit bend: filter control
//=====================================================================

ma_trig = hslider("ma_trig", 0, 0, 10, 0.001);
ma_decay = hslider("ma_decay", 0.2, 0, 1, 0.001) : si.smoo;
ma_tone = hslider("ma_tone", 0.7, 0, 1, 0.001) : si.smoo;

ma_trigger = (ma_trig > 0.9) & (ma_trig' <= 0.9);

ma_voice = output
with {
    // High-frequency filtered noise
    noise = no.noise : fi.highpass(2, 5000 + ma_tone * 5000);

    // Short ASR envelope
    amp_env = en.ar(0.001, 0.02 + ma_decay * 0.1, ma_trigger);

    output = (noise * amp_env) : drum_channel(8);
};

//=====================================================================
// RIMSHOT (RS)
// Triangle oscillator + bridged-T resonator
//=====================================================================

rs_trig = hslider("rs_trig", 0, 0, 10, 0.001);
rs_tune = hslider("rs_tune", 0, -1, 1, 0.001) : si.smoo;
rs_decay = hslider("rs_decay", 0.3, 0, 1, 0.001) : si.smoo;

rs_trigger = (rs_trig > 0.9) & (rs_trig' <= 0.9);

rs_voice = output
with {
    // High triangle tone
    tri_freq = 1700 * (2 ^ (rs_tune * 0.2));
    tri = os.triangle(tri_freq);

    // Lower bridged-T resonator for body
    body_freq = 500 * (2 ^ (rs_tune * 0.3));
    body = fi.resonbp(body_freq, 30, 1);

    // Envelopes
    tri_env = en.ar(0.0001, 0.003, rs_trigger);
    body_env = en.ar(0.0001, 0.02 + rs_decay * 0.05, rs_trigger);

    // Excite body resonator with impulse
    excite = (rs_trigger : ba.impulsify) * 2;
    body_out = excite : body * body_env;

    // Mix with compression
    output = (tri * tri_env + body_out) : drum_channel(30);
};

//=====================================================================
// MASTER SECTION
//=====================================================================

master_gain = hslider("master_gain", 0.8, 0, 1, 0.001) : si.smoo;
master_tone = hslider("master_tone", 0.7, 0, 1, 0.001) : si.smoo;
master_comp = hslider("master_comp", 0.5, 0, 1, 0.001) : si.smoo;

// Mix all voices - individual outputs are already compressed/normalized
voice_mix = (bd_voice + sd_voice +
             lt_voice + mt_voice + ht_voice +
             cb_voice + ch_voice + oh_voice + cy_voice +
             cp_voice + ma_voice + rs_voice) * 0.15;  // Headroom for 12 voices

// Master tone control
master_filter(x) = x : fi.lowpass(1, 2000 + master_tone * 18000);

// Master bus compressor for glue
// Slower attack to preserve transients, variable ratio based on comp knob
master_bus_comp = drum_comp(0.005, 0.15, 0.4, 2 + master_comp * 6, 1.5);

// Final master output: filter -> bus comp -> gain -> soft limit
master_out = voice_mix : master_filter : master_bus_comp * master_gain : soft_clip : dc_block;

//=====================================================================
// OUTPUT: 13 channels (12 individual + mix)
//=====================================================================

process = bd_voice, sd_voice, lt_voice, mt_voice, ht_voice,
          cb_voice, ch_voice, oh_voice, cy_voice,
          cp_voice, ma_voice, rs_voice, master_out;
