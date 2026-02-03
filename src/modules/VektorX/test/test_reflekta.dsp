// Test wrapper for vhikk_reflekta.lib
import("stdfaust.lib");
vh_rk = library("vhikk_reflekta.lib");

// Test controls
time = hslider("time", 0.3, 0, 1, 0.01) : si.smoo;
feedback = hslider("feedback", 0.5, 0, 0.95, 0.01) : si.smoo;
diffusion = hslider("diffusion", 0.5, 0, 1, 0.01) : si.smoo;

// Gate for test impulses
gate = hslider("gate", 0, 0, 10, 0.01);
trig = (gate > 0.9) & (gate' <= 0.9);

// Test signal: short burst when gate triggers
burst_env = en.ar(0.001, 0.1, trig);
test_signal = no.noise * burst_env;

// Apply reflekta
process = vh_rk.reflekta(test_signal, time, feedback, diffusion);
