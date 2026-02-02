// Tape Delay with feedback saturation
// X = delay time 1ms-1s (or clock-synced), Y = feedback 0-110%, Z = wobble/saturation
// Clock-syncable. Gate envelope activates dub send.

import("stdfaust.lib");

fx_delay(x, y, z, gate_env, clock_period, clock_synced, div_mult, in_l, in_r) = out_l, out_r
with {
    // Maximum delay in samples (1 second at 192kHz)
    max_delay = 192000;

    // Delay time: clock-synced or free-running
    free_time = 0.001 + x * 0.999;  // 1ms to 1s
    synced_time = clock_period * div_mult;
    delay_time = ba.if(clock_synced > 0.5, synced_time, free_time) : si.smoo;
    delay_samples = max(1, min(max_delay, int(delay_time * ma.SR)));

    // Feedback (0 to 1.1 for self-oscillation)
    feedback = y * 1.1 : si.smoo;

    // Wobble amount (tape flutter simulation)
    wobble_amt = z * 0.002 : si.smoo;
    wobble_lfo = os.osc(2.3) * wobble_amt * ma.SR;

    // Tape saturation in feedback loop
    tape_sat = z * 2.0 : si.smoo;
    saturate(sig) = ma.tanh(sig * (1.0 + tape_sat));

    // Stereo delay with cross-feedback and wobble
    delay_l = de.fdelay(max_delay, max(1, delay_samples + wobble_lfo));
    delay_r = de.fdelay(max_delay, max(1, delay_samples - wobble_lfo * 0.7));

    // Delay lines with feedback
    // Gate envelope acts as dub send (controls input to delay)
    dub_l = in_l * gate_env;
    dub_r = in_r * gate_env;

    del_l = (dub_l + del_r_fb * feedback * 0.3 + del_l_fb * feedback * 0.7) : delay_l
    with {
        del_l_fb = _ : saturate : fi.dcblocker;
        del_r_fb = _ ~ (*(0));  // placeholder, actual cross-feed below
    };

    // Simplified: two independent delays with saturation
    proc_l = dub_l : (+ ~ (delay_l : saturate : fi.dcblocker : *(feedback))) ;
    proc_r = dub_r : (+ ~ (delay_r : saturate : fi.dcblocker : *(feedback))) ;

    out_l = proc_l;
    out_r = proc_r;
};
