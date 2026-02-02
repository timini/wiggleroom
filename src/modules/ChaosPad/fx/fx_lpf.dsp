// Resonant LPF with Drive
// X = cutoff (20Hz-18kHz), Y = resonance (0-0.95), Z = drive/saturation
// Gate envelope controls VCA on wet signal

import("stdfaust.lib");

fx_lpf(x, y, z, gate_env, clock_period, clock_synced, div_mult, in_l, in_r) = out_l, out_r
with {
    // Map X to cutoff frequency (exponential mapping)
    cutoff = 20.0 * pow(900.0, x) : si.smoo;

    // Map Y to resonance (0 to 0.95)
    reso = y * 0.95 : si.smoo;

    // Map Z to drive (1.0 to 5.0)
    drive = 1.0 + z * 4.0 : si.smoo;

    // Pre-drive saturation
    saturate(sig) = ma.tanh(sig * drive);

    // Moog ladder filter
    lpf(sig) = saturate(sig) : ve.moog_vcf(reso, cutoff);

    // Apply gate envelope as VCA on wet signal
    out_l = lpf(in_l) * gate_env;
    out_r = lpf(in_r) * gate_env;
};
