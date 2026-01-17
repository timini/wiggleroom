// Infinite Folder - West Coast Wavefolder
// Creates complex metallic harmonics by folding waveforms
// Essential for turning simple waves into aggressive, screaming leads

import("stdfaust.lib");

declare name "Infinite Folder";
declare author "WiggleRoom";
declare description "Sine-based wavefolder for West Coast synthesis";

// ==========================================================
// 1. CONTROLS (alphabetical for Faust parameter ordering)
// ==========================================================

// DRIVE: Input gain (1x to 20x)
// Determines how many "folds" occur
drive = hslider("drive", 1.0, 1.0, 20.0, 0.01) : si.smoo;

// MIX: Dry/Wet Blend
mix = hslider("mix", 1.0, 0.0, 1.0, 0.01) : si.smoo;

// SYMMETRY: DC Offset (-1.0 to 1.0)
// Shifts wave before folding to create asymmetric/even harmonics
symmetry = hslider("symmetry", 0.0, -1.0, 1.0, 0.01) : si.smoo;

// ==========================================================
// 2. DSP LOGIC
// ==========================================================

// The Folding Function
// sin(x) as a soft folder - as x increases via drive,
// sin(x) cycles back and forth between -1 and 1
// We scale by PI to get proper folding behavior
folder_circuit(in) = sin((in + symmetry) * drive * ma.PI);

// ==========================================================
// 3. MAIN PROCESS
// ==========================================================

// Signal Flow:
// Input -> Split -> (Dry Path) + (Fold Path) -> Interpolate
process = _ <: dry_path, wet_path : interpolator
with {
    dry_path = _;

    // Wet path goes through the folder
    wet_path = folder_circuit;

    // Linear interpolation blend
    interpolator(dry, wet) = dry * (1.0 - mix) + wet * mix;
};
