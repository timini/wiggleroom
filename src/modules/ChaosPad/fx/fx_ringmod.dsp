// Ring Modulator with Drive
// X = carrier freq 20Hz-2kHz, Y = drive 1-5x, Z = carrier waveform morph (sin->square)
// Gate envelope controls VCA on wet signal

import("stdfaust.lib");

fx_ringmod(x, y, z, gate_env, clock_period, clock_synced, div_mult, in_l, in_r) = out_l, out_r
with {
    // Map X to carrier frequency (exponential: 20Hz to 2kHz)
    carrier_freq = 20.0 * pow(100.0, x) : si.smoo;

    // Map Y to drive amount (1x to 5x)
    drive = 1.0 + y * 4.0 : si.smoo;

    // Map Z to waveform morph: 0 = sine, 1 = square
    morph = z : si.smoo;

    // Carrier oscillator with waveform morphing
    sine_carrier = os.osc(carrier_freq);
    square_carrier = os.square(carrier_freq);
    carrier = sine_carrier * (1.0 - morph) + square_carrier * morph;

    // Ring modulation with pre-drive
    ring(sig) = ma.tanh(sig * drive) * carrier;

    // VCA on wet controlled by gate
    out_l = ring(in_l) * gate_env;
    out_r = ring(in_r) * gate_env;
};
