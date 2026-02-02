// Pitch Shifter (dual-grain overlap)
// X = semitones -12 to +12, Y = detune/spread, Z = grain size (quality)
// Gate envelope controls VCA on wet signal

import("stdfaust.lib");

fx_pitch(x, y, z, gate_env, clock_period, clock_synced, div_mult, in_l, in_r) = out_l, out_r
with {
    // Map X to semitones (-12 to +12)
    semitones = (x * 24.0 - 12.0) : si.smoo;

    // Map Y to detune spread in cents (-50 to +50)
    detune_cents = (y - 0.5) * 100.0 : si.smoo;

    // Map Z to grain/window size (50ms to 200ms)
    win_ms = 50.0 + z * 150.0;
    win_samples = int(win_ms * 0.001 * ma.SR);

    // Pitch shift ratio
    ratio = pow(2.0, semitones / 12.0);
    ratio_detuned = pow(2.0, (semitones + detune_cents / 100.0) / 12.0);

    // Dual-grain pitch shifter using delay line modulation
    // Two overlapping grains with Hann windowing for smooth crossfade
    max_win = int(0.25 * ma.SR);  // 250ms max window

    // Phasors for two grains (offset by half period)
    rate = (1.0 - ratio) * ma.SR / max(1, win_samples);
    ph1 = os.phasor(1.0, abs(rate));
    ph2 = (ph1 + 0.5) : ma.frac;

    // Hann windows
    w1 = sin(ph1 * ma.PI) : *(2.0) : min(1.0);
    w2 = sin(ph2 * ma.PI) : *(2.0) : min(1.0);

    // Delay amounts (modulated by phasor)
    d1 = ph1 * win_samples;
    d2 = ph2 * win_samples;

    // Pitch shift via variable delay
    shift(sig, d, w) = sig : de.fdelay(max_win, max(1, d)) * w;

    // Main pitch shift
    pitch_l = shift(in_l, d1, w1) + shift(in_l, d2, w2);

    // Detuned copy for spread (slightly different ratio on R channel)
    d1r = ph1 * win_samples * ratio_detuned / max(0.01, ratio);
    d2r = ph2 * win_samples * ratio_detuned / max(0.01, ratio);
    pitch_r = shift(in_r, d1r, w1) + shift(in_r, d2r, w2);

    // VCA on wet controlled by gate
    out_l = pitch_l * gate_env;
    out_r = pitch_r * gate_env;
};
