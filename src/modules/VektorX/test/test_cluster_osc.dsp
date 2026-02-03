// Test wrapper for vhikk_cluster_osc.lib
import("stdfaust.lib");
vh_co = library("vhikk_cluster_osc.lib");

// Test controls
freq = hslider("freq", 220, 20, 2000, 1);
morph = hslider("morph", 0.5, 0, 1, 0.01) : si.smoo;
spread = hslider("spread", 0.3, 0, 1, 0.01) : si.smoo;
width = hslider("width", 0.5, 0, 1, 0.01) : si.smoo;

// Stereo cluster oscillator
process = vh_co.cluster_osc(freq, morph, spread, width);
