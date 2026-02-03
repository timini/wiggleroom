// Bitcrush / Decimator
// X = sample rate reduction, Y = bit depth 2-16, Z = jitter/noise
// Gate envelope controls hard bypass

import("stdfaust.lib");

fx_bitcrush(x, y, z, gate_env, clock_period, clock_synced, div_mult, in_l, in_r) = out_l, out_r
with {
    // Map X to sample rate reduction factor (1 = no reduction, 100 = heavy)
    sr_factor = max(1, int(1.0 + (1.0 - x) * 99.0));

    // Map Y to bit depth (2 to 16)
    bits = 2.0 + y * 14.0 : si.smoo;
    levels = pow(2.0, bits);

    // Map Z to noise/jitter amount
    jitter_amt = z * 0.1 : si.smoo;

    // Jitter noise source
    jitter = no.noise * jitter_amt;

    // Sample rate reduction (zero-order hold)
    decimate(sig) = sig : ba.downSample(sr_factor);

    // Bit depth reduction (quantize)
    quantize(sig) = int(sig * levels + jitter) / levels;

    // Bitcrush chain
    crush(sig) = decimate(sig) : quantize;

    // Hard bypass when gate is off
    out_l = crush(in_l) * gate_env + in_l * (1.0 - gate_env);
    out_r = crush(in_r) * gate_env + in_r * (1.0 - gate_env);
};
