// Test wrapper for vhikk_morph_osc.lib
import("stdfaust.lib");
vh_mo = library("vhikk_morph_osc.lib");

// Test controls
freq = hslider("freq", 440, 20, 2000, 1);
morph = hslider("morph", 0.5, 0, 1, 0.01) : si.smoo;

// Stereo output for testing
process = vh_mo.morph_osc(freq, morph) <: _, _;
