// Plate Reverb (FDN)
// X = decay 0.5s-10s, Y = damping 500Hz-10kHz, Z = pre-delay 0-200ms
// Gate envelope mutes reverb input (tail continues)

import("stdfaust.lib");

fx_reverb(x, y, z, gate_env, clock_period, clock_synced, div_mult, in_l, in_r) = reverb_chain
with {
    // Map X to decay time (0.5 to 10 seconds) -> feedback coefficient
    decay_time = 0.5 + x * 9.5 : si.smoo;
    // Approximate: longer decay = higher feedback
    fb = min(0.98, 1.0 - (3.0 / (decay_time * ma.SR)) * 1000.0) : max(0.3) : si.smoo;

    // Map Y to damping frequency (500Hz to 10kHz)
    damp_freq = 500.0 + y * 9500.0 : si.smoo;

    // Map Z to pre-delay (0 to 200ms)
    predelay_ms = z * 200.0 : si.smoo;
    predelay_samples = max(1, int(predelay_ms * 0.001 * ma.SR));
    max_predelay = int(0.25 * ma.SR);

    // Pre-delay
    predelay(sig) = sig : de.fdelay(max_predelay, predelay_samples);

    // Gate controls input to reverb (mute input, tail continues)
    input_l = in_l * gate_env;
    input_r = in_r * gate_env;

    // Reverb parameters
    reverb_fb = fb;
    reverb_damp = 1.0 - (damp_freq / ma.SR);

    // Stereo reverb chain (returns 2 outputs directly)
    // re.stereo_freeverb(fb1, fb2, damp, spread) : 2->2
    reverb_chain = predelay(input_l), predelay(input_r) :
        re.stereo_freeverb(reverb_fb, reverb_fb, reverb_damp, 0.5);
};
