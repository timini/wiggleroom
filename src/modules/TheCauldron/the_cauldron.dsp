// TheCauldron - Fluid Wave Math Processor
// 4 inputs, 7 analog combination modes
// All math, no logic - everything stays wavey

import("stdfaust.lib");

// ==========================================================
// PARAMETERS
// ==========================================================

// MODE SELECTOR (0 to 6)
mode = hslider("mode", 0, 0, 6, 1) : int;

// MORPH SPEED (For Mode 6)
speed = hslider("speed", 0.5, 0.01, 10.0, 0.01) : si.smoo;

// DRIVE (For Mode 5 - Fold)
drive = hslider("drive", 1.0, 1.0, 5.0, 0.01) : si.smoo;

// ==========================================================
// FLUID ALGORITHMS
// ==========================================================

// 0. SUM (Mixer) - Standard mixing
op_sum(a, b, c, d) = (a + b + c + d) * 0.25;

// 1. RING (Multiply) - Ring modulation
// Creates complex continuous curves from bipolar signals
op_ring(a, b, c, d) = a * b * c * d * 0.1;

// 2. PEAKS (Max) - Analog "Max"
// Traces the mountain tops of the waves
op_max(a, b, c, d) = max(a, max(b, max(c, d)));

// 3. VALLEYS (Min) - Analog "Min"
// Traces the canyon floors of the waves
op_min(a, b, c, d) = min(a, min(b, min(c, d)));

// 4. DIFF (Subtract) - Phase cancellation
// Adds A+B, Subtracts C+D
op_diff(a, b, c, d) = (a + b - c - d) * 0.25;

// 5. FOLD (Wavefolder) - Sums then folds
op_fold(a, b, c, d) = ma.tanh(sin((a + b + c + d) * drive));

// 6. MORPH (Vector Mixer)
// Smoothly crossfades A->B->C->D->A using internal LFO
op_morph(a, b, c, d) = out
with {
    // Phasor goes 0 to 4, wrapping
    phase = os.phasor(4.0, speed);
    idx = int(floor(phase));
    frac = phase - float(idx);

    // Select current and next signals
    curr = ba.selectn(4, idx, a, b, c, d);
    next = ba.selectn(4, (idx + 1) % 4, a, b, c, d);

    // Linear interpolation for smooth transition
    out = curr * (1.0 - frac) + next * frac;
};

// ==========================================================
// MODE SELECTOR
// ==========================================================

select_mode(a, b, c, d) = result
with {
    sum_out = op_sum(a, b, c, d);
    ring_out = op_ring(a, b, c, d);
    max_out = op_max(a, b, c, d);
    min_out = op_min(a, b, c, d);
    diff_out = op_diff(a, b, c, d);
    fold_out = op_fold(a, b, c, d);
    morph_out = op_morph(a, b, c, d);

    result = ba.selectn(7, mode,
        sum_out, ring_out, max_out, min_out,
        diff_out, fold_out, morph_out);
};

// ==========================================================
// MAIN PROCESS
// ==========================================================

process = select_mode;
