// Test wrapper for vhikk_soft_clip.lib
import("stdfaust.lib");
vh_sc = library("vhikk_soft_clip.lib");

// Test controls
drive = hslider("drive", 1.0, 1.0, 5.0, 0.01) : si.smoo;
freq = hslider("freq", 440, 20, 2000, 1);

// Generate test signal and apply soft clipping
test_signal = os.osc(freq);
process = vh_sc.soft_clip(test_signal, drive) <: _, _;
