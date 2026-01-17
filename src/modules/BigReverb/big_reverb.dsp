// Big Reverb - Lush Stereo Reverb
// Uses Zita Rev1 algorithm - high-quality 8-line FDN reverb

import("stdfaust.lib");

declare name "Big Reverb";
declare author "WiggleRoom";
declare description "Lush stereo reverb with Zita Rev1 algorithm";

// ==========================================================
// 1. CONTROLS
// ==========================================================

// DAMPING: High frequency rolloff (100Hz - 15kHz)
damping_freq = hslider("damping", 5000, 100, 15000, 10) : si.smoo;

// DECAY_HIGH: T60 at mid/high frequencies (0.1 - 20 seconds)
// NOTE: max(0.1) clamp prevents NaN - si.smoo starts at 0 which causes
// division by zero in Zita's internal calculations
decay_high = hslider("decay_high", 2, 0.1, 20, 0.1) : si.smoo : max(0.1);

// DECAY_LOW: T60 at bass frequencies (0.1 - 60 seconds)
// NOTE: max(0.1) clamp prevents NaN for same reason
decay_low = hslider("decay_low", 3, 0.1, 60, 0.1) : si.smoo : max(0.1);

// MIX: Dry/Wet balance
mix = hslider("mix", 0.5, 0, 1, 0.01) : si.smoo;

// PREDELAY: Initial delay before reverb (0 - 200ms)
predelay_ms = hslider("predelay", 20, 0, 200, 1) : si.smoo;

// XOVER: Crossover frequency between bass and mid decay (50 - 6000 Hz)
xover_freq = hslider("xover", 200, 50, 6000, 10) : si.smoo;

// ==========================================================
// 2. DSP LOGIC
// ==========================================================

// Predelay using basic delay (convert ms to samples)
predelay_samps = predelay_ms * 0.001 * ma.SR;
add_predelay = de.delay(10000, predelay_samps), de.delay(10000, predelay_samps);

// Zita Rev1 stereo reverb - 8-line feedback delay network
// zita_rev1_stereo(rdel, f1, f2, t60dc, t60m, fsmax)
//   rdel   = internal delay reference in ms (60 is typical)
//   f1     = crossover frequency (bass/mid boundary)
//   f2     = damping frequency (HF rolloff point)
//   t60dc  = T60 decay time at DC (bass, in seconds)
//   t60m   = T60 decay time at mid frequencies (in seconds)
//   fsmax  = max sample rate (for sizing internal buffers)
core_reverb = re.zita_rev1_stereo(60, xover_freq, damping_freq, decay_low, decay_high, 48000);

// ==========================================================
// 3. MAIN PROCESS
// ==========================================================

// Dry/Wet mix: split input, add predelay to wet, process through reverb, mix back
process = _,_ <: (_,_, (add_predelay : core_reverb)) :
    ( *(1-mix), *(1-mix), *(mix), *(mix) ) :> _,_;
