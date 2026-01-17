// Chaos Flute - FIXED VERSION
// Same as original but with gate > 0.5 instead of gate > 1.0
//
// Changes from original:
// - Line 48: gate > 1.0 -> gate > 0.5

import("stdfaust.lib");
import("physmodels.lib");

declare name "Chaos Flute (Fixed)";
declare author "WiggleRoom";
declare description "Physical flute model with chaos and overblow - FIXED gate trigger";

// ==========================================================
// 1. CONTROLS
// ==========================================================

attack = hslider("attack", 0.05, 0.001, 0.5, 0.001) : si.smoo;
gate = hslider("gate", 0, 0, 10, 0.01);
growl = hslider("growl", 0, 0, 1, 0.01) : si.smoo;
mouth = hslider("mouth", 0.5, 0.1, 0.9, 0.01) : si.smoo;
pressure = hslider("pressure", 0.6, 0, 1.5, 0.01) : si.smoo;
release = hslider("release", 0.5, 0.01, 5.0, 0.01) : si.smoo;
reverb_mix = hslider("reverb", 0.3, 0, 1, 0.01) : si.smoo;
volts = hslider("volts", 0, -5, 10, 0.001);
freq = max(80, 261.62 * (2.0 ^ volts));

// ==========================================================
// 2. THE "LUNG" - FIXED: gate > 0.5 instead of gate > 1.0
// ==========================================================

breath_env = en.asr(attack, 1.0, release, gate > 0.5);

// ==========================================================
// 3. THE BLOWER
// ==========================================================

vib_freq = 5.0;
vib_gain = 0.02;
breath_cutoff = 2000 + growl * 6000;

blow_signal = pm.blower(
    breath_env * pressure,
    growl * 0.15,
    breath_cutoff,
    vib_freq,
    vib_gain
);

// ==========================================================
// 4. THE FLUTE MODEL
// ==========================================================

base_tube = 0.6;
freq_ratio = 261.62 / freq;
tube_length = min(2.0, max(0.1, base_tube * freq_ratio));

flute_sound = pm.fluteModel(tube_length, mouth, blow_signal);

// ==========================================================
// 5. OUTPUT PROCESSING
// ==========================================================

soft_clip = ef.cubicnl(0.8, 0);
dc_block = fi.dcblocker;
output_gain = 0.8;

// ==========================================================
// 6. REVERB
// ==========================================================

reverb_processor = re.zita_rev1_stereo(0, 200, 6000, 3, 4, 44100);

dry_wet_mix(dry_l, dry_r, wet_l, wet_r) =
    dry_l * (1 - reverb_mix) + wet_l * reverb_mix,
    dry_r * (1 - reverb_mix) + wet_r * reverb_mix;

dry_signal = flute_sound : soft_clip : dc_block : *(output_gain);
process = dry_signal <: (_, _), reverb_processor : dry_wet_mix;
