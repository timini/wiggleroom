// ChaosPad - Kaoss Pad Multi-FX Module
// Stereo multi-FX with X/Y/Z parameter control, 8 algorithms
// Each effect is a separate importable Faust library

import("stdfaust.lib");

// Import effect libraries
import("fx/fx_lpf.dsp");
import("fx/fx_bitcrush.dsp");
import("fx/fx_delay.dsp");
import("fx/fx_grain.dsp");
import("fx/fx_pitch.dsp");
import("fx/fx_reverb.dsp");
import("fx/fx_flanger.dsp");
import("fx/fx_ringmod.dsp");

declare name "ChaosPad";
declare author "WiggleRoom";
declare description "Kaoss Pad-style multi-FX for drum mangling with X/Y/Z control";

// ==========================================================
// CONTROLS (alphabetical for Faust parameter ordering)
// ==========================================================

// 0. clock_period - set from C++ (seconds between clock pulses)
clock_period = hslider("clock_period", 0.5, 0.01, 4.0, 0.001);

// 1. clock_synced - set from C++ (0 or 1)
clock_synced = hslider("clock_synced", 0, 0, 1, 1);

// 2. div_mult - clock division/multiplication ratio
div_mult = hslider("div_mult", 1.0, 0.0625, 16.0, 0.001);

// 3. fx_select - algorithm selector 0-7
fx_select = int(hslider("fx_select", 0, 0, 7, 1));

// 4. gate_env - pre-computed gate envelope from C++
gate_env = hslider("gate_env", 0, 0, 1, 0.001);

// 5. mix - dry/wet 0-1
mix = hslider("mix", 0.5, 0, 1, 0.01) : si.smoo;

// 6. x - final X value (computed in C++)
x = hslider("x", 0.5, 0, 1, 0.001);

// 7. y - final Y value (computed in C++)
y = hslider("y", 0.5, 0, 1, 0.001);

// 8. z - final Z value (computed in C++)
z = hslider("z", 0.5, 0, 1, 0.001);

// ==========================================================
// EFFECT ROUTING
// ==========================================================

// Partially apply common params to each effect -> each becomes 2-in, 2-out
fx0 = fx_lpf(x, y, z, gate_env, clock_period, clock_synced, div_mult);
fx1 = fx_bitcrush(x, y, z, gate_env, clock_period, clock_synced, div_mult);
fx2 = fx_delay(x, y, z, gate_env, clock_period, clock_synced, div_mult);
fx3 = fx_grain(x, y, z, gate_env, clock_period, clock_synced, div_mult);
fx4 = fx_pitch(x, y, z, gate_env, clock_period, clock_synced, div_mult);
fx5 = fx_reverb(x, y, z, gate_env, clock_period, clock_synced, div_mult);
fx6 = fx_flanger(x, y, z, gate_env, clock_period, clock_synced, div_mult);
fx7 = fx_ringmod(x, y, z, gate_env, clock_period, clock_synced, div_mult);

// Split stereo input to all 8 effects, producing 16 outputs (8 stereo pairs)
// Route: deinterleave L/R so first 8 are all L channels, last 8 are all R channels
// Then ba.selectn picks the active channel from each group
fx_select_stereo = _, _ <: fx0, fx1, fx2, fx3, fx4, fx5, fx6, fx7
    : route(16, 16,
        1,1,   2,9,
        3,2,   4,10,
        5,3,   6,11,
        7,4,   8,12,
        9,5,   10,13,
        11,6,  12,14,
        13,7,  14,15,
        15,8,  16,16)
    : ba.selectn(8, fx_select), ba.selectn(8, fx_select);

// ==========================================================
// DRY/WET MIX
// ==========================================================

// Takes (dry_l, dry_r, wet_l, wet_r) -> (out_l, out_r)
// Note: each chain must be fully parenthesized since , has higher precedence than :
dry_wet(dl, dr, wl, wr) =
    ((dl * (1.0 - mix) + wl * mix) : fi.dcblocker : ma.tanh)
    ,
    ((dr * (1.0 - mix) + wr * mix) : fi.dcblocker : ma.tanh);

// ==========================================================
// MAIN PROCESS
// ==========================================================

// Split stereo input: one copy goes dry, one copy through FX, then mix
process = _, _ <: (_, _), fx_select_stereo : dry_wet;
