// Test wrapper for vhikk_freq_shifter.lib
import("stdfaust.lib");
vh_fs = library("vhikk_freq_shifter.lib");

// Test controls
shift_hz = hslider("shift_hz", 0, -100, 100, 0.1) : si.smoo;
freq = hslider("freq", 440, 20, 2000, 1);

// Test signal
test_signal = os.osc(freq);

// Apply frequency shifting
process = vh_fs.freq_shifter(test_signal, shift_hz) <: _, _;
