// Physical Choir - Vocal Synthesis
// Source-filter model: harmonically rich source -> strong formant bandpass
// The formant bank does ALL the spectral shaping

import("stdfaust.lib");

declare name "Physical Choir";
declare author "WiggleRoom";
declare description "Articulated choral synthesizer with vocal formant filtering";

// ==========================================================
// CONTROLS
// ==========================================================

breath = hslider("breath", 0.05, 0, 1.0, 0.01) : si.smoo;
gate = hslider("gate", 0, 0, 10, 0.01);
portamento = hslider("portamento", 0.01, 0, 0.15, 0.01);
reverb_mix = hslider("reverb", 0.45, 0, 1, 0.01) : si.smoo;
tension = hslider("tension", 0.5, 0.1, 0.99, 0.01) : si.smoo;
throat = hslider("throat", 1.0, 0.5, 2.0, 0.01) : si.smoo;
volts = hslider("volts", 0, -5, 10, 0.001);
vowel = hslider("vowel", 0.0, 0, 4, 0.01);

// ==========================================================
// PITCH
// ==========================================================

targetFreq = 261.626 * pow(2.0, volts);
smoothFreq = targetFreq : si.smooth(ba.tau2pole(portamento));

vibratoRate = 5.2 + no.lfnoise0(3) * 0.5;
vibratoLFO = os.osc(vibratoRate) * 0.003;
baseFreq = max(20, min(2000, smoothFreq * (1.0 + vibratoLFO)));

// ==========================================================
// ENVELOPES
// ==========================================================

