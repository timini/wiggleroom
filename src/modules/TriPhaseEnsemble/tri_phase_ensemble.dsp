// Tri-Phase Ensemble - Classic String Ensemble Effect
// Vintage tri-phase BBD chorus
// Transforms thin waveforms into massive orchestral strings

import("stdfaust.lib");

declare name "Tri-Phase Ensemble";
declare author "WiggleRoom";
declare description "Classic 3-voice BBD string ensemble effect";

// ==========================================================
// 1. CONTROLS (alphabetical for Faust parameter ordering)
// ==========================================================

// Depth: Modulation intensity
depth = hslider("depth", 1.0, 0, 2.0, 0.01) : si.smoo;

// Mix: Dry/Wet blend (default 100% wet for ensemble effect)
mix = hslider("mix", 1.0, 0, 1, 0.01) : si.smoo;

// Rate: Speed scaling (1.0 = classic speed)
rate_scale = hslider("rate", 1.0, 0.1, 3.0, 0.01) : si.smoo;

// Tone: BBD lowpass filter (lower = more vintage/lo-fi)
tone = hslider("tone", 8000, 500, 20000, 10) : si.smoo;

// ==========================================================
// 2. LFO SYSTEM
// ==========================================================

// Classic ensemble used two LFOs mixed together:
// Slow (Chorus): ~0.6 Hz
// Fast (Vibrato): ~6.0 Hz
slow_freq = 0.6 * rate_scale;
fast_freq = 6.0 * rate_scale;

// Complex LFO generator combining slow and fast modulation
// phase_offset shifts the wave for each voice (0, 120, 240 degrees)
complex_lfo(phase_offset) = (slow_wave + (fast_wave * 0.15)) * 0.008 * depth
with {
    slow_wave = os.oscp(slow_freq, phase_offset);
    fast_wave = os.oscp(fast_freq, phase_offset);
};

// ==========================================================
// 3. VOICE (BBD Delay Line)
// ==========================================================

// A single delay line with modulation and filtering
// i = voice index (0, 1, 2)
voice(i) = de.fdelay(4096, delay_time) : fi.lowpass(1, tone)
with {
    // Base delay of ~12ms (typical for analog chorus)
    base_samps = 0.012 * ma.SR;

    // Calculate phase for this voice: 0, 2pi/3 (120deg), 4pi/3 (240deg)
    phase = i * (2.0 * ma.PI / 3.0);

    // Modulate delay time (1 + mod ensures delay stays positive)
    mod_val = complex_lfo(phase);
    delay_time = max(1, base_samps * (1 + mod_val));
};

// ==========================================================
// 4. MAIN PROCESS
// ==========================================================

// Mono input -> 3 tri-phase voices -> stereo output with dry/wet
// Voice 1: Left, Voice 2: Center (both), Voice 3: Right
process = _ <: wet_L + dry, wet_R + dry
with {
    // Dry signal (mono to both channels)
    dry = _ * (1 - mix);

    // Wet voices
    v1 = voice(0);
    v2 = voice(1);
    v3 = voice(2);

    // Stereo wet mix
    wet_L = (v1 + v2 * 0.5) * mix;
    wet_R = (v3 + v2 * 0.5) * mix;
};
