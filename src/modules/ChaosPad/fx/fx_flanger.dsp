// Stereo Flanger
// X = LFO rate 0.1-10Hz (or clock-synced), Y = depth, Z = feedback -0.9 to 0.9
// Clock-syncable. Gate envelope controls VCA on wet signal.

import("stdfaust.lib");

fx_flanger(x, y, z, gate_env, clock_period, clock_synced, div_mult, in_l, in_r) = out_l, out_r
with {
    // LFO rate: clock-synced or free-running
    free_rate = 0.1 + x * 9.9;  // 0.1 to 10 Hz
    synced_rate = ba.if(clock_period > 0.001, div_mult / clock_period, 1.0);
    lfo_rate = ba.if(clock_synced > 0.5, synced_rate, free_rate) : si.smoo;

    // Depth (0 to 1)
    depth = y : si.smoo;

    // Feedback (-0.9 to 0.9)
    fb = (z * 1.8 - 0.9) : si.smoo;

    // Flanger delay range: 0.1ms to 5ms
    min_delay = int(0.0001 * ma.SR);
    max_delay = int(0.005 * ma.SR);
    delay_range = max_delay - min_delay;

    // Stereo LFOs (90 degree phase offset)
    lfo_l = os.osc(lfo_rate) * 0.5 + 0.5;  // 0 to 1
    lfo_r = os.osc(lfo_rate) : +(os.osc(lfo_rate * 1.01) * 0.1) : *(0.5) : +(0.5);  // slightly detuned for stereo

    // Modulated delay amounts
    del_l = min_delay + lfo_l * delay_range * depth;
    del_r = min_delay + lfo_r * delay_range * depth;

    // Flanger with feedback
    flange_l = in_l : (+ ~ (de.fdelay(max_delay + 10, max(1, del_l)) : *(fb))) ;
    flange_r = in_r : (+ ~ (de.fdelay(max_delay + 10, max(1, del_r)) : *(fb))) ;

    // VCA on wet controlled by gate
    out_l = flange_l * gate_env;
    out_r = flange_r * gate_env;
};
