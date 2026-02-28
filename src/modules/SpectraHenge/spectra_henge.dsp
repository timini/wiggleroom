// SpectraHenge - 4-Channel Spatial/Spectral Mixer
// Each channel: input → SVF morph (LP/BP/HP) → equal-power pan → stereo bus
// Note: tanh soft limiter is applied in C++ (after send/return mixing)
//
// Parameters (alphabetical = Faust index order):
//   0: pan1   - Channel 1 stereo position (0=L, 1=R)
//   1: pan2   - Channel 2 stereo position
//   2: pan3   - Channel 3 stereo position
//   3: pan4   - Channel 4 stereo position
//   4: q      - SVF resonance (shared across all channels)
//   5: tilt1  - Channel 1 filter morph (0=LPF, 0.5=BPF, 1=HPF)
//   6: tilt2  - Channel 2 filter morph
//   7: tilt3  - Channel 3 filter morph
//   8: tilt4  - Channel 4 filter morph
//
// Inputs: 4 audio (in1, in2, in3, in4)
// Outputs: 2 (stereo L, R)

import("stdfaust.lib");

declare name "SpectraHenge";
declare author "WiggleRoom";
declare description "4-channel spatial/spectral mixer with SVF filter morphing and equal-power panning";

// ==========================================================
// PARAMETERS
// ==========================================================

pan1  = hslider("pan1",  0.25, 0, 1, 0.001) : si.smoo;
pan2  = hslider("pan2",  0.75, 0, 1, 0.001) : si.smoo;
pan3  = hslider("pan3",  0.25, 0, 1, 0.001) : si.smoo;
pan4  = hslider("pan4",  0.75, 0, 1, 0.001) : si.smoo;
q     = hslider("q",     1.0, 0.5, 10, 0.01) : si.smoo;
tilt1 = hslider("tilt1", 0.25, 0, 1, 0.001) : si.smoo;
tilt2 = hslider("tilt2", 0.75, 0, 1, 0.001) : si.smoo;
tilt3 = hslider("tilt3", 0.75, 0, 1, 0.001) : si.smoo;
tilt4 = hslider("tilt4", 0.25, 0, 1, 0.001) : si.smoo;

// ==========================================================
// SVF FILTER WITH MORPH (LP → BP → HP)
// ==========================================================

// Compute cutoff frequency from tilt value
// Bottom (tilt=0) → low cutoff, top (tilt=1) → high cutoff
// Exponential curve: 80 Hz to 16 kHz
tilt_to_freq(tilt) = 80.0 * (2.0 ^ (tilt * 7.6439));  // 80 * 2^7.6439 ≈ 16000

// State Variable Filter with LP/BP/HP morph
// tilt 0→0.5: crossfade LP→BP
// tilt 0.5→1: crossfade BP→HP
svf_morph(tilt, sig) = out
with {
    freq = tilt_to_freq(tilt);

    // SVF implementation using fi.svf
    // fi.svf returns (lp, bp, hp) but takes normalised freq and Q
    svf_lp = fi.svf.lp(freq, q, sig);
    svf_bp = fi.svf.bp(freq, q, sig);
    svf_hp = fi.svf.hp(freq, q, sig);

    // Morph crossfade
    lp_bp_mix = select2(tilt < 0.5,
        0.0,  // tilt >= 0.5: no LP/BP mixing needed
        tilt * 2.0  // tilt 0→0.5 maps to 0→1
    );
    bp_hp_mix = select2(tilt >= 0.5,
        0.0,  // tilt < 0.5: no BP/HP mixing needed
        (tilt - 0.5) * 2.0  // tilt 0.5→1 maps to 0→1
    );

    out = select2(tilt < 0.5,
        // tilt >= 0.5: crossfade BP → HP
        svf_bp * (1.0 - bp_hp_mix) + svf_hp * bp_hp_mix,
        // tilt < 0.5: crossfade LP → BP
        svf_lp * (1.0 - lp_bp_mix) + svf_bp * lp_bp_mix
    );
};

// ==========================================================
// EQUAL-POWER PANNING
// ==========================================================

// Returns (left, right) gain pair
pan_gains(pan) = cos(pan * ma.PI / 2.0), sin(pan * ma.PI / 2.0);

// ==========================================================
// SINGLE CHANNEL PROCESSING
// ==========================================================

// Process one channel: filter → pan → (left, right)
// Returns 2 signals: left gain applied, right gain applied
channel(tilt, pan, sig) = filtered * lg, filtered * rg
with {
    filtered = svf_morph(tilt, sig);
    lg = cos(pan * ma.PI / 2.0);
    rg = sin(pan * ma.PI / 2.0);
};

// Get left output of a channel
channel_l(tilt, pan, sig) = svf_morph(tilt, sig) * cos(pan * ma.PI / 2.0);

// Get right output of a channel
channel_r(tilt, pan, sig) = svf_morph(tilt, sig) * sin(pan * ma.PI / 2.0);

// ==========================================================
// MAIN PROCESS: 4 inputs → stereo output
// ==========================================================

process(in1, in2, in3, in4) = sum_l * gain, sum_r * gain
with {
    gain = 0.5;
    sum_l = channel_l(tilt1, pan1, in1)
          + channel_l(tilt2, pan2, in2)
          + channel_l(tilt3, pan3, in3)
          + channel_l(tilt4, pan4, in4);

    sum_r = channel_r(tilt1, pan1, in1)
          + channel_r(tilt2, pan2, in2)
          + channel_r(tilt3, pan3, in3)
          + channel_r(tilt4, pan4, in4);
};
