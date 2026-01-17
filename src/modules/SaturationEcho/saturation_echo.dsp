// Saturation Echo - Vintage Tape Delay Emulation
// Filter and Saturator inside the feedback loop
// Each repeat gets darker and grittier (Dub sound)

import("stdfaust.lib");

declare name "Saturation Echo";
declare author "WiggleRoom";
declare description "Vintage tape delay with saturation and wobble";

// ==========================================================
// 1. CONTROLS (Mapped to VCV Knobs)
// ==========================================================

// TIME: 1ms to 1000ms
// We use smoothing (: si.smoo) to prevent zipper noise, but allow pitch warps
time_ms  = hslider("time", 350, 1, 1000, 0.1) : si.smoo;

// FEEDBACK: 0% to 110% (Goes to 1.1 for self-oscillation/explosion)
feedback = hslider("feedback", 0.5, 0, 1.1, 0.001);

// TONE: Lowpass Filter Cutoff (Darker repeats)
tone     = hslider("tone", 4000, 100, 15000, 10) : si.smoo;

// DRIVE: Saturation amount inside the loop
drive    = hslider("drive", 0.2, 0, 1, 0.01);

// WOBBLE: Simulates broken tape motor (Wow/Flutter)
wobble_amount = hslider("wobble", 0, 0, 10, 0.01); // 0 to 10ms variance

// MIX: Dry/Wet
mix      = hslider("mix", 0.5, 0, 1, 0.01) : si.smoo;


// ==========================================================
// 2. DSP LOGIC
// ==========================================================

// A. The Modulator (Tape Motor Wobble)
// A slow, random-ish sine wave (3Hz)
lfo = os.osc(3.0) * wobble_amount;

// B. The Saturation Function
// A cubic soft-clipper that warms up the signal
tape_saturation = ef.cubicnl(drive, 0);

// C. The Filter Function
// A standard LowPass
tape_filter = fi.lowpass(2, tone);

// D. The Tape Loop
// Input -> Delay(Time + Wobble) -> Filter -> Saturation -> Feedback
// We use de.fdelay4 for high-quality cubic interpolation (clean pitch shifting)
max_delay = 96000; // Buffer for approx 2 seconds at 48k
calc_delay_samps = max(1, (time_ms * 0.001 * ma.SR) + lfo);

loop = + ~ (de.fdelay4(max_delay, calc_delay_samps) : tape_filter : tape_saturation : *(feedback));

// E. Dry/Wet Mix
drywet(dw, dry, wet) = dry * (1 - dw) + wet * dw;


// ==========================================================
// 3. MAIN PROCESS
// ==========================================================

// Stereo path with dry/wet mixing
// Each channel processed independently through the loop
// Split input to dry path and wet path, then mix
channel = _ <: _, loop : drywet(mix);
process = channel, channel;
