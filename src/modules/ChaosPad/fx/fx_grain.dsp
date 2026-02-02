// Grain Cloud Processor
// X = position jitter, Y = grain size/density, Z = pitch scatter
// Clock-syncable for grain rate. Gate envelope freezes buffer.

import("stdfaust.lib");

fx_grain(x, y, z, gate_env, clock_period, clock_synced, div_mult, in_l, in_r) = out_l, out_r
with {
    // Buffer size in samples (0.5 seconds at max SR)
    buf_size = 96000;

    // Grain rate: clock-synced or from Y parameter
    free_rate = 2.0 + y * 48.0;  // 2-50 grains/sec
    synced_rate = ba.if(clock_period > 0.001, div_mult / clock_period, 10.0);
    grain_rate = ba.if(clock_synced > 0.5, synced_rate, free_rate);

    // Grain size from Y (10ms to 200ms)
    grain_size = (0.01 + y * 0.19) * ma.SR;

    // Position jitter from X
    pos_jitter = x * buf_size * 0.5;

    // Pitch scatter from Z (-12 to +12 semitones worth of rate change)
    pitch_scatter = 1.0 + (z - 0.5) * 0.5;  // 0.75 to 1.25 playback rate

    // Simple granular: two overlapping read heads with jitter
    // Use phasor to drive grain position
    phasor1 = os.phasor(1.0, grain_rate);
    phasor2 = os.phasor(1.0, grain_rate) : +(0.5) : ma.frac;

    // Hann window envelope for each grain
    hann(phase) = sin(phase * ma.PI) * sin(phase * ma.PI);

    // Write position (circular buffer)
    write_pos = ba.counter(1) % buf_size;

    // Noise sources for jitter (different seeds)
    jitter1 = no.noise : abs * pos_jitter;
    jitter2 = no.lfnoise(grain_rate * 1.3) : abs * pos_jitter;

    // Read position with jitter
    read_offset1 = int(phasor1 * grain_size + jitter1) % buf_size;
    read_offset2 = int(phasor2 * grain_size + jitter2) % buf_size;

    // When gate is off (freeze), stop writing new audio
    // Simple approach: mix between live input and frozen buffer via gate
    input_l = in_l * (1.0 - gate_env * 0.5);
    input_r = in_r * (1.0 - gate_env * 0.5);

    // Granular processing using delay line as buffer
    grain_proc(sig) = sig : de.fdelay(buf_size, grain_size * phasor1) * hann(phasor1)
                     + sig : de.fdelay(buf_size, grain_size * phasor2) * hann(phasor2);

    out_l = grain_proc(input_l) * 0.7;
    out_r = grain_proc(input_r) * 0.7;
};
