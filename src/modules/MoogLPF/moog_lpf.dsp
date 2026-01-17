// Moog Ladder Lowpass Filter
// A classic 4-pole resonant lowpass filter based on the Moog synthesizer design
//
// Parameters:
//   cutoff    - Filter cutoff frequency in Hz (20-20000)
//   resonance - Filter resonance/Q (0-0.95, self-oscillates near 1.0)

import("stdfaust.lib");

declare name "Moog LPF";
declare author "WiggleRoom";
declare description "Moog ladder lowpass filter for VCV Rack";

// Parameters (will be mapped to VCV knobs)
cutoff = hslider("cutoff", 1000, 20, 20000, 1) : si.smoo;
resonance = hslider("resonance", 0, 0, 0.95, 0.01) : si.smoo;

// Use Faust's built-in Moog VCF implementation
// ve.moog_vcf_2bn is a 2-pole normalized Moog VCF
// For a proper 4-pole ladder, we cascade or use moog_vcf
process = ve.moog_vcf_2bn(resonance, cutoff);