gateOn = gate > 0.9;
gateTrig = gateOn & (gateOn' == 0);

ampEnv = gateOn : en.asr(0.08, 1.0, 0.5) : si.smooth(0.998);

consonantDip = gateTrig : en.ar(0.001, 0.06);
consonantShape = 1.0 - (consonantDip * 0.7);
articulation = ampEnv * consonantShape;

formantOpenness = gateOn : en.asr(0.04, 1.0, 0.3) : si.smooth(0.995);

// ==========================================================
// GLOTTAL SOURCE - Harmonically RICH, minimal filtering
// ==========================================================
// The source must have energy up to 5kHz+ so that all 5 formants
// have material to work with. The formant bank does the shaping.
// Previous version had lpCutoff at ~1800Hz which starved F2/F3.

glottalSource(freq, tens) = source
with {
    saw = os.sawtooth(freq);
    sq = os.square(freq) * 0.3;
    tri = os.triangle(freq) * 0.4;
    // Tension crossfade: soft(triangle) <-> bright(saw+square)
    source = saw * tens + tri * (1.0 - tens) + sq * tens * 0.5;
    // NO lowpass here - let formants do the shaping
};

// ==========================================================
// CHOIR ENSEMBLE - 4 detuned voices
// ==========================================================

cent(c) = pow(2, c/1200);

voice1 = glottalSource(baseFreq * cent(0), tension);
voice2 = glottalSource(baseFreq * cent(5), tension);
voice3 = glottalSource(baseFreq * cent(-5), tension);
voice4 = glottalSource(baseFreq * cent(8), tension);

tonalL = (voice1 + voice3) * 0.5;
tonalR = (voice2 + voice4) * 0.5;

// ==========================================================
// BREATH / WHISPER - Mixed before formants
// ==========================================================

whisperNoiseL = no.noise * 0.4;
whisperNoiseR = no.noise * -0.4;

voiceMixL = tonalL * (1.0 - breath * 0.7) + whisperNoiseL * breath;
voiceMixR = tonalR * (1.0 - breath * 0.7) + whisperNoiseR * breath;

// ==========================================================
// FORMANT FILTERS - Strong parallel bandpass
// ==========================================================
// Parallel bandpass passes ONLY formant frequencies.
// High Q (8-10) and high gain (1.0-2.0) for clear vowels.
// Output is then scaled down to prevent clipping.
//
// Phonetics reference (adult male):
//   Ah(ɑ): F1=730  F2=1090 F3=2440
//   Eh(ɛ): F1=660  F2=1720 F3=2410
//   Ee(i): F1=270  F2=2290 F3=3010
//   Oh(ɔ): F1=570  F2=840  F3=2410
//   Oo(u): F1=300  F2=870  F3=2240

interp(x, a, b) = a * (1-x) + b * x;
s_vowel = vowel : si.smooth(ba.tau2pole(0.15));

// F1 - jaw openness (THE primary vowel dimension)
f1_target(v) = ba.if(v<1, interp(v, 730, 660),
              ba.if(v<2, interp(v-1, 660, 270),
              ba.if(v<3, interp(v-2, 270, 570),
                         interp(v-3, 570, 300))));

// F2 - tongue front/back (THE secondary vowel dimension)
f2_target(v) = ba.if(v<1, interp(v, 1090, 1720),
              ba.if(v<2, interp(v-1, 1720, 2290),
              ba.if(v<3, interp(v-2, 2290, 840),
                         interp(v-3, 840, 870))));

// F3 - voice quality
f3_target(v) = ba.if(v<1, interp(v, 2440, 2410),
              ba.if(v<2, interp(v-1, 2410, 3010),
              ba.if(v<3, interp(v-2, 3010, 2410),
                         interp(v-3, 2410, 2240))));

// F4 - presence
f4_target(v) = ba.if(v<1, interp(v, 3300, 3300),
              ba.if(v<2, interp(v-1, 3300, 3500),
              ba.if(v<3, interp(v-2, 3500, 3500),
                         interp(v-3, 3500, 3300))));

// F5 - air
f5_target(v) = ba.if(v<1, interp(v, 4500, 4500),
              ba.if(v<2, interp(v-1, 4500, 4800),
              ba.if(v<3, interp(v-2, 4800, 4700),
                         interp(v-3, 4700, 4500))));

// Apply throat scaling
f1_raw = f1_target(s_vowel) / throat;
f2_raw = f2_target(s_vowel) / throat;
f3_raw = f3_target(s_vowel) / throat;
f4_raw = f4_target(s_vowel) / throat;
f5_raw = f5_target(s_vowel) / throat;

// Formant opening during consonant->vowel
f1_hz = max(150, f1_raw * (0.4 + formantOpenness * 0.6));
f2_hz = max(400, f2_raw * (0.5 + formantOpenness * 0.5));
f3_hz = max(800, f3_raw * (0.7 + formantOpenness * 0.3));
f4_hz = max(2500, f4_raw);
f5_hz = max(3500, f5_raw);

// Cascaded formant filters for strong vowel selectivity
// 4th-order (-24dB/oct) rejection on F1/F2 = clean vowel peaks
// Lower gains to prevent saturation which creates false harmonics
f1_filt(sig) = sig : fi.resonbp(f1_hz, 7, 0.8) : fi.resonbp(f1_hz, 7, 0.6);
f2_filt(sig) = sig : fi.resonbp(f2_hz, 8, 0.7) : fi.resonbp(f2_hz, 8, 0.5);
f3_filt(sig) = sig : fi.resonbp(f3_hz, 6, 0.5);
f4_filt(sig) = sig : fi.resonbp(f4_hz, 5, 0.3);
f5_filt(sig) = sig : fi.resonbp(f5_hz, 4, 0.2);

formantBank(sig) =
    f1_filt(sig) * 0.50 +
    f2_filt(sig) * 0.30 +
    f3_filt(sig) * 0.10 +
    f4_filt(sig) * 0.06 +
    f5_filt(sig) * 0.04;

// ==========================================================
// SIGNAL PATH
// ==========================================================

filteredL = voiceMixL : formantBank;
filteredR = voiceMixR : formantBank;

sourceL = filteredL * articulation;
sourceR = filteredR * articulation;

// Output: LINEAR path to preserve formant spectrum
// tanh/soft-clip creates intermodulation products that regenerate
// harmonics the formant bank rejected, destroying vowel clarity.
// VCV Rack handles ±10V signals fine so no limiter needed.
dc_block = fi.dcblocker;
rumble_hp = fi.highpass(1, 60);
output_gain = 0.045;

processL = sourceL : dc_block : rumble_hp : *(output_gain);
processR = sourceR : dc_block : rumble_hp : *(output_gain);

// ==========================================================
// REVERB - Large cathedral
// ==========================================================

reverb_core = re.zita_rev1_stereo(40, 120, 5500, 8.0, 5.5, 48000);
reverb_boost = 3.0;
reverb_process(l, r) = reverb_core(l, r) : (*(reverb_boost), *(reverb_boost));

wet_gain = sqrt(reverb_mix);
dry_gain = sqrt(1.0 - reverb_mix);

dry_wet(dL, dR, wL, wR) =
    dL * dry_gain + wL * wet_gain,
    dR * dry_gain + wR * wet_gain;

process = (processL, processR) <: (_, _, reverb_process) : dry_wet;
