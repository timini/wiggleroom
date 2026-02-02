// Test wrapper for vhikk_field_filter.lib
import("stdfaust.lib");
vh_ff = library("vhikk_field_filter.lib");

// Test controls
cutoff = hslider("cutoff", 1000, 20, 15000, 1) : si.smoo;
resonance = hslider("resonance", 0.3, 0, 0.95, 0.01) : si.smoo;
tilt = hslider("tilt", 0, -1, 1, 0.01) : si.smoo;

// Test signal (sawtooth for rich harmonics)
test_signal = os.sawtooth(220);

// Apply filter
process = vh_ff.field_filter(test_signal, cutoff, resonance, tilt) <: _, _;
