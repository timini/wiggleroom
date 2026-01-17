// Chaos Flute - BLOWER SUBMODULE TEST
// Tests the breath/blower component to verify pressure and growl work
//
// Expected behavior:
// - Higher pressure = louder/more intense output
// - Higher growl = more noise in the breath signal
// - Envelope should shape the output

import("stdfaust.lib");
import("physmodels.lib");

declare name "ChaosFlute Blower Test";

// Controls
attack = hslider("attack", 0.05, 0.001, 0.5, 0.001) : si.smoo;
release = hslider("release", 0.5, 0.01, 5.0, 0.01) : si.smoo;
gate = hslider("gate", 0, 0, 10, 0.01);
pressure = hslider("pressure", 0.6, 0, 1.5, 0.01) : si.smoo;
growl = hslider("growl", 0, 0, 1, 0.01) : si.smoo;

// FIXED envelope (gate > 0.5 instead of gate > 1.0)
breath_env = en.asr(attack, 1.0, release, gate > 0.5);

// Breath noise cutoff
breath_cutoff = 2000 + growl * 6000;

// Vibrato
vib_freq = 5.0;
vib_gain = 0.02;

// The blower with envelope-shaped pressure
blow_signal = pm.blower(
    breath_env * pressure,  // Envelope-shaped pressure
    growl * 0.15,           // Breath noise gain
    breath_cutoff,          // Breath noise cutoff
    vib_freq,               // Vibrato frequency
    vib_gain                // Vibrato gain
);

// Output the blower signal directly (mono)
process = blow_signal;
