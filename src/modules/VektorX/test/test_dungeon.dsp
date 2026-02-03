// Test wrapper for vhikk_dungeon.lib
import("stdfaust.lib");
vh_dg = library("vhikk_dungeon.lib");

// Test controls
decay = hslider("decay", 0.5, 0, 1, 0.01) : si.smoo;
damping = hslider("damping", 0.3, 0, 1, 0.01) : si.smoo;
size = hslider("size", 0.5, 0, 1, 0.01) : si.smoo;

// Gate for test impulses
gate = hslider("gate", 0, 0, 10, 0.01);
trig = (gate > 0.9) & (gate' <= 0.9);

// Test signal: short burst when gate triggers
burst_env = en.ar(0.001, 0.05, trig);
test_signal = no.noise * burst_env;

// Apply dungeon reverb
process = vh_dg.dungeon(test_signal, decay, damping, size);
