// Chaos Flute - ENVELOPE SUBMODULE TEST
// Tests just the breath envelope to verify attack/release work
//
// Expected behavior:
// - Output should ramp up over 'attack' time when gate goes high
// - Output should decay over 'release' time when gate goes low
// - Different attack/release values should produce different envelope shapes

import("stdfaust.lib");

declare name "ChaosFlute Envelope Test";

// Controls
attack = hslider("attack", 0.05, 0.001, 0.5, 0.001) : si.smoo;
release = hslider("release", 0.5, 0.01, 5.0, 0.01) : si.smoo;
gate = hslider("gate", 0, 0, 10, 0.01);

// THE BUG: gate > 1.0 means gate=1.0 doesn't trigger!
// Original (buggy):
envelope_buggy = en.asr(attack, 1.0, release, gate > 1.0);

// Fixed version - gate > 0.5 triggers on standard gate signals
envelope_fixed = en.asr(attack, 1.0, release, gate > 0.5);

// Output both for comparison (left = buggy, right = fixed)
process = envelope_buggy, envelope_fixed;
